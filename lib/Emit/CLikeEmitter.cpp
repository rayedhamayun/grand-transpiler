#include "grand/Emit/CLikeEmitter.h"

#include "grand/Core/Transpiler.h"

#include "llvm/ADT/TypeSwitch.h"

#include <cstdio>

using namespace mlir;
using namespace grand;

std::string CLikeEmitter::emit(ModuleOp module, const EmitOptions &options) {
  out.clear();
  indent = 0;
  className = options.className;
  if (className.empty())
    if (auto hint = module->getAttrOfType<StringAttr>(kClassNameAttr))
      className = hint.str();
  if (className.empty())
    className = "Main";

  symbolNames.clear();
  for (auto func : module.getOps<FuncOp>())
    symbolNames[func.getSymName()] = nameOf(func, func.getSymName());
  for (auto global : module.getOps<GlobalOp>())
    symbolNames[global.getSymName()] = nameOf(global, global.getSymName());

  emitModule(module);
  return out;
}

void CLikeEmitter::line(const std::string &text) {
  out.append(indent * 4, ' ');
  out += text;
  out += '\n';
}

void CLikeEmitter::blank() { out += '\n'; }

void CLikeEmitter::openBlock(const std::string &header) {
  if (braceOnOwnLine()) {
    line(header);
    line("{");
  } else {
    line(header + " {");
  }
  ++indent;
}

void CLikeEmitter::closeBlock() {
  --indent;
  line("}");
}

void CLikeEmitter::elseBlock(const std::string &header) {
  --indent;
  if (braceOnOwnLine()) {
    line("}");
    line(header);
    line("{");
  } else {
    line("} " + header + " {");
  }
  ++indent;
}

std::string CLikeEmitter::nameOf(Operation *op, llvm::StringRef fallback) {
  if (auto name = op->getAttrOfType<StringAttr>(kEmitNameAttr))
    return name.str();
  return fallback.str();
}

std::string CLikeEmitter::quote(const std::string &value) {
  std::string quoted = "\"";
  for (unsigned char c : value) {
    switch (c) {
    case '\n': quoted += "\\n"; break;
    case '\t': quoted += "\\t"; break;
    case '\r': quoted += "\\r"; break;
    case '\\': quoted += "\\\\"; break;
    case '"': quoted += "\\\""; break;
    default:
      if (c < 0x20) {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
        quoted += buffer;
      } else {
        quoted += static_cast<char>(c);
      }
    }
  }
  return quoted + "\"";
}

std::string CLikeEmitter::paramList(FuncOp func, bool typeFirst) {
  std::string params;
  if (func.getBody().empty())
    return params;
  for (auto &op : func.getBody().front()) {
    auto var = dyn_cast<VarOp>(op);
    if (!var || !var.getIsParam())
      continue;
    std::string name = nameOf(var, var.getName());
    std::string type = typeName(var.getRef().getType().getPointee());
    if (!params.empty())
      params += ", ";
    params += typeFirst ? type + " " + name : name + ": " + type;
  }
  return params;
}

Value CLikeEmitter::regionValue(Region &region) {
  return cast<RegionResultOp>(region.front().back()).getValue();
}

std::string CLikeEmitter::globalInit(GlobalOp global) {
  if (!global.getInit().empty())
    return text(regionValue(global.getInit()));
  Type type = global.getType();
  if (isa<IntType>(type))
    return "0";
  if (isa<BoolType>(type))
    return "false";
  return "\"\"";
}

bool CLikeEmitter::isReassigned(VarOp var) {
  for (Operation *user : var.getRef().getUsers())
    if (!isa<LoadOp>(user))
      return true;
  return false;
}

//===----------------------------------------------------------------------===//
// Statements
//===----------------------------------------------------------------------===//

void CLikeEmitter::emitBlock(Block &block) {
  for (Operation &op : block)
    statement(op);
}

void CLikeEmitter::emitStatements(Region &region) {
  if (!region.empty())
    emitBlock(region.front());
}

void CLikeEmitter::statement(Operation &op) {
  if (auto var = dyn_cast<VarOp>(op)) {
    if (var.getIsParam())
      return;
    line(localDeclaration(var, var.getInit() ? text(var.getInit()) : "") + ";");
    return;
  }
  if (isa<RegionResultOp>(op))
    return;
  // An op whose result is used is printed at its use.
  if (op.getNumResults() != 0 && !op.use_empty())
    return;

  llvm::TypeSwitch<Operation *>(&op)
      .Case<IfOp>([&](IfOp ifOp) { ifStatement(ifOp, ""); })
      .Case<WhileOp>([&](WhileOp loop) {
        openBlock("while (" + text(regionValue(loop.getCondRegion())) + ")");
        emitStatements(loop.getBodyRegion());
        closeBlock();
      })
      .Case<DoWhileOp>([&](DoWhileOp loop) {
        openBlock("do");
        emitStatements(loop.getBodyRegion());
        std::string condition = "while (" + text(regionValue(loop.getCondRegion())) + ");";
        if (braceOnOwnLine()) {
          closeBlock();
          line(condition);
        } else {
          --indent;
          line("} " + condition);
        }
      })
      .Case<ForOp>([&](ForOp loop) {
        openBlock(forHeader(loop));
        emitStatements(loop.getLoop().getBodyRegion());
        closeBlock();
      })
      .Case<ReturnOp>([&](ReturnOp ret) {
        line(ret.getValue() ? "return " + text(ret.getValue()) + ";" : "return;");
      })
      .Case<BreakOp>([&](BreakOp) { line("break;"); })
      .Case<ContinueOp>([&](ContinueOp) { line("continue;"); })
      .Case<PrintOp>([&](PrintOp print) {
        if (!print.getValue()) {
          line(printStatement(nullptr, true, print.getNewline()) + ";");
          return;
        }
        Expr value = stringOperand(print.getValue(), true);
        auto convert = print.getValue().getDefiningOp<ToStringOp>();
        bool isString = !convert || (convert.getBoolStyle() &&
                                     *convert.getBoolStyle() != nativeBoolStyle());
        line(printStatement(&value, isString, print.getNewline()) + ";");
      })
      .Default([&](Operation *other) { line(expressionOp(other).text + ";"); });
}

void CLikeEmitter::ifStatement(IfOp op, const std::string &prefix) {
  std::string header = prefix + "if (" + text(op.getCond()) + ")";
  if (prefix.empty())
    openBlock(header);
  else
    elseBlock(header);
  emitStatements(op.getThenRegion());

  Region &otherwise = op.getElseRegion();
  if (!otherwise.empty() && !otherwise.front().empty()) {
    // `else if` chains stay flat. The condition's expression ops share the
    // block with the nested if, so only the last op has to be that if.
    auto nested = dyn_cast<IfOp>(otherwise.front().back());
    bool onlyExpressions = nested != nullptr;
    if (nested)
      for (Operation &inner : otherwise.front())
        if (&inner != nested.getOperation() &&
            (inner.getNumResults() == 0 || inner.use_empty() || isa<VarOp>(inner)))
          onlyExpressions = false;
    if (onlyExpressions) {
      ifStatement(nested, "else ");
      return;
    }
    elseBlock("else");
    emitStatements(otherwise);
  }
  closeBlock();
}

std::string CLikeEmitter::forClause(Region &region) {
  std::string clause;
  if (region.empty())
    return clause;
  bool first = true;
  for (Operation &op : region.front()) {
    std::string piece;
    if (auto var = dyn_cast<VarOp>(op)) {
      std::string init = var.getInit() ? text(var.getInit()) : "";
      if (first) {
        piece = localDeclaration(var, init);
      } else {
        piece = nameOf(var, var.getName());
        if (!init.empty())
          piece += " = " + init;
      }
    } else if (isa<ForLoopOp>(op) || (op.getNumResults() != 0 && !op.use_empty())) {
      continue;
    } else {
      piece = expressionOp(&op).text;
    }
    if (!first)
      clause += ", ";
    clause += piece;
    first = false;
  }
  return clause;
}

std::string CLikeEmitter::forHeader(ForOp op) {
  std::string condition;
  ForLoopOp loop = op.getLoop();
  Region &cond = loop.getCondRegion();
  if (!cond.empty() && !cond.front().empty())
    condition = text(regionValue(cond));
  std::string init = forClause(op.getInitRegion());
  std::string step = forClause(loop.getStepRegion());
  return "for (" + init + "; " + condition + (step.empty() ? ";" : "; " + step) + ")";
}

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

std::string CLikeEmitter::operand(const Expr &value, int minPrecedence) {
  return value.precedence < minPrecedence ? "(" + value.text + ")" : value.text;
}

std::string CLikeEmitter::text(Value value, int minPrecedence) {
  return operand(expr(value), minPrecedence);
}

Expr CLikeEmitter::expr(Value value) {
  Operation *op = value.getDefiningOp();
  assert(op && "block arguments are only read through their parameter variable");
  return expressionOp(op);
}

Expr CLikeEmitter::binary(const Expr &lhs, const std::string &op, const Expr &rhs,
                          int precedence) {
  return {operand(lhs, precedence) + " " + op + " " + operand(rhs, precedence + 1), precedence};
}

Expr CLikeEmitter::divide(const Expr &lhs, const Expr &rhs) {
  return binary(lhs, "/", rhs, kPrecMultiplicative);
}

Expr CLikeEmitter::unsignedShift(const Expr &lhs, const Expr &rhs) {
  return binary(lhs, ">>>", rhs, kPrecShift);
}

Expr CLikeEmitter::stringEquals(const Expr &lhs, const Expr &rhs, bool negate) {
  return binary(lhs, equalsOperator(negate), rhs, kPrecEquality);
}

std::string CLikeEmitter::refName(Value ref) {
  if (auto var = ref.getDefiningOp<VarOp>())
    return nameOf(var, var.getName());
  auto global = ref.getDefiningOp<GlobalRefOp>();
  return symbolNames.lookup(global.getGlobal());
}

Expr CLikeEmitter::boolToString(ToStringOp op) {
  bool pascal = *op.getBoolStyle() == "pascal";
  std::string yes = quote(pascal ? "True" : "true");
  std::string no = quote(pascal ? "False" : "false");
  return {text(op.getValue(), kPrecTernary + 1) + " ? " + yes + " : " + no, kPrecTernary};
}

Expr CLikeEmitter::stringOperand(Value value, bool implicit) {
  auto convert = value.getDefiningOp<ToStringOp>();
  if (!convert)
    return expr(value);
  Type type = convert.getValue().getType();
  if (isa<BoolType>(type) && *convert.getBoolStyle() != nativeBoolStyle())
    return boolToString(convert);
  if (implicit)
    return expr(convert.getValue());
  return explicitToString(expr(convert.getValue()), type);
}

Expr CLikeEmitter::expressionOp(Operation *op) {
  auto bin = [&](Value lhs, Value rhs, const char *symbol, int precedence) {
    return binary(expr(lhs), symbol, expr(rhs), precedence);
  };

  return llvm::TypeSwitch<Operation *, Expr>(op)
      .Case<IntLitOp>([&](IntLitOp lit) -> Expr {
        int64_t value = lit.getValue();
        return {std::to_string(value), value < 0 ? kPrecUnary : kPrecPrimary};
      })
      .Case<BoolLitOp>([&](BoolLitOp lit) -> Expr {
        return {lit.getValue() ? "true" : "false", kPrecPrimary};
      })
      .Case<StrLitOp>([&](StrLitOp lit) -> Expr { return {quote(lit.getValue().str())}; })
      .Case<LoadOp>([&](LoadOp load) -> Expr { return {refName(load.getRef())}; })
      .Case<StoreOp>([&](StoreOp store) -> Expr {
        return {refName(store.getRef()) + " = " + text(store.getValue(), kPrecAssign), kPrecAssign};
      })
      .Case<CompoundAssignOp>([&](CompoundAssignOp assign) -> Expr {
        static const llvm::StringMap<std::string> symbols = {
            {"add", "+"},     {"sub", "-"},     {"mul", "*"},     {"div", "/"},
            {"rem", "%"},     {"bit_and", "&"}, {"bit_or", "|"},  {"bit_xor", "^"},
            {"shl", "<<"},    {"shr", ">>"},    {"ushr", ">>>"},  {"concat", "+"}};
        std::string target = refName(assign.getRef());
        Expr value = assign.getKind() == "concat" ? stringOperand(assign.getValue(), true)
                                                  : expr(assign.getValue());
        std::string fallback = compoundFallback(assign.getKind().str(), target, value);
        if (!fallback.empty())
          return {fallback, kPrecAssign};
        return {target + " " + symbols.lookup(assign.getKind()) + "= " +
                    operand(value, kPrecAssign),
                kPrecAssign};
      })
      .Case<IncDecOp>([&](IncDecOp inc) -> Expr {
        std::string token = inc.getDelta() > 0 ? "++" : "--";
        std::string name = refName(inc.getRef());
        if (inc.getPrefix())
          return {token + name, kPrecUnary};
        return {name + token, kPrecPostfix};
      })
      .Case<AddOp>([&](AddOp o) { return bin(o.getLhs(), o.getRhs(), "+", kPrecAdditive); })
      .Case<SubOp>([&](SubOp o) { return bin(o.getLhs(), o.getRhs(), "-", kPrecAdditive); })
      .Case<MulOp>([&](MulOp o) { return bin(o.getLhs(), o.getRhs(), "*", kPrecMultiplicative); })
      .Case<RemOp>([&](RemOp o) { return bin(o.getLhs(), o.getRhs(), "%", kPrecMultiplicative); })
      .Case<DivOp>([&](DivOp o) { return divide(expr(o.getLhs()), expr(o.getRhs())); })
      .Case<BitAndOp>([&](BitAndOp o) { return bin(o.getLhs(), o.getRhs(), "&", kPrecBitAnd); })
      .Case<BitOrOp>([&](BitOrOp o) { return bin(o.getLhs(), o.getRhs(), "|", kPrecBitOr); })
      .Case<BitXorOp>([&](BitXorOp o) { return bin(o.getLhs(), o.getRhs(), "^", kPrecBitXor); })
      .Case<ShlOp>([&](ShlOp o) { return bin(o.getLhs(), o.getRhs(), "<<", kPrecShift); })
      .Case<ShrOp>([&](ShrOp o) { return bin(o.getLhs(), o.getRhs(), ">>", kPrecShift); })
      .Case<UShrOp>([&](UShrOp o) { return unsignedShift(expr(o.getLhs()), expr(o.getRhs())); })
      .Case<NegOp>([&](NegOp o) -> Expr {
        return {"-" + text(o.getValue(), kPrecUnary + 1), kPrecUnary};
      })
      .Case<BitNotOp>([&](BitNotOp o) -> Expr {
        return {"~" + text(o.getValue(), kPrecUnary + 1), kPrecUnary};
      })
      .Case<CmpOp>([&](CmpOp cmp) -> Expr {
        llvm::StringRef pred = cmp.getPred();
        if (pred == "eq" || pred == "ne")
          return bin(cmp.getLhs(), cmp.getRhs(), equalsOperator(pred == "ne").c_str(),
                     kPrecEquality);
        const char *symbol = pred == "lt" ? "<" : pred == "le" ? "<=" : pred == "gt" ? ">" : ">=";
        return bin(cmp.getLhs(), cmp.getRhs(), symbol, kPrecRelational);
      })
      .Case<NotOp>([&](NotOp o) -> Expr {
        if (auto equal = o.getValue().getDefiningOp<StrEqOp>())
          return stringEquals(expr(equal.getLhs()), expr(equal.getRhs()), true);
        return {"!" + text(o.getValue(), kPrecUnary + 1), kPrecUnary};
      })
      .Case<LogicalAndOp>([&](LogicalAndOp o) {
        return bin(o.getLhs(), regionValue(o.getRhs()), "&&", kPrecLogicalAnd);
      })
      .Case<LogicalOrOp>([&](LogicalOrOp o) {
        return bin(o.getLhs(), regionValue(o.getRhs()), "||", kPrecLogicalOr);
      })
      .Case<TernaryOp>([&](TernaryOp o) -> Expr {
        return {text(o.getCond(), kPrecTernary + 1) + " ? " +
                    text(regionValue(o.getThenRegion()), kPrecTernary) + " : " +
                    text(regionValue(o.getElseRegion()), kPrecTernary),
                kPrecTernary};
      })
      .Case<CallOp>([&](CallOp call) -> Expr {
        std::string args;
        for (Value arg : call.getArgs()) {
          if (!args.empty())
            args += ", ";
          args += text(arg);
        }
        return {symbolNames.lookup(call.getCallee()) + "(" + args + ")"};
      })
      .Case<StrConcatOp>([&](StrConcatOp o) -> Expr {
        // `+` only converts when the other side is known to be a string.
        bool leftConverts = o.getLhs().getDefiningOp<ToStringOp>() != nullptr;
        bool rightConverts = o.getRhs().getDefiningOp<ToStringOp>() != nullptr;
        return binary(stringOperand(o.getLhs(), !rightConverts), "+",
                      stringOperand(o.getRhs(), !leftConverts), kPrecAdditive);
      })
      .Case<StrLenOp>([&](StrLenOp o) { return stringLength(expr(o.getValue())); })
      .Case<StrEqOp>([&](StrEqOp o) {
        return stringEquals(expr(o.getLhs()), expr(o.getRhs()), false);
      })
      .Case<ToStringOp>([&](ToStringOp o) { return stringOperand(o.getResult(), false); })
      .Default([&](Operation *other) -> Expr {
        llvm_unreachable("unhandled op in expression position");
      });
}
