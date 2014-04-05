//===- lib/Trace/ProcessState.cpp -----------------------------------------===//
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

#include "seec/Trace/ProcessState.hpp"
#include "seec/Util/Fallthrough.hpp"
#include "seec/Util/ModuleIndex.hpp"

#include "llvm/Support/raw_ostream.h"

#include <thread>
#include <functional>

namespace seec {

namespace trace {

//------------------------------------------------------------------------------
// ProcessState
//------------------------------------------------------------------------------

ProcessState::ProcessState(std::shared_ptr<ProcessTrace const> TracePtr,
                           std::shared_ptr<ModuleIndex const> ModIndexPtr)
: Trace(std::move(TracePtr)),
  Module(std::move(ModIndexPtr)),
  DL(&(Module->getModule())),
  ProcessTime(0),
  ThreadStates(Trace->getNumThreads()),
  Mallocs(),
  Memory(),
  KnownMemory(),
  Streams(),
  StreamsClosed(),
  Dirs()
{
  // Setup initial memory state for global variables.
  for (std::size_t i = 0; i < Module->getGlobalCount(); ++i) {
    auto const Global = Module->getGlobal(i);
    assert(Global);
    
    auto const ElemTy = Global->getType()->getElementType();
    auto const Size = DL.getTypeStoreSize(ElemTy);
    auto const Data = Trace->getGlobalVariableInitialData(i, Size);
    auto const Start = Trace->getGlobalVariableAddress(i);
    
    Memory.add(MappedMemoryBlock(Start, Size, Data.data()), EventLocation());
  }
  
  // Setup initial open streams.
  auto const &StreamsInitial = Trace->getStreamsInitial();
  
  switch (StreamsInitial.size()) {
    default:
      SEEC_FALLTHROUGH;
    case 3:
      addStream(StreamState{StreamsInitial[2], "stderr", "w"});
      SEEC_FALLTHROUGH;
    case 2:
      addStream(StreamState{StreamsInitial[1], "stdout", "w"});
      SEEC_FALLTHROUGH;
    case 1:
      addStream(StreamState{StreamsInitial[0], "stdin", "r"});
      SEEC_FALLTHROUGH;
    case 0: break;
  }
  
  // Setup ThreadState objects for each thread.
  auto NumThreads = Trace->getNumThreads();
  for (std::size_t i = 0; i < NumThreads; ++i) {
    ThreadStates[i].reset(new ThreadState(*this, Trace->getThreadTrace(i+1)));
  }
}

ProcessState::~ProcessState() = default;

seec::Maybe<MemoryArea>
ProcessState::getContainingMemoryArea(uintptr_t Address) const {
  // Check global variables.
  for (uint32_t Index = 0; Index < Module->getGlobalCount(); ++Index) {
    auto const Begin = Trace->getGlobalVariableAddress(Index);
    if (Address < Begin)
      continue;
    
    auto const Global = Module->getGlobal(Index);
    auto const Size = DL.getTypeStoreSize(Global->getType()->getElementType());
    auto const Permission = Global->isConstant() ? MemoryPermission::ReadOnly
                                                 : MemoryPermission::ReadWrite;
    
    auto const Area = MemoryArea(Begin, Size, Permission);
    
    if (Area.contains(Address))
      return Area;
  }
  
  // Check dynamic memory allocations.
  {
    auto DynIt = Mallocs.upper_bound(Address);
    if (DynIt != Mallocs.begin()) {
      --DynIt; // DynIt's allocation address is now <= Address.
      
      auto const Area = MemoryArea(DynIt->second.getAddress(),
                                   DynIt->second.getSize());
      
      if (Area.contains(Address))
        return Area;
    }
  }

  // Check known readable/writable regions.
  {
    auto KnownIt = KnownMemory.find(Address);
    if (KnownIt != KnownMemory.end()) {
      // Range of interval is inclusive: [Begin, End]
      auto Length = (KnownIt->End - KnownIt->Begin) + 1;
      return seec::Maybe<MemoryArea>(MemoryArea(KnownIt->Begin,
                                     Length,
                                     KnownIt->Value));
    }
  }
  
  // Check other threads.
  for (auto const &ThreadStatePtr : ThreadStates) {
    auto MaybeArea = ThreadStatePtr->getContainingMemoryArea(Address);
    if (!MaybeArea.assigned<MemoryArea>())
      continue;
    
    return MaybeArea;
  }
    
  return seec::Maybe<MemoryArea>();
}

bool ProcessState::addStream(StreamState Stream)
{
  auto const Address = Stream.getAddress();
  auto const Result = Streams.insert(std::make_pair(Address,
                                                    std::move(Stream)));
  return Result.second;
}

bool ProcessState::removeStream(uintptr_t const Address)
{
  return Streams.erase(Address);
}

bool ProcessState::closeStream(uintptr_t const Address)
{
  auto const It = Streams.find(Address);
  if (It == Streams.end())
    return false;

  StreamsClosed.emplace_back(std::move(It->second));
  Streams.erase(It);
  return true;
}

bool ProcessState::restoreStream(uintptr_t const Address)
{
  if (StreamsClosed.empty() || StreamsClosed.back().getAddress() != Address)
    return false;

  Streams.insert(std::make_pair(Address, std::move(StreamsClosed.back())));
  StreamsClosed.pop_back();
  return true;
}

StreamState *ProcessState::getStream(uintptr_t const Address)
{
  auto const It = Streams.find(Address);
  return It != Streams.end() ? &It->second : nullptr;
}

StreamState const *ProcessState::getStream(uintptr_t const Address) const
{
  auto const It = Streams.find(Address);
  return It != Streams.end() ? &It->second : nullptr;
}

StreamState const *ProcessState::getStreamStdout() const
{
  auto const &StreamsInitial = Trace->getStreamsInitial();
  if (StreamsInitial.size() >= 2)
    return getStream(StreamsInitial[1]);
  return nullptr;
}

bool ProcessState::addDir(DIRState Dir)
{
  auto const Address = Dir.getAddress();
  auto const Result = Dirs.insert(std::make_pair(Address, std::move(Dir)));
  return Result.second;
}

bool ProcessState::removeDir(uintptr_t const Address)
{
  return Dirs.erase(Address);
}

DIRState const *ProcessState::getDir(uintptr_t const Address) const
{
  auto const It = Dirs.find(Address);
  return It != Dirs.end() ? &It->second : nullptr;
}

uintptr_t ProcessState::getRuntimeAddress(llvm::Function const *F) const {
  auto const MaybeIndex = Module->getIndexOfFunction(F);
  assert(MaybeIndex.assigned());
  return Trace->getFunctionAddress(MaybeIndex.get<0>());
}

uintptr_t
ProcessState::getRuntimeAddress(llvm::GlobalVariable const *GV) const {
  auto const MaybeIndex = Module->getIndexOfGlobal(GV);
  assert(MaybeIndex.assigned());
  return Trace->getGlobalVariableAddress(MaybeIndex.get<0>());
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &Out,
                              ProcessState const &State) {
  Out << "Process @" << State.getProcessTime() << "\n";
  
  Out << " Dynamic Allocations: " << State.getMallocs().size() << "\n";
  
  Out << " Known Memory Regions: " << State.getKnownMemory().size() << "\n";
  
  Out << " Memory State Fragments: "
      << State.getMemory().getNumberOfFragments() << "\n";
  Out << State.getMemory();
  
  Out << " Open Streams: " << State.getStreams().size() << "\n";
  for (auto const &Stream : State.getStreams()) {
    Out << Stream.second;
  }
  
  Out << " Open DIRs: " << State.getDirs().size() << "\n";
  for (auto const &Dir : State.getDirs()) {
    Out << Dir.second;
  }
  
  for (auto &ThreadStatePtr : State.getThreadStates()) {
    Out << *ThreadStatePtr;
  }
  
  return Out;
}

} // namespace trace (in seec)

} // namespace seec
