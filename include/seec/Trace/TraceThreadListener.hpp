//===- include/seec/Trace/TraceThreadListener.hpp ------------------- C++ -===//
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

#ifndef SEEC_TRACE_TRACETHREADLISTENER_HPP
#define SEEC_TRACE_TRACETHREADLISTENER_HPP

#include "seec/DSA/MemoryBlock.hpp"
#include "seec/RuntimeErrors/RuntimeErrors.hpp"
#include "seec/Trace/DetectCalls.hpp"
#include "seec/Trace/RuntimeValue.hpp"
#include "seec/Trace/TracedFunction.hpp"
#include "seec/Trace/TraceEventWriter.hpp"
#include "seec/Trace/TraceFormat.hpp"
#include "seec/Trace/TraceProcessListener.hpp"
#include "seec/Trace/TraceStorage.hpp"
#include "seec/Util/Maybe.hpp"
#include "seec/Util/ModuleIndex.hpp"
#include "seec/Util/Serialization.hpp"
#include "seec/Util/SynchronizedExit.hpp"

#include "llvm/ADT/ArrayRef.h"

// #include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <memory>
#include <vector>


namespace llvm {
  class AllocaInst;
  class BinaryOperator;
  class Function;
  class CallInst;
  class LoadInst;
  class StoreInst;
  class Instruction;
  struct GenericValue;
} // namespace llvm


namespace seec {

namespace trace {


/// Describes the severity of a detected run-time error.
enum class RunErrorSeverity {
  Warning,
  Fatal
};


/// \brief Records thread-specific execution events.
///
///
class TraceThreadListener
: public seec::trace::CallDetector<TraceThreadListener>
{
  friend class CStdLibChecker;
  friend class CIOChecker;
  
  // Don't allow copying.
  TraceThreadListener(TraceThreadListener const &) = delete;
  TraceThreadListener &operator=(TraceThreadListener const &) = delete;

  /// \name Constant information.
  /// @{

  /// The process listener for this process.
  TraceProcessListener &ProcessListener;

  /// Support this thread's participation in synchronized exits.
  SupportSynchronizedExit const SupportSyncExit;

  /// The unique integer identifier for this thread.
  uint32_t const ThreadID;
  
  /// Size limit for thread event output.
  offset_uint const ThreadEventLimit;

  /// @} (Constant information)


  /// \name Outputs
  /// @{
  
  /// Allocates output streams.
  OutputStreamAllocator &StreamAllocator;
  
  /// Controls trace file output.
  bool OutputEnabled;
  
  /// Handles writing event data.
  EventWriter EventsOut;

  /// @} (Outputs)


  /// The synthetic ``thread time'' for this thread.
  uint64_t Time;

  /// This thread's view of the synthetic ``process time'' for this process.
  uint64_t ProcessTime;

  /// List of all traced Functions, in order.
  std::vector<std::unique_ptr<RecordedFunction>> RecordedFunctions;

  /// Offset of all top-level traced Functions.
  std::vector<offset_uint> RecordedTopLevelFunctions;

  /// Stack of trace information for still-executing Functions.
  /// The back of the vector is the currently active Function.
  std::vector<TracedFunction> FunctionStack;

  /// Controls access to FunctionStack.
  mutable std::mutex FunctionStackMutex;

  /// Pointer to the trace information for the currently active Function, or
  /// nullptr if no Function is currently active.
  TracedFunction *ActiveFunction;

  /// Global memory lock owned by this thread.
  std::unique_lock<std::mutex> GlobalMemoryLock;

  /// Dynamic memory lock owned by this thread.
  std::unique_lock<std::mutex> DynamicMemoryLock;
  
  /// I/O streams lock owned by this thread.
  std::unique_lock<std::mutex> StreamsLock;
  
  /// DIR pointers lock owned by this thread.
  std::unique_lock<std::mutex> DirsLock;


  /// \name Current instruction information.
  /// @{
  
  /// Holds the new ProcessTime generated by the current Instruction, if any.
  seec::Maybe<uint64_t> CIProcessTime;
  
  /// Get the new ProcessTime generated by the current Instruction.
  uint64_t getCIProcessTime() {
    if (CIProcessTime.assigned())
      return CIProcessTime.get<0>();
    
    CIProcessTime = ProcessListener.getNewTime();
    return CIProcessTime.get<0>();
  }
  
  /// Clear current Instruction information.
  void clearCI() {
    CIProcessTime.reset();
    
    if (ActiveFunction)
      ActiveFunction->clearActiveInstruction();
  }
  
  /// @} (Current instruction information)
  
  
  /// \name Helper methods
  /// @{

  /// \brief Get the offset that a new FunctionRecord would be placed at.
  offset_uint getNewFunctionRecordOffset() {
    constexpr size_t SizeOfFunctionRecord
      = sizeof(uint32_t)        // index
      + (2*sizeof(offset_uint)) // event start, end
      + (2*sizeof(uint64_t))    // thread entered, exited
      + (1*sizeof(offset_uint)) // child list
      ;

    return sizeof(offset_uint) // For top-level function list offset
      + (RecordedFunctions.size() * SizeOfFunctionRecord); // For functions
  }

  /// \brief Synchronize this thread's view of the synthetic process time.
  void synchronizeProcessTime();
  
  /// \brief Check for any pending signals.
  ///
  void checkSignals();

  /// \brief Get (an estimate of) the remaining stack available.
  ///
  std::uintptr_t getRemainingStack() const;

public:  
  /// \brief Acquire the StreamsLock, if we don't have it already.
  ///
  void acquireStreamsLock() {
    if (!StreamsLock) {
      StreamsLock = ProcessListener.getStreamsLock();
    }
  }
  
  /// \brief Record that a stream opened.
  ///
  /// This will acquire the StreamsLock if we don't have it already.
  ///
  /// pre: Filename is a valid C string.
  /// pre: Mode is a valid C string.
  ///
  void recordStreamOpen(FILE *Stream,
                        char const *Filename,
                        char const *Mode);
  
  /// \brief Record a write to a stream.
  ///
  /// pre: Own the StreamsLock.
  ///
  void recordStreamWrite(FILE *Stream, llvm::ArrayRef<char> Data);
  
  /// \brief Record a write to a stream from recorded memory.
  ///
  /// This save some space in the trace because we can use the data from the
  /// recreated state rather than saving another copy into the trace data.
  ///
  /// pre: Own the StreamsLock.
  ///
  void recordStreamWriteFromMemory(FILE *Stream, MemoryArea Area);
  
  /// \brief Record that a stream closed.
  ///
  /// This will acquire the StreamsLock if we don't have it already.
  ///
  bool recordStreamClose(FILE *Stream);

  /// @} (Helper methods)
  
  
  /// \name DIR tracking.
  /// @{
  
  /// \brief Acquire the DirsLock, if we don't have it already.
  ///
  void acquireDirsLock() {
    if (!DirsLock) {
      DirsLock = ProcessListener.getDirsLock();
    }
  }
  
  /// \brief Access the TraceDirs using this thread's DirsLock.
  ///
  TraceDirs &getDirs() {
    acquireDirsLock();
    return ProcessListener.getDirs(DirsLock);
  }
  
  /// \brief Record that a DIR opened.
  ///
  /// This will acquire the DirsLock if we don't have it already.
  ///
  /// pre: Filename is a valid C string.
  ///
  void recordDirOpen(void const *TheDIR,
                     char const *Filename);
  
  /// \brief Record that a stream closed.
  ///
  /// This will acquire the StreamsLock if we don't have it already.
  ///
  bool recordDirClose(void const *TheDIR);
  
  /// @}


public:
  /// \name Dynamic memory
  /// @{
  
  /// \brief Acquire the DynamicMemoryLock, if we don't have it already.
  void acquireDynamicMemoryLock() {
    if (!DynamicMemoryLock) {
      DynamicMemoryLock = ProcessListener.lockDynamicMemory();
    }
  }

  /// \brief Write a malloc record and update the process' dynamic memory.
  ///
  /// pre: DynamicMemoryLock acquired by this object.
  ///
  void recordMalloc(uintptr_t Address, std::size_t Size);

  /// \brief Write a Realloc event and update the process' dynamic memory.
  ///
  void recordRealloc(uintptr_t const Address, std::size_t const NewSize);

  /// \brief Write a free record and update the process' dynamic memory.
  ///
  /// pre: DynamicMemoryLock acquired by this object.
  ///
  /// \return the DynamicAllocation that was freed.
  ///
  DynamicAllocation recordFree(uintptr_t Address);

  /// \brief Write a free record and update the process' dynamic memory. Clears
  ///        the freed area of memory.
  ///
  /// pre: DynamicMemoryLock acquired by this object.
  ///
  void recordFreeAndClear(uintptr_t Address);

  /// @} (Dynamic memory)


public:
  /// \name Memory states
  /// @{
  
  /// \brief Acquire the GlobalMemoryLock, if we don't have it already.
  /// At the moment, this is identical to acquireGlobalMemoryReadLock(), but we
  /// may change to a multiple readers / single writer design in the future.
  void acquireGlobalMemoryWriteLock() {
    if (!GlobalMemoryLock) {
      GlobalMemoryLock = ProcessListener.lockMemory();
    }
  }
  
  /// \brief Acquire the GlobalMemoryLock, if we don't have it already.
  /// At the moment, this is identical to acquireGlobalMemoryWriteLock(), but we
  /// may change to a multiple readers / single writer design in the future.
  void acquireGlobalMemoryReadLock() {
    if (!GlobalMemoryLock) {
      GlobalMemoryLock = ProcessListener.lockMemory();
    }
  }

  /// \brief Record an untyped update to memory.
  ///
  /// pre: GlobalMemoryLock acquired by this object.
  ///
  void recordUntypedState(char const *Data, std::size_t Size);

  /// \brief Record a typed update to memory.
  ///
  /// At the moment this simply defers to recordUntypedState.
  ///
  /// pre: GlobalMemoryLock acquired by this object.
  ///
  void recordTypedState(void const *Data, std::size_t Size, offset_uint Value);

  /// \brief Record a clear to a memory region.
  ///
  /// pre: GlobalMemoryLock acquired by this object.
  ///
  void recordStateClear(uintptr_t Address, std::size_t Size);

  /// \brief Unimplemented.
  ///
  void recordMemset();

  /// \brief Record a memmove (or memcpy) update to memory.
  ///
  /// pre: GlobalMemoryLock acquired by this object.
  ///
  void recordMemmove(uintptr_t Source, uintptr_t Destination, std::size_t Size);

  /// \brief Add a region of known, but unowned, memory.
  ///
  /// pre: GlobalMemoryLock acquired by this object.
  ///
  void addKnownMemoryRegion(uintptr_t Address,
                            std::size_t Length,
                            MemoryPermission Access);
  
  /// \brief Check if there is a region of known memory at Address.
  ///
  /// pre: GlobalMemoryLock acquired by this object.
  ///
  bool isKnownMemoryRegionAt(uintptr_t Address) const;
  
  /// \brief Check if there is a region of known memory covering the given area.
  ///
  /// pre: GlobalMemoryLock acquired by this object.
  ///
  bool isKnownMemoryRegionCovering(uintptr_t const Address,
                                   std::size_t const Length) const;

  /// \brief Remove the region of known memory starting at Address.
  ///
  /// pre: GlobalMemoryLock acquired by this object.
  ///
  bool removeKnownMemoryRegion(uintptr_t Address);

  /// @} (Memory states)


public:
  /// \brief Constructor.
  ///
  TraceThreadListener(TraceProcessListener &ProcessListener,
                      OutputStreamAllocator &StreamAllocator,
                      offset_uint const WithThreadEventLimit);

  /// \brief Destructor.
  ///
  ~TraceThreadListener();
  
  
  /// \name Trace writing control.
  /// @{
  
  /// \brief Check if tracing is enabled.
  ///
  bool traceEnabled() const { return OutputEnabled; }
  
  /// \brief Get the size of the trace's event stream.
  ///
  offset_uint traceEventSize() const;

  /// \brief Write out complete trace information.
  ///
  void traceWrite();
  
  /// \brief Flush all open trace streams.
  ///
  void traceFlush();
  
  /// \brief Close all open trace streams and disable future writes.
  ///
  void traceClose();
  
  /// \brief Open all used trace streams and enable future writes.
  ///
  void traceOpen();
  
  /// @} (Trace writing control.)


  /// \name Accessors
  /// @{

  /// \brief Get the \c TraceProcessListener that this thread belongs to.
  ///
  TraceProcessListener &getProcessListener() { return ProcessListener; }

  /// \brief Get the \c TraceProcessListener that this thread belongs to.
  ///
  TraceProcessListener const &getProcessListener() const {
    return ProcessListener;
  }
  
  /// \brief Access the synchronized exit supporter for this thread.
  ///
  SupportSynchronizedExit const &getSupportSynchronizedExit() {
    return SupportSyncExit;
  }

  /// \brief Get the unique ThreadID for this thread.
  ///
  uint32_t getThreadID() const { return ThreadID; }
  
  /// \brief Get access to the event output.
  ///
  EventWriter &getEventsOut() { return EventsOut; }

  /// \brief Get the \c llvm::DataLayout for the \c llvm::Module.
  ///
  llvm::DataLayout const &getDataLayout() const {
    return ProcessListener.getDataLayout();
  }

  /// \brief Get the run-time address of a GlobalVariable.
  /// \param GV the GlobalVariable.
  /// \return the run-time address of GV, or 0 if it is not known.
  ///
  uintptr_t getRuntimeAddress(llvm::GlobalVariable const *GV) const {
    return ProcessListener.getRuntimeAddress(GV);
  }

  /// \brief Get the run-time address of a Function.
  /// \param F the Function.
  /// \return the run-time address of F, or 0 if it is not known.
  ///
  uintptr_t getRuntimeAddress(llvm::Function const *F) const {
    return ProcessListener.getRuntimeAddress(F);
  }

  /// \brief Get trace information about the currently active Function.
  /// \return a pointer to the current active TracedFunction, or nullptr
  ///         if no Function is currently active.
  ///
  TracedFunction *getActiveFunction() {
    // return ActiveFunction.load();
    return ActiveFunction;
  }

  /// \brief Get trace information about the currently active Function.
  /// \return a const pointer to the current active TracedFunction, or nullptr
  ///         if no Function is currently active.
  ///
  TracedFunction const *getActiveFunction() const {
    // return ActiveFunction.load();
    return ActiveFunction;
  }

  /// \brief Get the current RuntimeValue associated with an Instruction.
  ///
  RuntimeValue const *getCurrentRuntimeValue(llvm::Instruction const *I) const;
  
  /// \brief Get the area occupied by the given Argument in the active function.
  ///
  seec::Maybe<seec::MemoryArea>
  getParamByValArea(llvm::Argument const *Arg) const;

  /// \brief Find the allocated range that owns an address, if it belongs to
  ///        this thread. This method is thread safe.
  ///
  seec::Maybe<MemoryArea>
  getContainingMemoryArea(uintptr_t Address) const {
    std::lock_guard<std::mutex> Lock(FunctionStackMutex);

    seec::Maybe<MemoryArea> Area;

    for (auto const &TracedFunc : FunctionStack) {
      Area = TracedFunc.getContainingMemoryArea(Address);
      if (Area.assigned()) {
        return Area;
      }
    }

    return Area;
  }

  /// @} (Accessors)


  /// \name Mutators
  /// @{
  
  /// \brief Increment the thread time and write a NewThreadTime event.
  ///
  /// This will also reset the process time associated with the current
  /// instruction.
  ///
  uint64_t incrementThreadTime() {
    EventsOut.write<seec::trace::EventType::NewThreadTime>(++Time);
    
    CIProcessTime.reset();
    
    return Time;
  }

  /// \brief Handle a run-time error.
  /// At this time, a run-time error is handled by writing it to the trace, and
  /// then terminating execution with a synchronized exit.
  /// \param Error the run-time error.
  /// \param Severity the severity of the error.
  /// \param PreInstructionIndex the index of the Instruction that would cause
  ///                            this error.
  void handleRunError(seec::runtime_errors::RunError const &Error,
                      RunErrorSeverity Severity,
                      seec::Maybe<uint32_t> PreInstructionIndex
                        = seec::Maybe<uint32_t>());

  /// @} (Mutators)


  /// \name Shadow stack.
  /// @{

  /// \brief Push a shim function onto the shadow stack.
  ///
  void pushShimFunction();

  /// \brief Pop a shim function from the shadow stack.
  ///
  void popShimFunction();

  /// @} (Shadow stack.)


  /// \name Thread Listener Notifications
  /// @{

  void enterNotification();

  void exitNotification();

  void exitPreNotification();

  void exitPostNotification();

  void notifyFunctionBegin(uint32_t Index, llvm::Function const *F);

  void notifyFunctionEnd(uint32_t const Index,
                         llvm::Function const *F,
                         uint32_t const TerminatorIndex,
                         llvm::Instruction const *Terminator);
  
  /// \brief Notify of a byval argument.
  void notifyArgumentByVal(uint32_t Index, llvm::Argument const *Arg,
                           void const *Address);
  
  /// \brief Receive the contents of argc and argv.
  void notifyArgs(uint64_t ArgC, char **ArgV);
  
private:
  void setupEnvironTable(char **Environ);

public:
  /// \brief Receive the contents of envp.
  void notifyEnv(char **EnvP);

  void notifyPreCall(uint32_t Index, llvm::CallInst const *Call,
                     void const *Address);

  void notifyPostCall(uint32_t Index, llvm::CallInst const *Call,
                      void const *Address);

  void notifyPreCallIntrinsic(uint32_t Index, llvm::CallInst const *Call);

  void notifyPostCallIntrinsic(uint32_t Index, llvm::CallInst const *Call);

  void notifyPreAlloca(uint32_t const Index,
                       llvm::AllocaInst const &Alloca,
                       uint64_t const ElemSize,
                       uint64_t const ElemCount);

  void notifyPreLoad(uint32_t Index,
                     llvm::LoadInst const *Load,
                     void const *Address,
                     std::size_t Size);

  void notifyPostLoad(uint32_t Index,
                      llvm::LoadInst const *Load,
                      void const *Address,
                      std::size_t Size);

  void notifyPreStore(uint32_t Index,
                      llvm::StoreInst const *Store,
                      void const *Address,
                      std::size_t Size);

  void notifyPostStore(uint32_t Index,
                       llvm::StoreInst const *Store,
                       void const *Address,
                       std::size_t Size);

  void notifyPreDivide(uint32_t Index,
                       llvm::BinaryOperator const *Instruction);

  void notifyValue(uint32_t const Index,
                   llvm::Instruction const * const Instruction);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   void *Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   uint64_t Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   uint32_t Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   uint16_t Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   uint8_t Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   float Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   double Value);
  
  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   long double Value);

  /// @} (Thread Listener Notifications)
  
  
  /// \name Detect Calls - ctype.h
  /// @{
  
  void postLINUX__ctype_b_loc(llvm::CallInst const *Call, uint32_t Index);
  
  void postLINUX__ctype_tolower_loc(llvm::CallInst const *Call, uint32_t Index);
  
  void postLINUX__ctype_toupper_loc(llvm::CallInst const *Call, uint32_t Index);
  
  /// @}


  /// \name Detect Calls - stdio.h file access
  /// @{
  
  // fopen
  void preCfopen(llvm::CallInst const *Call, uint32_t Index,
                 char const *Filename, char const *Mode);
  void postCfopen(llvm::CallInst const *Call, uint32_t Index,
                  char const *Filename, char const *Mode);
  
  // freopen
  void preCfreopen(llvm::CallInst const *Call, uint32_t Index,
                   char const *Filename, char const *Mode, FILE *Stream);
  void postCfreopen(llvm::CallInst const *Call, uint32_t Index,
                    char const *Filename, char const *Mode, FILE *Stream);
  
  // fclose
  void preCfclose(llvm::CallInst const *Call, uint32_t Index, FILE *Stream);
  void postCfclose(llvm::CallInst const *Call, uint32_t Index, FILE *Stream);
  
  // fflush
  void preCfflush(llvm::CallInst const *Call, uint32_t Index, FILE *Stream);
  
  // fwide
  void preCfwide(llvm::CallInst const *Call, uint32_t Index, FILE *Stream,
                 int Mode);
  
  /// @}
  
  
  /// \name Detect Calls - stdio.h direct input/output.
  /// @{
  
  /// @}
  
  
  /// \name Detect Calls - stdio.h unformatted input/output.
  /// @{
  
  // fgetc
  void preCfgetc(llvm::CallInst const *Call, uint32_t Index, FILE *Stream);
  
  // fgets
  void preCfgets(llvm::CallInst const *Call, uint32_t Index, char *Str,
                 int Count, FILE *Stream);
  void postCfgets(llvm::CallInst const *Call, uint32_t Index, char *Str,
                  int Count, FILE *Stream);
  
  // fputc
  void preCfputc(llvm::CallInst const *Call, uint32_t Index, int Ch,
                 FILE *Stream);
  void postCfputc(llvm::CallInst const *Call, uint32_t Index, int Ch,
                  FILE *Stream);
  
  // fputs
  void preCfputs(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                 FILE *Stream);
  void postCfputs(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                  FILE *Stream);
  
  // getchar
  void preCgetchar(llvm::CallInst const *Call, uint32_t Index);
  
  // gets - should never be used.
  
  // putchar
  void preCputchar(llvm::CallInst const *Call, uint32_t Index, int Ch);
  void postCputchar(llvm::CallInst const *Call, uint32_t Index, int Ch);
  
  // puts
  void preCputs(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  void postCputs(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  
  // ungetc
  void preCungetc(llvm::CallInst const *Call, uint32_t Index, int Ch,
                  FILE *Stream);
  
  /// @}
  
  
  /// \name Detect Calls - stdio.h formatted input/output.
  /// @{
  
  // snprintf
  void preCsnprintf(llvm::CallInst const *Call, uint32_t Index, char *Buffer,
                    std::size_t BufSize, char const *Str,
                    detect_calls::VarArgList<TraceThreadListener> const &Args);
  void postCsnprintf(llvm::CallInst const *Call, uint32_t Index, char *Buffer,
                     std::size_t BufSize, char const *Str,
                     detect_calls::VarArgList<TraceThreadListener> const &Args);
  
  /// @}
  
  
  /// \name Detect Calls - stdlib.h string
  /// @{
  
  void preCatof(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  
  void preCatoi(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  void preCatol(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  void preCatoll(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  
  // strtol
  void preCstrtol(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                  char **EndPtr, int Base);
  void postCstrtol(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr, int Base);
  
  // strtoll
  void preCstrtoll(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr, int Base);
  void postCstrtoll(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                    char **EndPtr, int Base);

  // strtoul
  void preCstrtoul(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr, int Base);
  void postCstrtoul(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                    char **EndPtr, int Base);
  
  // strtoull
  void preCstrtoull(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                    char **EndPtr, int Base);
  void postCstrtoull(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                     char **EndPtr, int Base);

  // strtof
  void preCstrtof(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                  char **EndPtr);
  void postCstrtof(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr);
  
  // strtod
  void preCstrtod(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                  char **EndPtr);
  void postCstrtod(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr);
  
  // strtold
  void preCstrtold(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr);
  void postCstrtold(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                    char **EndPtr);

  // strtoimax
  void preCstrtoimax(llvm::CallInst const *Call, uint32_t Index,
                     char const *Str, char **EndPtr);
  void postCstrtoimax(llvm::CallInst const *Call, uint32_t Index,
                      char const *Str, char **EndPtr);
  
  // strtoumax
  void preCstrtoumax(llvm::CallInst const *Call, uint32_t Index,
                     char const *Str, char **EndPtr);
  void postCstrtoumax(llvm::CallInst const *Call, uint32_t Index,
                      char const *Str, char **EndPtr);

  /// @}
  
  
  /// \name Detect Calls - stdlib.h memory
  /// @{
  
  void preCcalloc(llvm::CallInst const *Call, uint32_t Index, size_t Num,
                  size_t Size);

  void postCcalloc(llvm::CallInst const *Call, uint32_t Index, size_t Num,
                   size_t Size);

  void preCfree(llvm::CallInst const *Call, uint32_t Index, void *Address);

  void postCfree(llvm::CallInst const *Call, uint32_t Index, void *Address);

  void preCmalloc(llvm::CallInst const *Call, uint32_t Index, size_t Size);

  void postCmalloc(llvm::CallInst const *Call, uint32_t Index, size_t Size);

  void preCrealloc(llvm::CallInst const *Call, uint32_t Index, void *Address,
                   size_t Size);

  void postCrealloc(llvm::CallInst const *Call, uint32_t Index, void *Address,
                    size_t Size);
  
  /// @}
  
  
  /// \name Detect Calls - stdlib.h environment
  /// @{
  
  void preCgetenv(llvm::CallInst const *Call, uint32_t Index,
                  char const *Name);
  
  void postCgetenv(llvm::CallInst const *Call, uint32_t Index,
                   char const *Name);
  
  void preCsystem(llvm::CallInst const *Call, uint32_t Index,
                  char const *Command);
  
  /// @}
  
  
  /// \name Detect Calls - string.h
  /// @{

  void preCmemchr(llvm::CallInst const *Call, uint32_t Index,
                  void const *Ptr, int Value, size_t Num);

  void postCmemchr(llvm::CallInst const *Call, uint32_t Index,
                   void const *Ptr, int Value, size_t Num);

  void preCmemcmp(llvm::CallInst const *Call, uint32_t Index,
                  void const *Address1, void const *Address2, size_t Size);

  void postCmemcmp(llvm::CallInst const *Call, uint32_t Index,
                   void const *Address1, void const *Address2, size_t Size);

  void preCmemcpy(llvm::CallInst const *Call, uint32_t Index,
                  void *Destination, void const *Source, size_t Size);

  void postCmemcpy(llvm::CallInst const *Call, uint32_t Index,
                   void *Destination, void const *Source, size_t Size);

  void preCmemmove(llvm::CallInst const *Call, uint32_t Index,
                   void *Destination, void const *Source, size_t Size);

  void postCmemmove(llvm::CallInst const *Call, uint32_t Index,
                    void *Destination, void const *Source, size_t Size);

  void preCmemset(llvm::CallInst const *Call, uint32_t Index,
                  void *Destination, int Value, size_t Size);

  void postCmemset(llvm::CallInst const *Call, uint32_t Index,
                   void *Destination, int Value, size_t Size);

  void preCstrcat(llvm::CallInst const *Call, uint32_t Index,
                  char *Destination, char const *Source);

  void postCstrcat(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source);

  void preCstrchr(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str, int Character);

  void postCstrchr(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str, int Character);

  void preCstrcmp(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str1, char const *Str2);

  void preCstrcoll(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2);

  void preCstrcpy(llvm::CallInst const *Call, uint32_t Index,
                  char *Destination, char const *Source);

  void postCstrcpy(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source);

  void preCstrcspn(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2);

  void preCstrerror(llvm::CallInst const *Call, uint32_t Index, int Errnum);
  
  void postCstrerror(llvm::CallInst const *Call, uint32_t Index, int Errnum);

  void preCstrlen(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str);

  void preCstrncat(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source, size_t Size);

  void postCstrncat(llvm::CallInst const *Call, uint32_t Index,
                    char *Destination, char const *Source, size_t Size);

  void preCstrncmp(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2, size_t Num);

  void preCstrncpy(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source, size_t Size);

  void postCstrncpy(llvm::CallInst const *Call, uint32_t Index,
                    char *Destination, char const *Source, size_t Size);

  void preCstrpbrk(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2);

  void postCstrpbrk(llvm::CallInst const *Call, uint32_t Index,
                    char const *Str1, char const *Str2);

  void preCstrrchr(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str, int Character);

  void postCstrrchr(llvm::CallInst const *Call, uint32_t Index,
                    char const *Str, int Character);

  void preCstrspn(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str1, char const *Str2);

  void preCstrstr(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str1, char const *Str2);

  void postCstrstr(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2);

  void preCstrxfrm(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source, size_t Num);
  
  void postCstrxfrm(llvm::CallInst const *Call, uint32_t Index,
                    char *Destination, char const *Source, size_t Num);
  
  /// @}
};


} // namespace trace (in seec)

} // namespace seec

#endif // SEEC_TRACE_TRACETHREADLISTENER_HPP
