#include "languages/Plugins.h"

#include "grand/Core/Transpiler.h"
#include "grand/Emit/CLikeEmitter.h"

using namespace mlir;
using namespace grand;

namespace {

class TypeScriptEmitter : public CLikeEmitter {
protected:
  std::string nativeBoolStyle() const override { return "lower"; }
  std::string equalsOperator(bool negate) override { return negate ? "!==" : "==="; }

  std::string typeName(Type type) override {
    if (isa<IntType>(type))
      return "number";
    return isa<BoolType>(type) ? "boolean" : "string";
  }

  std::string localDeclaration(VarOp var, const std::string &init) override {
    std::string name = nameOf(var, var.getName());
    if (init.empty())
      return "let " + name + ": " + typeName(var.getRef().getType().getPointee());
    return (isReassigned(var) ? "let " : "const ") + name + " = " + init;
  }

  std::string printStatement(const Expr *value, bool isString, bool newline) override {
    if (newline)
      return "console.log(" + (value ? value->text : std::string()) + ")";
    std::string text = value ? value->text : "\"\"";
    return "process.stdout.write(" + (isString ? text : "String(" + text + ")") + ")";
  }

  Expr stringLength(const Expr &value) override {
    std::string target = value.precedence < kPrecPostfix ? "(" + value.text + ")" : value.text;
    return {target + ".length", kPrecPostfix};
  }

  Expr explicitToString(const Expr &value, Type) override {
    return {"String(" + value.text + ")"};
  }

  Expr divide(const Expr &lhs, const Expr &rhs) override {
    std::string right = rhs.precedence <= kPrecMultiplicative ? "(" + rhs.text + ")" : rhs.text;
    std::string left = lhs.precedence < kPrecMultiplicative ? "(" + lhs.text + ")" : lhs.text;
    return {"Math.trunc(" + left + " / " + right + ")"};
  }

  Expr unsignedShift(const Expr &lhs, const Expr &rhs) override {
    std::string right = rhs.precedence <= kPrecShift ? "(" + rhs.text + ")" : rhs.text;
    std::string left = lhs.precedence < kPrecShift ? "(" + lhs.text + ")" : lhs.text;
    return {"(" + left + " >>> " + right + ") | 0", kPrecBitOr};
  }

  std::string compoundFallback(const std::string &kind, const std::string &target,
                               const Expr &value) override {
    Expr self{target};
    if (kind == "div")
      return target + " = " + divide(self, value).text;
    if (kind == "ushr")
      return target + " = " + unsignedShift(self, value).text;
    return {};
  }

  void emitModule(ModuleOp module) override {
    bool first = true;
    for (GlobalOp global : module.getOps<GlobalOp>()) {
      first = false;
      line(std::string("export ") + (global.getIsConst() ? "const " : "let ") +
           symbolNames.lookup(global.getSymName()) + " = " + globalInit(global) + ";");
    }
    for (FuncOp func : module.getOps<FuncOp>()) {
      if (func.getEntry())
        continue;
      if (!first)
        blank();
      first = false;
      std::string result =
          func.getResultTypes().empty() ? "void" : typeName(func.getResultTypes().front());
      openBlock("export function " + symbolNames.lookup(func.getSymName()) + "(" +
                paramList(func, false) + "): " + result);
      emitStatements(func.getBody());
      closeBlock();
    }
    for (FuncOp func : module.getOps<FuncOp>()) {
      if (!func.getEntry())
        continue;
      if (!first)
        blank();
      emitEntry(func);
    }
  }

  // `return` is illegal at module level, so such bodies stay in a function.
  void emitEntry(FuncOp func) {
    bool hasReturn = false;
    func.walk([&](ReturnOp) { hasReturn = true; });
    if (!hasReturn) {
      emitStatements(func.getBody());
      return;
    }
    std::string name = "main";
    while (symbolTaken(name, func.getSymName().str()))
      name += "_";
    openBlock("function " + name + "(): void");
    emitStatements(func.getBody());
    closeBlock();
    blank();
    line(name + "();");
  }

  bool symbolTaken(const std::string &name, const std::string &self) {
    for (auto &entry : symbolNames)
      if (entry.second == name && entry.first() != self)
        return true;
    return false;
  }
};

TargetProfile makeProfile() {
  TargetProfile profile;
  profile.allowsLocalShadowing = true;
  profile.allowsOverloads = false;
  profile.numbersAreDoubles = true;
  profile.entryIsTopLevel = true;
  profile.reservedWords = {
      "arguments", "await", "break", "case", "catch", "class", "const", "continue", "debugger",
      "default", "delete", "do", "else", "enum", "eval", "export", "extends", "false", "finally",
      "for", "function", "if", "implements", "import", "in", "instanceof", "interface", "let",
      "new", "null", "package", "private", "protected", "public", "return", "static", "super",
      "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", "yield"};
  return profile;
}

class TypeScriptPlugin : public LanguagePlugin {
public:
  std::string name() const override { return "typescript"; }
  std::vector<std::string> extensions() const override { return {"ts"}; }
  const TargetProfile &profile() const override {
    static const TargetProfile instance = makeProfile();
    return instance;
  }
  OwningOpRef<ModuleOp> importSource(const std::string &source, const std::string &fileName,
                                     MLIRContext &context, Diagnostics &diagnostics) const override {
    return importTypeScript(source, fileName, context, diagnostics);
  }
  std::string emit(ModuleOp module, const EmitOptions &options) const override {
    return TypeScriptEmitter().emit(module, options);
  }
};

} // namespace

const LanguagePlugin &grand::typescriptPlugin() {
  static const TypeScriptPlugin plugin;
  return plugin;
}
