//===- include/seec/RuntimeErrors/ArgumentTypes.hpp -----------------------===//
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

#ifndef SEEC_RUNTIMEERRORS_ARGUMENTTYPES_HPP
#define SEEC_RUNTIMEERRORS_ARGUMENTTYPES_HPP

#include "seec/RuntimeErrors/FormatSelects.hpp"

#include "llvm/Support/Casting.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <memory>

namespace seec {

namespace runtime_errors {

/// Enumeration of all basic runtime error argument types.
enum class ArgType : uint8_t {
  None = 0,
#define SEEC_RUNERR_ARG(TYPE) TYPE,
#include "seec/RuntimeErrors/ArgumentTypes.def"
};

char const *describe(ArgType Type);

/// \brief Base class for all runtime error arguments.
///
/// This class can not be constructed directly, but holds functionality common
/// to all arguments.
class Arg {
  ArgType Type;

  /// \brief Clone this Arg.
  ///
  virtual std::unique_ptr<Arg> cloneImpl() const =0;

protected:
  Arg(ArgType Type)
  : Type(Type)
  {}

  // Arg objects should not be copied directly, only as part of copying a
  // subclass.
  Arg(Arg const &Other) = default;
  Arg &operator=(Arg const &RHS) = default;

public:
  virtual ~Arg() = 0;

  /// Get the type of this argument.
  ArgType type() const { return Type; }

  /// Serialize data.
  virtual uint64_t data() const = 0;

  /// Deserialize from type and data.
  static std::unique_ptr<Arg> deserialize(uint8_t Type, uint64_t Data);

  /// Support LLVM's dynamic casting.
  static bool classof(Arg const *A) { return true; }

  /// \brief Clone this Arg.
  ///
  std::unique_ptr<Arg> clone() const { return cloneImpl(); }
};

/// \brief An argument that holds a runtime address.
///
class ArgAddress : public Arg {
  uintptr_t Address;

  /// \brief Clone this Arg.
  ///
  std::unique_ptr<Arg> cloneImpl() const override {
    return std::unique_ptr<Arg>(new ArgAddress(*this));
  }

public:
  ArgAddress(uintptr_t Address)
  : Arg(ArgType::Address),
    Address(Address)
  {}

  ArgAddress(ArgAddress const &Other) = default;

  virtual ~ArgAddress();

  virtual uint64_t data() const override { return Address; }

  /// \brief Deserialize from data.
  static std::unique_ptr<Arg> deserialize(uint64_t Data) {
    return std::unique_ptr<Arg>(new ArgAddress(Data));
  }

  /// Support LLVM's dynamic casting.
  /// \return true.
  static bool classof(ArgAddress const *A) { return true; }

  /// Support LLVM's dynamic casting.
  /// \return true iff *A is an ArgAddress.
  static bool classof(Arg const *A) { return A->type() == ArgType::Address; }

  uintptr_t address() const { return Address; }
};

/// \brief An argument that represents a runtime object.
///
class ArgObject : public Arg {

  /// \brief Clone this Arg.
  ///
  std::unique_ptr<Arg> cloneImpl() const override {
    return std::unique_ptr<Arg>(new ArgObject(*this));
  }

public:
  ArgObject()
  : Arg(ArgType::Object)
  {}

  ArgObject(ArgObject const &Other) = default;

  virtual ~ArgObject();

  virtual uint64_t data() const override { return 0; }

  /// \brief Deserialize from data.
  static std::unique_ptr<Arg> deserialize(uint64_t Data) {
    return std::unique_ptr<Arg>(new ArgObject());
  }

  /// Support LLVM's dynamic casting.
  /// \return true.
  static bool classof(ArgObject const *A) { return true; }

  /// Support LLVM's dynamic casting.
  /// \return true iff *A is an ArgObject.
  static bool classof(Arg const *A) { return A->type() == ArgType::Object; }
};

/// \brief Base class for all ArgSelect objects.
///
class ArgSelectBase : public Arg {
public:
  ArgSelectBase()
  : Arg(ArgType::SelectBase)
  {}

  virtual ~ArgSelectBase();

  virtual uint64_t data() const override {
    uint64_t Data = static_cast<uint32_t>(getSelectID());
    Data <<= 32;
    Data |= static_cast<uint32_t>(getRawItemValue());
    return Data;
  }

  /// \brief Deserialize from data.
  static std::unique_ptr<Arg> deserialize(uint64_t Data);

  /// Support LLVM's dynamic casting.
  /// \return true.
  static bool classof(ArgSelectBase const *A) { return true; }

  /// Support LLVM's dynamic casting.
  /// \return true iff A->type() is ArgType::SelectBase.
  static bool classof(Arg const *A) { return A->type() == ArgType::SelectBase; }

  /// Get the address of the format_selects::getCString() overload for the
  /// select type of this ArgSelect object. Used by ArgSelect to support LLVM's
  /// dynamic casting.
  virtual uintptr_t getCStringAddress() const = 0;

  virtual format_selects::SelectID getSelectID() const = 0;

  virtual uint32_t getRawItemValue() const = 0;
};

/// \brief An argument that represents a selection.
///
template<typename SelectType>
class ArgSelect : public ArgSelectBase {
  SelectType Item;

  /// \brief Clone this Arg.
  ///
  std::unique_ptr<Arg> cloneImpl() const override {
    return std::unique_ptr<Arg>(new ArgSelect(*this));
  }

protected:
  static uintptr_t getCStringAddressImpl() {
    char const *(*func)(SelectType) = format_selects::getCString;
    return (uintptr_t) func;
  }

  virtual uintptr_t getCStringAddress() const override {
    return getCStringAddressImpl();
  }

public:
  ArgSelect(SelectType Item)
  : ArgSelectBase(),
    Item(Item)
  {}

  ArgSelect(ArgSelect<SelectType> const &Other) = default;

  virtual ~ArgSelect() {}

  static bool classof(ArgSelect<SelectType> const *A) { return true; }

  static bool classof(Arg const *A) {
    if (A->type() != ArgType::SelectBase)
      return false;

    ArgSelectBase const *Base = llvm::cast<ArgSelectBase>(A);
    return Base->getCStringAddress() == getCStringAddressImpl();
  }

  virtual format_selects::SelectID getSelectID() const override {
    return format_selects::GetSelectID<SelectType>::value();
  }

  virtual uint32_t getRawItemValue() const override {
    return static_cast<uint32_t>(Item);
  }
};

///
std::unique_ptr<Arg> createArgSelect(format_selects::SelectID Select,
                                     uint32_t Item);

/// \brief An argument that holds a size.
///
class ArgSize : public Arg {
  uint64_t Size;

  /// \brief Clone this Arg.
  ///
  std::unique_ptr<Arg> cloneImpl() const override {
    return std::unique_ptr<Arg>(new ArgSize(*this));
  }

public:
  ArgSize(uint64_t Size)
  : Arg(ArgType::Size),
    Size(Size)
  {}

  ArgSize(ArgSize const &Other) = default;

  virtual ~ArgSize();

  virtual uint64_t data() const override { return Size; }

  /// \brief Deserialize from data.
  static std::unique_ptr<Arg> deserialize(uint64_t Data) {
    return std::unique_ptr<Arg>(new ArgSize(Data));
  }

  /// Support LLVM's dynamic casting.
  /// \return true.
  static bool classof(ArgSize const *A) { return true; }

  /// Support LLVM's dynamic casting.
  /// \return true iff *A is an ArgSize.
  static bool classof(Arg const *A) { return A->type() == ArgType::Size; }

  uint64_t size() const { return Size; }
};

/// \brief An argument that holds the index of an operand of an llvm::User.
///
class ArgOperand : public Arg {
  uint64_t Index;

  /// \brief Clone this Arg.
  ///
  std::unique_ptr<Arg> cloneImpl() const override {
    return std::unique_ptr<Arg>(new ArgOperand(*this));
  }

public:
  ArgOperand(uint64_t Index)
  : Arg(ArgType::Operand),
    Index(Index)
  {}

  ArgOperand(ArgOperand const &Other) = default;

  virtual ~ArgOperand();

  virtual uint64_t data() const override { return Index; }

  /// \brief Deserialize from data.
  static std::unique_ptr<Arg> deserialize(uint64_t Data) {
    return std::unique_ptr<Arg>(new ArgOperand(Data));
  }

  /// Support LLVM's dynamic casting.
  /// \return true.
  static bool classof(ArgOperand const *A) { return true; }

  /// Support LLVM's dynamic casting.
  /// \return true iff *A is an ArgOperand.
  static bool classof(Arg const *A) { return A->type() == ArgType::Operand; }

  uint64_t index() const { return Index; }
};

/// \brief An argument that holds the index of a parameter to a function.
///
class ArgParameter : public Arg {
  uint64_t Index;

  /// \brief Clone this Arg.
  ///
  std::unique_ptr<Arg> cloneImpl() const override {
    return std::unique_ptr<Arg>(new ArgParameter(*this));
  }

public:
  ArgParameter(uint64_t Index)
  : Arg(ArgType::Parameter),
    Index(Index)
  {}

  ArgParameter(ArgParameter const &Other) = default;

  virtual ~ArgParameter();

  virtual uint64_t data() const override { return Index; }

  /// \brief Deserialize from data.
  static std::unique_ptr<Arg> deserialize(uint64_t Data) {
    return std::unique_ptr<Arg>(new ArgParameter(Data));
  }

  /// Support LLVM's dynamic casting.
  /// \return true.
  static bool classof(ArgParameter const *A) { return true; }

  /// Support LLVM's dynamic casting.
  /// \return true iff *A is an ArgParameter.
  static bool classof(Arg const *A) { return A->type() == ArgType::Parameter; }

  uint64_t index() const { return Index; }
};

/// \brief An argument that holds a single character.
///
class ArgCharacter : public Arg {
  char Character;

  /// \brief Clone this Arg.
  ///
  std::unique_ptr<Arg> cloneImpl() const override {
    return std::unique_ptr<Arg>(new ArgCharacter(*this));
  }

public:
  ArgCharacter(char Character)
  : Arg(ArgType::Character),
    Character(Character)
  {}

  ArgCharacter(ArgCharacter const &Other) = default;

  virtual ~ArgCharacter();

  virtual uint64_t data() const override { return Character; }

  /// \brief Deserialize from data.
  static std::unique_ptr<Arg> deserialize(uint64_t Data) {
    return std::unique_ptr<Arg>(new ArgCharacter(static_cast<char>(Data)));
  }

  /// Support LLVM's dynamic casting.
  /// \return true.
  static bool classof(ArgCharacter const *A) { return true; }

  /// Support LLVM's dynamic casting.
  /// \return true iff *A is an ArgCharacter.
  static bool classof(Arg const *A) { return A->type() == ArgType::Character; }

  char character() const { return Character; }
};

} // namespace runtime_errors (in seec)

} // namespace seec

#endif // SEEC_RUNTIMEERRORS_ARGUMENTTYPES_HPP
