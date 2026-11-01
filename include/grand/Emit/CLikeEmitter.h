#ifndef GRAND_EMIT_CLIKEEMITTER_H
#define GRAND_EMIT_CLIKEEMITTER_H

#include "grand/Core/LanguagePlugin.h"
#include "grand/Dialect/GrandDialect.h"

#include "llvm/ADT/StringMap.h"

#include <string>

namespace grand {

// Operator precedence, shared by the C-like languages. Higher binds tighter.
enum Precedence {
  kPrecAssign = 1,
  kPrecTernary,
  kPrecLogicalOr,
  kPrecLogicalAnd,
  kPrecBitOr,
  kPrecBitXor,
  kPrecBitAnd,
  kPrecEquality,
  kPrecRelational,
  kPrecShift,
  kPrecAdditive,
  kPrecMultiplicative,
  kPrecUnary,
  kPrecPostfix,
  kPrecPrimary,
};

struct Expr {
  std::string text;
  int precedence = kPrecPrimary;
};

// Prints grand IR as curly-brace source. Expression trees are rebuilt from
// their single-use SSA values; subclasses provide language spellings.
class CLikeEmitter {
public:
  virtual ~CLikeEmitter() = default;
  std::string emit(mlir::ModuleOp module, const EmitOptions &options);

protected:
  virtual void emitModule(mlir::ModuleOp module) = 0;

  // Spellings.
  virtual std::string typeName(mlir::Type type) = 0;
  virtual std::string localDeclaration(VarOp var, const std::string &init) = 0;
  virtual std::string printStatement(const Expr *value, bool isString, bool newline) = 0;
  virtual std::string equalsOperator(bool negate) { return negate ? "!=" : "=="; }
  virtual Expr stringEquals(const Expr &lhs, const Expr &rhs, bool negate);
  virtual Expr stringLength(const Expr &value) = 0;
  virtual Expr explicitToString(const Expr &value, mlir::Type type) = 0;
  virtual Expr divide(const Expr &lhs, const Expr &rhs);
  virtual Expr unsignedShift(const Expr &lhs, const Expr &rhs);
  // Returns text for `target op= value` when the language has no such form.
  virtual std::string compoundFallback(const std::string &, const std::string &, const Expr &) {
    return {};
  }
  virtual std::string nativeBoolStyle() const = 0;

  // Layout.
  virtual bool braceOnOwnLine() const { return false; }
  void line(const std::string &text);
  void blank();
  void openBlock(const std::string &header);
  void closeBlock();
  void elseBlock(const std::string &header);

  // Helpers for subclasses.
  void emitBlock(mlir::Block &block);
  void emitStatements(mlir::Region &region);
  Expr expr(mlir::Value value);
  std::string text(mlir::Value value, int minPrecedence = kPrecAssign);
  std::string quote(const std::string &value);
  std::string paramList(FuncOp func, bool typeFirst);
  std::string nameOf(mlir::Operation *op, llvm::StringRef fallback);
  mlir::Value regionValue(mlir::Region &region);
  // The initial value of a global as text: its initialiser, or the default.
  std::string globalInit(GlobalOp global);
  bool isReassigned(VarOp var);

  std::string className;
  llvm::StringMap<std::string> symbolNames;

private:
  void statement(mlir::Operation &op);
  void ifStatement(IfOp op, const std::string &prefix);
  std::string forHeader(ForOp op);
  std::string forClause(mlir::Region &region);
  std::string refName(mlir::Value ref);
  Expr binary(const Expr &lhs, const std::string &op, const Expr &rhs, int precedence);
  std::string operand(const Expr &value, int minPrecedence);
  Expr expressionOp(mlir::Operation *op);
  // The value of a to_string without conversion, or an explicit form when the
  // surrounding context will not convert implicitly.
  Expr stringOperand(mlir::Value value, bool implicit);
  Expr boolToString(ToStringOp op);

  std::string out;
  int indent = 0;
};

} // namespace grand

#endif // GRAND_EMIT_CLIKEEMITTER_H
