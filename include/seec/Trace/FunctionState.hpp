//===- include/seec/Trace/FunctionState.hpp ------------------------- C++ -===//
//
//
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef SEEC_TRACE_FUNCTIONSTATE_HPP
#define SEEC_TRACE_FUNCTIONSTATE_HPP

#include "seec/Trace/RuntimeValue.hpp"
#include "seec/Trace/TraceReader.hpp"
#include "seec/Util/Maybe.hpp"
#include "seec/Util/ModuleIndex.hpp"

#include <cstdint>
#include <vector>

namespace llvm {

class raw_ostream;

}

namespace seec {

namespace trace {


/// \brief Represents the result of a single alloca instruction.
///
class AllocaState {
  /// Index of the llvm::AllocaInst.
  uint32_t InstructionIndex;
  
  /// Runtime address for this allocation.
  uint64_t Address;
  
  /// Size of the element type that this allocation was for.
  uint64_t ElementSize;
  
  /// Number of elements that space was allocated for.
  uint64_t ElementCount;
  
public:
  /// Construct a new AllocaState with the specified values.
  AllocaState(uint32_t InstructionIndex,
              uint64_t Address,
              uint64_t ElementSize,
              uint64_t ElementCount)
  : InstructionIndex(InstructionIndex),
    Address(Address),
    ElementSize(ElementSize),
    ElementCount(ElementCount)
  {}
  
  /// Get the index of the llvm::AllocaInst that produced this state.
  uint32_t getInstructionIndex() const { return InstructionIndex; }
  
  /// Get the runtime address for this allocation.
  uint64_t getAddress() const { return Address; }
  
  /// Get the size of the element type that this allocation was for.
  uint64_t getElementSize() const { return ElementSize; }
  
  /// Get the number of elements that space was allocated for.
  uint64_t getElementCount() const { return ElementCount; }
  
  /// Get the total size of this allocation.
  uint64_t getTotalSize() const { return ElementSize * ElementCount; }
};


/// \brief State of a function invocation at a specific point in time.
///
class FunctionState {
  /// Index of the llvm::Function in the llvm::Module.
  uint32_t Index;
  
  /// Function trace record.
  FunctionTrace Trace;
  
  /// Index of the currently active llvm::Instruction.
  seec::util::Maybe<uint32_t> ActiveInstruction;
  
  /// Runtime values indexed by Instruction index.
  std::vector<RuntimeValue> InstructionValues;
  
  /// All active stack allocations for this function.
  std::vector<AllocaState> Allocas;
  
public:
  /// \brief Constructor.
  /// \param Index Index of this llvm::Function in the llvm::Module.
  /// \param Function Indexed view of the llvm::Function.
  FunctionState(uint32_t Index,
                FunctionIndex const &Function,
                FunctionTrace Trace)
  : Index(Index),
    Trace(Trace),
    ActiveInstruction(),
    InstructionValues(Function.getInstructionCount()),
    Allocas()
  {}
  
  
  /// \name Accessors.
  /// @{
  
  /// \brief Get the index of the llvm::Function in the llvm::Module.
  uint32_t getIndex() const { return Index; }
  
  /// \brief Get the function trace record for this function invocation.
  FunctionTrace getTrace() const { return Trace; }
  
  /// \brief Get the number of llvm::Instructions in this llvm::Function.
  std::size_t getInstructionCount() const { return InstructionValues.size(); }
  
  /// \brief Get the index of the active llvm::Instruction, if there is one.
  seec::util::Maybe<uint32_t> getActiveInstruction() const {
    return ActiveInstruction;
  }
  
  /// @} (Accessors)
  
  
  /// \name Mutators.
  /// @{
  
  /// \brief Set the index of the active llvm::Instruction.
  /// \param Index the index for the new active llvm::Instruction.
  void setActiveInstruction(uint32_t Index) {
    ActiveInstruction = Index;
  }
  
  /// \brief Clear the currently active llvm::Instruction.
  void clearActiveInstruction() {
    ActiveInstruction.reset();
  }
  
  /// @} (Mutators)
  
  
  /// \name Runtime values.
  /// @{
  
  /// \brief Get the current runtime value for an llvm::Instruction.
  /// \param Index the index of the llvm::Instruction in this llvm::Function.
  RuntimeValue &getRuntimeValue(uint32_t Index) {
    return InstructionValues[Index];
  }
  
  /// \brief Get the current runtime value for an llvm::Instruction.
  /// \param Index the index of the llvm::Instruction in this llvm::Function.
  RuntimeValue const &getRuntimeValue(uint32_t Index) const {
    return InstructionValues[Index];
  }
  
  /// @}
  
  
  /// \name Allocas.
  /// @{
  
  /// \brief Get the active stack allocations for this function.
  decltype(Allocas) &getAllocas() { return Allocas; }
  
  /// \brief Get the active stack allocations for this function.
  decltype(Allocas) const &getAllocas() const { return Allocas; }
  
  /// @}
};

/// Print a textual description of a FunctionState.
llvm::raw_ostream &operator<<(llvm::raw_ostream &Out,
                              FunctionState const &State);

} // namespace trace (in seec)

} // namespace seec

#endif // SEEC_TRACE_FUNCTIONSTATE_HPP
