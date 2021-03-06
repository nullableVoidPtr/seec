//===- lib/Runtimes/Tracer/WrapCstring_h.cpp ------------------------------===//
//
//                                    SeeC
//
// This file is distributed under The MIT License (MIT). See LICENSE.TXT for
// details.
//
//===----------------------------------------------------------------------===//
///
/// \file
///
//===----------------------------------------------------------------------===//

#include "SimpleWrapper.hpp"
#include "Tracer.hpp"

#include "seec/RuntimeErrors/FormatSelects.hpp"
#include "seec/Runtimes/MangleFunction.h"
#include "seec/Trace/TraceThreadListener.hpp"
#include "seec/Trace/TraceThreadMemCheck.hpp"
#include "seec/Util/ScopeExit.hpp"

#include "llvm/IR/CallSite.h"

#include <atomic>
#include <cstring>

extern "C" {


//===----------------------------------------------------------------------===//
// strcasecmp
//===----------------------------------------------------------------------===//

int
SEEC_MANGLE_FUNCTION(strcasecmp)
(char const * const s1, char const * const s2)
{
  return seec::SimpleWrapper
          <seec::SimpleWrapperSetting::AcquireGlobalMemoryReadLock>
          {seec::runtime_errors::format_selects::CStdFunction::strcasecmp}
          (strcasecmp,
           [](int const){ return true; },
           seec::ResultStateRecorderForNoOp(),
           seec::wrapInputCString(s1),
           seec::wrapInputCString(s2));
}


//===----------------------------------------------------------------------===//
// strncasecmp
//===----------------------------------------------------------------------===//

int
SEEC_MANGLE_FUNCTION(strncasecmp)
(char const * const s1, char const * const s2, size_t const n)
{
  return seec::SimpleWrapper
          <seec::SimpleWrapperSetting::AcquireGlobalMemoryReadLock>
          {seec::runtime_errors::format_selects::CStdFunction::strncasecmp}
          (strncasecmp,
           [](int const){ return true; },
           seec::ResultStateRecorderForNoOp(),
           seec::wrapInputCString(s1).setLimited(n),
           seec::wrapInputCString(s2).setLimited(n),
           n);
}


//===----------------------------------------------------------------------===//
// strcasestr
//===----------------------------------------------------------------------===//

#if (defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)))
char *
SEEC_MANGLE_FUNCTION(strcasestr)
(char const * const haystack, char const * const needle)
{
#if (defined(__USE_GNU) && defined(__CORRECT_ISO_CPP_STRING_H_PROTO))
  using StrictFnType = char const *(*)(char const *, char const *);
  using EvilFnType   = char       *(*)(char const *, char const *);
  
  StrictFnType originalfnptr = strcasestr;
  EvilFnType fnptr = reinterpret_cast<EvilFnType>(originalfnptr);
#else
  extern char *strcasestr(char const *, char const *) __attribute__((weak));
  assert(strcasestr);
  
  auto const fnptr = strcasestr;
#endif

  return seec::SimpleWrapper
          <seec::SimpleWrapperSetting::AcquireGlobalMemoryReadLock>
          {seec::runtime_errors::format_selects::CStdFunction::strcasestr}
          (fnptr,
           [](char * const){ return true; },
           seec::ResultStateRecorderForNoOp(),
           seec::wrapInputCString(haystack),
           seec::wrapInputCString(needle));
}
#endif


//===----------------------------------------------------------------------===//
// strdup
//===----------------------------------------------------------------------===//

char *
SEEC_MANGLE_FUNCTION(strdup)
(char const *String)
{
  using namespace seec::trace;
  
  auto &ThreadEnv = getThreadEnvironment();
  auto &Listener = ThreadEnv.getThreadListener();
  auto Instruction = ThreadEnv.getInstruction();
  auto InstructionIndex = ThreadEnv.getInstructionIndex();
  
  // Interact with the thread listener's notification system.
  Listener.enterNotification();
  auto DoExit = seec::scopeExit([&](){ Listener.exitPostNotification(); });
  
  // Lock global memory.
  Listener.acquireGlobalMemoryWriteLock();
  Listener.acquireDynamicMemoryLock();
  
  Listener.getActiveFunction()->setActiveInstruction(Instruction);
  
  // Use a CIOChecker to help check memory.
  auto FSFunction = seec::runtime_errors::format_selects::CStdFunction::strdup;
  CStdLibChecker Checker{Listener, InstructionIndex, FSFunction};
  
  // Ensure that String is accessible.
  Checker.checkCStringRead(0, String);
  
  auto Result = strdup(String);
  
  // Record the result.
  Listener.notifyValue(InstructionIndex,
                       Instruction,
                       Result);

  if (Result) {
    auto Size = std::strlen(Result) + 1;
    Listener.recordMalloc(reinterpret_cast<uintptr_t>(Result), Size);
    Listener.recordUntypedState(Result, Size);
  }
  else{
    seec::recordErrno(Listener, errno);
  }

  // Set the object for the returned pointer.
  auto const ResultAddr = reinterpret_cast<uintptr_t>(Result);
  Listener.getActiveFunction()->setPointerObject(
    Instruction,
    Listener.getProcessListener().makePointerObject(ResultAddr));

  return Result;
}


//===----------------------------------------------------------------------===//
// strndup
//===----------------------------------------------------------------------===//

#if (defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)))
char *
SEEC_MANGLE_FUNCTION(strndup)
(char const * const String, size_t const Length)
{
  extern char *strndup(char const *, size_t) __attribute__((weak));
  assert(strndup);

  struct ResultStateRecorderForStrndup {
    void record(seec::trace::TraceProcessListener &ProcessListener,
                seec::trace::TraceThreadListener &ThreadListener,
                char *Value)
    {
      if (Value != nullptr) {
        auto const Length = std::strlen(Value) + 1;
        ThreadListener.recordMalloc(reinterpret_cast<uintptr_t>(Value), Length);
        ThreadListener.recordUntypedState(Value, Length);
      }
    }
  };

  return seec::SimpleWrapper
          <seec::SimpleWrapperSetting::AcquireGlobalMemoryWriteLock,
           seec::SimpleWrapperSetting::AcquireDynamicMemoryLock>
          {seec::runtime_errors::format_selects::CStdFunction::strndup}
          (strndup,
           [](char * const Result){ return Result != nullptr; },
           ResultStateRecorderForStrndup(),
           seec::wrapInputCString(String).setLimited(Length),
           Length);
}
#endif


//===----------------------------------------------------------------------===//
// strnlen
//===----------------------------------------------------------------------===//

size_t
SEEC_MANGLE_FUNCTION(strnlen)
(char const * const string, size_t maxlen)
{
  return seec::SimpleWrapper
          <seec::SimpleWrapperSetting::AcquireGlobalMemoryReadLock>
          {seec::runtime_errors::format_selects::CStdFunction::strnlen}
          (strnlen,
           [](size_t){ return true; },
           seec::ResultStateRecorderForNoOp(),
           seec::wrapInputCString(string).setLimited(maxlen),
           maxlen);
}


//===----------------------------------------------------------------------===//
// strsep
//===----------------------------------------------------------------------===//

char *
SEEC_MANGLE_FUNCTION(strsep)
(char ** const stringp, char const * const delim)
{
  extern char *strsep(char **, char const *) __attribute__((weak));
  assert(strsep);

  using namespace seec;

  auto const FSFunction = runtime_errors::format_selects::CStdFunction::strsep;
  auto &ThreadEnv = seec::trace::getThreadEnvironment();
  auto &Listener = ThreadEnv.getThreadListener();
  auto &Process  = Listener.getProcessListener();

  // Interact with the thread listener's notification system.
  Listener.enterNotification();
  auto DoExit = seec::scopeExit([&](){ Listener.exitPostNotification(); });

  // Lock global memory because strsep will modify *stringp.
  Listener.acquireGlobalMemoryWriteLock();

  // Get information about the call Instruction.
  auto const Instruction      = ThreadEnv.getInstruction();
  auto const InstructionIndex = ThreadEnv.getInstructionIndex();
  auto const ActiveFn         = Listener.getActiveFunction();

  ActiveFn->setActiveInstruction(Instruction);

  // We have to check that the first argument is a valid pointer to a char *,
  // then we have to check if that char * is a valid pointer to a C string. We
  // also have to check if delim is a valid C string.
  seec::trace::CStdLibChecker Checker {Listener, InstructionIndex, FSFunction};

  auto const stringpInt = reinterpret_cast<uintptr_t>(stringp);
  Checker.checkMemoryExistsAndAccessibleForParameter
            (0, stringpInt, sizeof(*stringp),
             runtime_errors::format_selects::MemoryAccess::Write);

  // Slightly misleading that we simply give parameter 0 as the string, but for
  // now it's functional (in the future we could add a temporary note).
  auto const str = *stringp;
  Checker.checkCStringRead(0, str);

  Checker.checkCStringRead(1, delim);

  auto const Result = strsep(stringp, delim);

  // Record the result.
  Listener.notifyValue(InstructionIndex, Instruction, Result);

  // The returned value's pointer information will match *stringp:
  ActiveFn->setPointerObject(Instruction,
                             Process.getInMemoryPointerObject(stringpInt));

  if (*stringp == nullptr) {
    // Update in-memory pointer information for *stringp.
    Process.setInMemoryPointerObject(stringpInt, seec::trace::PointerTarget());
  }
  else {
    // Update memory state for NULL character. *stringp still points to the
    // same allocation (albeit a different C string), so its in memory pointer
    // target does not require updating.
    auto NewTerminatorPtr = *stringp - 1;
    Listener.recordUntypedState(NewTerminatorPtr, 1);
  }

  return Result;
}


//===----------------------------------------------------------------------===//
// strtok
//===----------------------------------------------------------------------===//

char *
SEEC_MANGLE_FUNCTION(strtok)
(char *String, char const *Delimiters)
{
  static std::atomic<unsigned> CallingThreadCount {0};
  static seec::trace::PointerTarget CurrentStringPointerObject{};
  
  auto const NewThreadCount = ++CallingThreadCount;
  auto const OnExit = seec::scopeExit([&](){ --CallingThreadCount; });
  
  auto const FSFunction =
    seec::runtime_errors::format_selects::CStdFunction::Strtok;
  
  auto &ThreadEnv = seec::trace::getThreadEnvironment();
  auto &Listener = ThreadEnv.getThreadListener();
  
  if (NewThreadCount != 1) {
    // This function is not thread-safe, so raise an error.
    using namespace seec::runtime_errors;
    
    Listener.handleRunError(*createRunError<RunErrorType::UnsafeMultithreaded>
                                           (FSFunction),
                            seec::trace::RunErrorSeverity::Fatal,
                            ThreadEnv.getInstructionIndex());
    
    return nullptr;
  }
  
  // Interact with the thread listener's notification system.
  Listener.enterNotification();
  auto DoExit = seec::scopeExit([&](){ Listener.exitPostNotification(); });
  
  // Lock global memory because strtok may write a terminating nul character.
  Listener.acquireGlobalMemoryWriteLock();
  
  // Get information about the call Instruction.
  auto const Instruction      = ThreadEnv.getInstruction();
  auto const InstructionIndex = ThreadEnv.getInstructionIndex();
  auto const ActiveFn         = Listener.getActiveFunction();
  
  ActiveFn->setActiveInstruction(Instruction);

  if (String) {
    llvm::CallSite Call(Instruction);
    CurrentStringPointerObject =
      ActiveFn->getPointerObject(Call.getArgument(0));
  }

  // Check that the arguments are valid C strings. The first argument is
  // allowed to be NULL, in which case we are continuing to tokenize a
  // previously passed string (TODO: ensure that there is a previous string).
  seec::trace::CStdLibChecker Checker {Listener, InstructionIndex, FSFunction};
  
  if (String)
    Checker.checkCStringRead(0, String);
  
  Checker.checkCStringRead(1, Delimiters);
  
  auto const Result = strtok(String, Delimiters);
  
  // Record the result.
  Listener.notifyValue(InstructionIndex, Instruction, Result);
  
  // Record state changes (if any) and set the returned pointer's object.
  if (Result) {
    ActiveFn->setPointerObject(Instruction, CurrentStringPointerObject);

    // The NULL character that terminates this token was inserted by strtok, so
    // we must record it.
    auto const Terminator = Result + strlen(Result);
    Listener.recordUntypedState(Terminator, 1);
  }
  else {
    ActiveFn->setPointerObject(Instruction, seec::trace::PointerTarget{});
  }
  
  return Result;
}


} // extern "C"
