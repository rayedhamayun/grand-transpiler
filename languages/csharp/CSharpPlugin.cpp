#include "languages/Plugins.h"

#include "grand/Core/Transpiler.h"
#include "grand/Emit/CLikeEmitter.h"

using namespace mlir;
using namespace grand;

namespace {

class CSharpEmitter : public CLikeEmitter {
protected:
  std::string nativeBoolStyle() const override { return "pascal"; }
  bool braceOnOwnLine() const override { return true; }

  std::string typeName(Type type) override {
    if (isa<IntType>(type))
      return "int";
    return isa<BoolType>(type) ? "bool" : "string";
  }

  static bool isLiteral(Value value) {
    return value && value.getDefiningOp() &&
           isa<IntLitOp, BoolLitOp, StrLitOp>(value.getDefiningOp());
  }

  std::string localDeclaration(VarOp var, const std::string &init) override {
    // `const` needs a compile-time constant; other values are simply locals.
    std::string declaration = var.getIsConst() && isLiteral(var.getInit()) ? "const " : "";
    declaration += typeName(var.getRef().getType().getPointee()) + " " + nameOf(var, var.getName());
    return init.empty() ? declaration : declaration + " = " + init;
  }

  std::string printStatement(const Expr *value, bool, bool newline) override {
    return std::string("Console.") + (newline ? "WriteLine(" : "Write(") +
           (value ? value->text : "") + ")";
  }

  Expr stringLength(const Expr &value) override {
    std::string target = value.precedence < kPrecPostfix ? "(" + value.text + ")" : value.text;
    return {target + ".Length", kPrecPostfix};
  }

  Expr explicitToString(const Expr &value, Type) override {
    std::string target = value.precedence < kPrecPostfix ? "(" + value.text + ")" : value.text;
    return {target + ".ToString()"};
  }

  void emitModule(ModuleOp module) override {
    line("using System;");
    blank();
    openBlock("public class " + className);
    bool first = true;
    for (GlobalOp global : module.getOps<GlobalOp>()) {
      first = false;
      Region &init = global.getInit();
      bool literal = !init.empty() && isLiteral(regionValue(init));
      std::string modifier = !global.getIsConst() ? "static " : literal ? "const " : "static readonly ";
      line("public " + modifier + typeName(global.getType()) + " " +
           symbolNames.lookup(global.getSymName()) + " = " + globalInit(global) + ";");
    }
    for (FuncOp func : module.getOps<FuncOp>()) {
      if (!first)
        blank();
      first = false;
      std::string result =
          func.getResultTypes().empty() ? "void" : typeName(func.getResultTypes().front());
      if (func.getEntry())
        openBlock("public static void Main()");
      else
        openBlock("public static " + result + " " + symbolNames.lookup(func.getSymName()) + "(" +
                  paramList(func, true) + ")");
      emitStatements(func.getBody());
      closeBlock();
    }
    closeBlock();
  }
};

TargetProfile makeProfile() {
  TargetProfile profile;
  profile.reservedEscapePrefix = "@";
  profile.reservedWords = {
      "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked",
      "class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else",
      "enum", "event", "explicit", "extern", "false", "finally", "fixed", "float", "for",
      "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock",
      "long", "namespace", "new", "null", "object", "operator", "out", "override", "params",
      "private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
      "sizeof", "stackalloc", "static", "string", "struct", "switch", "this", "throw", "true",
      "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using", "virtual",
      "void", "volatile", "while"};
  return profile;
}

class CSharpPlugin : public LanguagePlugin {
public:
  std::string name() const override { return "csharp"; }
  std::vector<std::string> extensions() const override { return {"cs"}; }
  const TargetProfile &profile() const override {
    static const TargetProfile instance = makeProfile();
    return instance;
  }
  OwningOpRef<ModuleOp> importSource(const std::string &source, const std::string &fileName,
                                     MLIRContext &context, Diagnostics &diagnostics) const override {
    return importCSharp(source, fileName, context, diagnostics);
  }
  std::string emit(ModuleOp module, const EmitOptions &options) const override {
    return CSharpEmitter().emit(module, options);
  }
};

} // namespace

const LanguagePlugin &grand::csharpPlugin() {
  static const CSharpPlugin plugin;
  return plugin;
}
