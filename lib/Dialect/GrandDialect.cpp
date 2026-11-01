#include "grand/Dialect/GrandDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/Interfaces/FunctionImplementation.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace grand;

#include "grand/Dialect/GrandDialect.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "grand/Dialect/GrandTypes.cpp.inc"

#define GET_OP_CLASSES
#include "grand/Dialect/GrandOps.cpp.inc"

void GrandDialect::initialize() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "grand/Dialect/GrandTypes.cpp.inc"
      >();
  addOperations<
#define GET_OP_LIST
#include "grand/Dialect/GrandOps.cpp.inc"
      >();
}

// Checks that a value-producing region ends in region_result of `type`.
static LogicalResult verifyResultRegion(Operation *op, Region &region, Type type,
                                        StringRef name) {
  auto result = dyn_cast_or_null<RegionResultOp>(
      region.empty() || region.front().empty() ? nullptr : &region.front().back());
  if (!result)
    return op->emitOpError() << name << " region must end in region_result";
  if (result.getValue().getType() != type)
    return op->emitOpError() << name << " region must produce " << type;
  return success();
}

static bool isInsideLoop(Operation *op) {
  // The cond, init and step regions cannot hold statements in any source
  // language, so any enclosing loop region qualifies.
  return op->getParentOfType<WhileOp>() || op->getParentOfType<DoWhileOp>() ||
         op->getParentOfType<ForOp>();
}

static LogicalResult verifyRefStores(Operation *op, Value ref, Type valueType) {
  if (cast<RefType>(ref.getType()).getPointee() != valueType)
    return op->emitOpError("value type does not match reference type");
  return success();
}

ParseResult FuncOp::parse(OpAsmParser &parser, OperationState &result) {
  auto buildFuncType = [](Builder &builder, ArrayRef<Type> argTypes,
                          ArrayRef<Type> results, function_interface_impl::VariadicFlag,
                          std::string &) {
    return builder.getFunctionType(argTypes, results);
  };
  return function_interface_impl::parseFunctionOp(
      parser, result, /*allowVariadic=*/false,
      getFunctionTypeAttrName(result.name), buildFuncType,
      getArgAttrsAttrName(result.name), getResAttrsAttrName(result.name));
}

void FuncOp::print(OpAsmPrinter &printer) {
  function_interface_impl::printFunctionOp(
      printer, *this, /*isVariadic=*/false, getFunctionTypeAttrName(),
      getArgAttrsAttrName(), getResAttrsAttrName());
}

LogicalResult FuncOp::verify() {
  if (getFunctionType().getNumResults() > 1)
    return emitOpError("may return at most one value");
  return success();
}

LogicalResult GlobalOp::verify() {
  if (getInit().empty())
    return success();
  return verifyResultRegion(*this, getInit(), getType(), "init");
}

LogicalResult ReturnOp::verify() {
  auto func = (*this)->getParentOfType<FuncOp>();
  if (!func)
    return emitOpError("must be inside a function");
  auto expected = func.getFunctionType().getResults();
  if (expected.empty() != !getValue())
    return emitOpError("value does not match the function's return type");
  if (getValue() && getValue().getType() != expected.front())
    return emitOpError("returns ") << getValue().getType() << ", expected " << expected.front();
  return success();
}

LogicalResult VarOp::verify() {
  Type pointee = getRef().getType().getPointee();
  if (getInit() && getInit().getType() != pointee)
    return emitOpError("initialiser type does not match variable type");
  if (getIsParam() && !isa_and_nonnull<BlockArgument>(getInit()))
    return emitOpError("parameter must be initialised from a block argument");
  return success();
}

LogicalResult LoadOp::verify() {
  return verifyRefStores(*this, getRef(), getResult().getType());
}

LogicalResult StoreOp::verify() {
  return verifyRefStores(*this, getRef(), getValue().getType());
}

LogicalResult CompoundAssignOp::verify() {
  static const llvm::StringSet<> kinds = {"add", "sub",    "mul",    "div", "rem",
                                          "bit_and", "bit_or", "bit_xor", "shl", "shr",
                                          "ushr", "concat"};
  if (!kinds.contains(getKind()))
    return emitOpError("unknown kind '") << getKind() << "'";
  return verifyRefStores(*this, getRef(), getValue().getType());
}

LogicalResult IncDecOp::verify() {
  if (getDelta() != 1 && getDelta() != -1)
    return emitOpError("delta must be 1 or -1");
  if (cast<RefType>(getRef().getType()).getPointee() != getResult().getType())
    return emitOpError("result type must match the reference type");
  return success();
}

LogicalResult CmpOp::verify() {
  static const llvm::StringSet<> preds = {"eq", "ne", "lt", "le", "gt", "ge"};
  if (!preds.contains(getPred()))
    return emitOpError("unknown predicate '") << getPred() << "'";
  if (isa<StringType>(getLhs().getType()))
    return emitOpError("strings are compared with str_eq");
  if (isa<BoolType>(getLhs().getType()) && getPred() != "eq" && getPred() != "ne")
    return emitOpError("booleans support only eq and ne");
  return success();
}

template <typename OpT>
static LogicalResult verifyLogical(OpT op) {
  return verifyResultRegion(op, op.getRhs(), op.getResult().getType(), "rhs");
}

LogicalResult LogicalAndOp::verify() { return verifyLogical(*this); }
LogicalResult LogicalOrOp::verify() { return verifyLogical(*this); }

LogicalResult TernaryOp::verify() {
  Type type = getResult().getType();
  if (failed(verifyResultRegion(*this, getThenRegion(), type, "then")))
    return failure();
  return verifyResultRegion(*this, getElseRegion(), type, "else");
}

LogicalResult WhileOp::verify() {
  return verifyResultRegion(*this, getCondRegion(), BoolType::get(getContext()), "cond");
}

LogicalResult DoWhileOp::verify() {
  return verifyResultRegion(*this, getCondRegion(), BoolType::get(getContext()), "cond");
}

ForLoopOp ForOp::getLoop() { return dyn_cast<ForLoopOp>(getInitRegion().front().back()); }

LogicalResult ForOp::verify() {
  if (getInitRegion().front().empty() || !getLoop())
    return emitOpError("region must end in for_loop");
  return success();
}

LogicalResult ForLoopOp::verify() {
  if (getCondRegion().empty() || getCondRegion().front().empty())
    return success();
  return verifyResultRegion(*this, getCondRegion(), BoolType::get(getContext()), "cond");
}

LogicalResult BreakOp::verify() {
  return isInsideLoop(*this) ? success() : emitOpError("must be inside a loop");
}

LogicalResult ContinueOp::verify() {
  return isInsideLoop(*this) ? success() : emitOpError("must be inside a loop");
}

LogicalResult ToStringOp::verify() {
  auto style = getBoolStyle();
  if (isa<BoolType>(getValue().getType())) {
    if (!style || (*style != "lower" && *style != "pascal"))
      return emitOpError("bool operand requires bool_style `lower` or `pascal`");
  } else if (style) {
    return emitOpError("bool_style is only valid on bool operands");
  }
  return success();
}
