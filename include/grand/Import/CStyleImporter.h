#ifndef GRAND_IMPORT_CSTYLEIMPORTER_H
#define GRAND_IMPORT_CSTYLEIMPORTER_H

#include "grand/Core/Diagnostics.h"
#include "grand/Dialect/GrandDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"

#include <tree_sitter/api.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace grand {

// Thrown to abandon the current statement after reporting an error.
struct ImportFailure {};

// Builds grand IR from the tree-sitter CST of a C-style language. Statements
// and expressions shared by the grammars are handled here; subclasses provide
// declarations, types and library calls.
class CStyleImporter {
public:
  CStyleImporter(const std::string &source, const std::string &fileName,
                 mlir::MLIRContext &context, Diagnostics &diagnostics);
  virtual ~CStyleImporter() = default;

  mlir::OwningOpRef<mlir::ModuleOp> run(const TSLanguage *language,
                                        const std::string &languageName);

protected:
  struct Function {
    std::string name;
    std::string symbol;
    std::vector<std::string> paramNames;
    std::vector<mlir::Type> paramTypes;
    // Null means void. Meaningless until returnKnown is set.
    mlir::Type returnType;
    bool returnKnown = false;
    TSNode declaration{};
    // A block, an expression, or null when `statements` is used.
    TSNode body{};
    std::vector<TSNode> statements;
    bool isEntry = false;
    bool isTopLevel = false;
    enum class State { Pending, InProgress, Done } state = State::Pending;
    FuncOp op;
  };

  struct Global {
    std::string name;
    mlir::Type type;
    TSNode initializer{};
    bool isConst = false;
    // The value is assigned in the entry function, in statement order.
    bool deferred = false;
    TSNode declaration{};
    enum class State { Pending, InProgress, Done } state = State::Pending;
    GlobalOp op;
  };

  // Subclass responsibilities.
  virtual void collectDeclarations(TSNode root) = 0;
  // Returns null for an inferred type such as `var`; reports unsupported types.
  virtual mlir::Type parseType(TSNode typeNode) = 0;
  virtual std::string boolStyle() const = 0;
  virtual mlir::Value importCall(TSNode node) = 0;
  virtual void importLocalDeclaration(TSNode node) = 0;
  virtual mlir::Value importInterpolated(TSNode node);
  // Called for expression kinds the shared code does not know.
  virtual mlir::Value importLanguageExpr(TSNode node);

  // CST helpers.
  std::string text(TSNode node) const;
  static std::string kind(TSNode node);
  static TSNode field(TSNode node, const char *name);
  static std::vector<TSNode> fieldAll(TSNode node, const char *name);
  static std::vector<TSNode> namedChildren(TSNode node);
  static std::string operatorOf(TSNode node);
  static void collectIdentifiers(TSNode node, const std::string &source,
                                 std::set<std::string> &names);
  void collectIdentifiers(TSNode node, std::set<std::string> &names) const {
    collectIdentifiers(node, source, names);
  }
  // A variable read by a global must itself be global: extends `used` with the
  // identifiers in the initialisers of the names already in it.
  void promoteDependencies(const std::vector<TSNode> &initializers, std::set<std::string> &used,
                           const std::function<std::string(size_t)> &nameOf);

  [[noreturn]] void fail(TSNode node, const std::string &message);
  [[noreturn]] void unsupported(TSNode node);
  void warn(TSNode node, const std::string &message);
  mlir::Location loc(TSNode node);

  // Declarations.
  Function &addFunction(std::string name);
  Global &addGlobal(std::string name, TSNode declaration, mlir::Type type, TSNode initializer,
                    bool isConst, bool deferred);
  bool isGlobal(const std::string &name) const { return globals.count(name) != 0; }
  void declareVariable(TSNode node, const std::string &name, mlir::Type type, TSNode initializer,
                       bool isConst);
  mlir::Type intType();
  mlir::Type boolType();
  mlir::Type stringType();

  // Expressions.
  mlir::Value expr(TSNode node);
  mlir::Value identifier(TSNode node);
  mlir::Value requiredExpr(TSNode node);
  mlir::Value conditionExpr(TSNode node);
  mlir::Value toBoolean(TSNode node, mlir::Value value);
  mlir::Value toStringValue(mlir::Location location, mlir::Value value);
  mlir::Value concat(mlir::Location location, mlir::Value lhs, mlir::Value rhs);
  mlir::Value stringLiteral(mlir::Location location, const std::string &value);
  mlir::Value callFunction(TSNode node, const std::string &name, std::vector<TSNode> arguments);
  void print(mlir::Location location, mlir::Value value, bool newline);
  std::vector<TSNode> argumentNodes(TSNode argumentList);
  mlir::Value binary(TSNode node, const std::string &op, mlir::Value lhs, mlir::Value rhs);
  mlir::Value divide(TSNode node, TSNode lhs, TSNode rhs);
  std::string decodeString(TSNode node, const std::string &raw);
  static TSNode unwrapParens(TSNode node);
  // True for the receivers that name the single class: `this` and its name.
  bool isSelf(const std::string &object) const;

  // True when `/` may produce a fraction, so bare division is an assumption.
  virtual bool divisionCanBeFractional() const { return false; }
  // Lets a language recognise patterns before the generic binary lowering.
  virtual mlir::Value importSpecialBinary(TSNode) { return nullptr; }

  std::string className;
  mlir::OpBuilder builder;
  mlir::MLIRContext &context;
  std::string source;
  Diagnostics &diagnostics;

private:
  struct Variable {
    mlir::Value ref;
    mlir::Type type;
  };

  void importFunction(Function &function);
  void importGlobal(Global &global);
  void createGlobalOps();
  void statement(TSNode node);
  void statementBody(TSNode node);
  void blockContents(TSNode node);
  void importInto(mlir::Region &region, TSNode node);
  void ifStatement(TSNode node);
  void whileStatement(TSNode node);
  void doStatement(TSNode node);
  void forStatement(TSNode node);
  void forClause(TSNode node);
  void returnStatement(TSNode node);
  mlir::Value logical(TSNode node, bool coerce);
  mlir::Value ternary(TSNode node);
  mlir::Value unary(TSNode node);
  mlir::Value update(TSNode node);
  mlir::Value assignment(TSNode node);
  mlir::Value member(TSNode node);
  mlir::Value integerLiteral(TSNode node);
  mlir::Value refOf(TSNode node);
  mlir::Value lookupRef(TSNode node, const std::string &name);
  void regionResult(mlir::Region &region, TSNode node, bool asCondition,
                    mlir::Type *resultType = nullptr);
  void pushScope() { scopes.emplace_back(); }
  void popScope() { scopes.pop_back(); }

  std::string fileName;
  std::map<std::string, std::vector<std::unique_ptr<Function>>> functions;
  std::vector<Function *> functionOrder;
  std::map<std::string, std::unique_ptr<Global>> globals;
  std::vector<Global *> globalOrder;
  std::vector<std::map<std::string, Variable>> scopes;
  Function *currentFunction = nullptr;
  mlir::ModuleOp module;
};

} // namespace grand

#endif // GRAND_IMPORT_CSTYLEIMPORTER_H
