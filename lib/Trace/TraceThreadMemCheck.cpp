#include "TraceThreadMemCheck.hpp"

namespace seec {

namespace trace {


using namespace seec::runtime_errors;

//===------------------------------------------------------------------------===
// getContainingMemoryArea()
//===------------------------------------------------------------------------===

seec::util::Maybe<MemoryArea>
getContainingMemoryArea(TraceThreadListener &Listener,
                        uintptr_t Address) {
  auto const &ProcListener = Listener.getProcessListener();

  // Find the memory area containing Address.
  auto MaybeArea = Listener.getContainingMemoryArea(Address);
  if (!MaybeArea.assigned()) {
    auto ThreadID = Listener.getThreadID();
    MaybeArea = ProcListener.getContainingMemoryArea(Address, ThreadID);
  }

  return MaybeArea;
}

//===------------------------------------------------------------------------===
// checkMemoryAccessOfParameter()
//===------------------------------------------------------------------------===

bool checkMemoryAccessOfParameter(
        TraceThreadListener &Listener,
        uint32_t InstructionIndex,
        seec::runtime_errors::format_selects::CStdFunction Function,
        std::size_t ParameterIndex,
        seec::runtime_errors::format_selects::MemoryAccess Access,
        uintptr_t Address,
        std::size_t Size,
        MemoryArea ContainingArea) {
  using namespace seec::runtime_errors;

  auto const &ProcListener = Listener.getProcessListener();

  // Check that the owned memory area contains the entire access size.
  MemoryArea AccessArea(Address, Size);

  if (!ContainingArea.contains(AccessArea)) {
    Listener.handleRunError(
      createRunError<RunErrorType::PassPointerToInsufficient>
                    (Function,
                     Address,
                     Size,
                     ArgObject{},
                     ContainingArea.address(),
                     ContainingArea.length()),
      RunErrorSeverity::Fatal,
      InstructionIndex);

    return true;
  }

  // If this is a read, check that the memory is initialized.
  if (Access == format_selects::MemoryAccess::Read) {
    auto MemoryState = ProcListener.getTraceMemoryStateAccessor();
    if (!MemoryState->hasKnownState(Address, Size)) {
      Listener.handleRunError(
        createRunError<RunErrorType::PassPointerToUninitialized>
                      (Function, Address, ParameterIndex),
        RunErrorSeverity::Warning,
        InstructionIndex);
    }

    return true;
  }

  return false;
}


//===------------------------------------------------------------------------===
// RuntimeErrorChecker
//===------------------------------------------------------------------------===

bool
RuntimeErrorChecker::memoryExists(uintptr_t Address,
                                  std::size_t Size,
                                  format_selects::MemoryAccess Access,
                                  seec::util::Maybe<MemoryArea> const &Area)
{
  if (Area.assigned())
    return true;
  
  Thread.handleRunError(createRunError<RunErrorType::MemoryUnowned>(Access,
                                                                    Address,
                                                                    Size),
                        RunErrorSeverity::Fatal,
                        Instruction);
  
  return false;
}

bool RuntimeErrorChecker::checkMemoryAccess(uintptr_t Address,
                                            std::size_t Size,
                                            format_selects::MemoryAccess Access,
                                            MemoryArea ContainingArea)
{
  // Check that the owned memory area contains the entire load.
  MemoryArea AccessArea(Address, Size);

  if (!ContainingArea.contains(AccessArea)) {
    Thread.handleRunError(createRunError<RunErrorType::MemoryOverflow>
                                        (Access,
                                         Address,
                                         Size,
                                         ArgObject{},
                                         ContainingArea.address(),
                                         ContainingArea.length()),
                          RunErrorSeverity::Fatal,
                          Instruction);
    return false;
  }

  // If this is a read, check that the memory is initialized.
  if (Access == format_selects::MemoryAccess::Read) {
    auto const &ProcListener = Thread.getProcessListener();
    auto MemoryState = ProcListener.getTraceMemoryStateAccessor();
    
    if (!MemoryState->hasKnownState(Address, Size)) {
      Thread.handleRunError(createRunError<RunErrorType::MemoryUninitialized>
                                          (Address, Size),
                            RunErrorSeverity::Warning,
                            Instruction);
      
      return false;
    }
  }

  return true;
}

bool RuntimeErrorChecker::checkMemoryExistsAndAccessible(
                            uintptr_t Address,
                            std::size_t Size,
                            format_selects::MemoryAccess Access)
{
  auto MaybeArea = getContainingMemoryArea(Thread, Address);

  if (!memoryExists(Address, Size, Access, MaybeArea))
    return false;
  
  return checkMemoryAccess(Address, Size, Access, MaybeArea.get<0>());
}

seec::util::Maybe<MemoryArea>
RuntimeErrorChecker::getCStringInArea(char const *String, MemoryArea Area)
{
  auto const StrAddress = reinterpret_cast<uintptr_t>(String);
  auto const MaxLength = Area.withStart(StrAddress).length();
  
  for (std::size_t Index = 0; Index < MaxLength; ++Index) {
    if (!String[Index]) {
      // Area includes the terminating NUL byte.
      return MemoryArea(StrAddress, Index + 1);
    }
  }

  return seec::util::Maybe<MemoryArea>();
}

MemoryArea RuntimeErrorChecker::getLimitedCStringInArea(char const *String,
                                                        MemoryArea Area,
                                                        std::size_t Limit)
{
  auto const MaybeStrArea = getCStringInArea(String, Area);
  
  if (MaybeStrArea.assigned()) {
    auto const Length = std::min(MaybeStrArea.get<0>().length(), Limit);
    return MaybeStrArea.get<0>().withLength(Length);
  }
  
  return MemoryArea(String, Limit);
}


//===------------------------------------------------------------------------===
// RuntimeErrorChecker
//===------------------------------------------------------------------------===

bool
CStdLibChecker::memoryExistsForParameter(
                  unsigned Parameter,
                  uintptr_t Address,
                  std::size_t Size,
                  format_selects::MemoryAccess Access,
                  seec::util::Maybe<MemoryArea> const &Area)
{
  if (Area.assigned())
    return true;
  
  Thread.handleRunError(createRunError<RunErrorType::PassPointerToUnowned>
                                      (Function,
                                       Address,
                                       Parameter),
                        RunErrorSeverity::Fatal,
                        Instruction);
  
  return false;
}

bool
CStdLibChecker::checkMemoryAccessForParameter(
                  unsigned Parameter,
                  uintptr_t Address,
                  std::size_t Size,
                  format_selects::MemoryAccess Access,
                  MemoryArea ContainingArea)
{
  // Check that the owned memory area contains the entire load.
  MemoryArea AccessArea(Address, Size);

  if (!ContainingArea.contains(AccessArea)) {
    Thread.handleRunError(
              createRunError<RunErrorType::PassPointerToInsufficient>
                            (Function,
                             Address,
                             Size,
                             ArgObject{},
                             ContainingArea.address(),
                             ContainingArea.length()),
              RunErrorSeverity::Fatal,
              Instruction);
    
    return false;
  }

  // If this is a read, check that the memory is initialized.
  if (Access == format_selects::MemoryAccess::Read) {
    auto const &ProcListener = Thread.getProcessListener();
    auto MemoryState = ProcListener.getTraceMemoryStateAccessor();
    
    if (!MemoryState->hasKnownState(Address, Size)) {
      Thread.handleRunError(
                createRunError<RunErrorType::PassPointerToUninitialized>
                              (Function, Address, Parameter),
                RunErrorSeverity::Warning,
                Instruction);
      
      return false;
    }
  }
  
  return true;
}

bool CStdLibChecker::checkMemoryExistsAndAccessibleForParameter(
                        unsigned Parameter,
                        uintptr_t Address,
                        std::size_t Size,
                        format_selects::MemoryAccess Access)
{
  auto MaybeArea = getContainingMemoryArea(Thread, Address);

  if (!memoryExistsForParameter(Parameter, Address, Size, Access, MaybeArea))
    return false;
  
  return checkMemoryAccessForParameter(Parameter,
                                       Address,
                                       Size,
                                       Access,
                                       MaybeArea.get<0>());
}

bool CStdLibChecker::checkMemoryDoesNotOverlap(MemoryArea Area1,
                                               MemoryArea Area2)
{
  auto const Overlap = Area1.intersection(Area2);
  if (!Overlap.length())
    return true;

  Thread.handleRunError(createRunError<RunErrorType::OverlappingSourceDest>
                                      (Function,
                                       Overlap.start(),
                                       Overlap.length()),
                        RunErrorSeverity::Warning,
                        Instruction);
  
  return false;
}

bool CStdLibChecker::checkCStringIsValid(uintptr_t Address,
                                         unsigned Parameter,
                                         seec::util::Maybe<MemoryArea> Area)
{
  if (Area.assigned())
    return true;
  
  Thread.handleRunError(createRunError<RunErrorType::InvalidCString>
                                      (Function,
                                       Address,
                                       Parameter),
                        RunErrorSeverity::Fatal,
                        Instruction);

  return false;
}

std::size_t CStdLibChecker::checkCStringRead(unsigned Parameter,
                                             char const *String)
{
  auto StrAddr = reinterpret_cast<uintptr_t>(String);

  // Check if String points to owned memory.
  auto const Area = getContainingMemoryArea(Thread, StrAddr);
  if (!memoryExists(StrAddr, 1, format_selects::MemoryAccess::Read, Area))
    return 0;

  // Check if Str points to a valid C string.
  auto const StrArea = getCStringInArea(String, Area.get<0>());
  if (!checkCStringIsValid(StrAddr, Parameter, StrArea))
    return 0;

  auto const StrLength = StrArea.get<0>().length();

  // Check if the read from Str is OK. We already know that the size of the
  // read is valid, from using getCStringInArea, but this will check if the
  // memory is initialized.
  checkMemoryAccessForParameter(Parameter,
                                StrAddr,
                                StrLength,
                                format_selects::MemoryAccess::Read,
                                StrArea.get<0>());
  
  return StrLength;
}

std::size_t CStdLibChecker::checkLimitedCStringRead(unsigned Parameter,
                                                    char const *String,
                                                    std::size_t Limit)
{
  auto StrAddr = reinterpret_cast<uintptr_t>(String);
  
  // Check if String points to owned memory.
  auto const Area = getContainingMemoryArea(Thread, StrAddr);
  if (!memoryExists(StrAddr, 1, format_selects::MemoryAccess::Read, Area))
    return 0;
  
  // Find the C string that String refers to, within Limit.
  auto const StrArea = getLimitedCStringInArea(String, Area.get<0>(), Limit);

  // Check if the read from Str is OK.
  checkMemoryAccessForParameter(Parameter,
                                StrAddr,
                                StrArea.length(),
                                format_selects::MemoryAccess::Read,
                                StrArea);

  return StrArea.length();
}


} // namespace trace (in seec)

} // namespace seec
