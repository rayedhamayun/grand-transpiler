#include "languages/Plugins.h"

#include "grand/Core/Transpiler.h"
#include "grand/Emit/CLikeEmitter.h"

using namespace mlir;
using namespace grand;

namespace {

class JavaEmitter : public CLikeEmitter {
protected:
  std::string nativeBoolStyle() const override { return "lower"; }

  std::string typeName(Type type) override {
    if (isa<IntType>(type))
      return "int";
    return isa<BoolType>(type) ? "boolean" : "String";
  }

  std::string localDeclaration(VarOp var, const std::string &init) override {
    std::string declaration = var.getIsConst() ? "final " : "";
    declaration += typeName(var.getRef().getType().getPointee()) + " " + nameOf(var, var.getName());
    return init.empty() ? declaration : declaration + " = " + init;
  }

  std::string printStatement(const Expr *value, bool, bool newline) override {
    return std::string("System.out.") + (newline ? "println(" : "print(") +
           (value ? value->text : "") + ")";
  }

  Expr stringEquals(const Expr &lhs, const Expr &rhs, bool negate) override {
    std::string left = lhs.precedence < kPrecPostfix ? "(" + lhs.text + ")" : lhs.text;
    std::string call = left + ".equals(" + rhs.text + ")";
    return negate ? Expr{"!" + call, kPrecUnary} : Expr{call, kPrecPostfix};
  }

  Expr stringLength(const Expr &value) override {
    std::string target = value.precedence < kPrecPostfix ? "(" + value.text + ")" : value.text;
    return {target + ".length()", kPrecPostfix};
  }

  Expr explicitToString(const Expr &value, Type) override {
    return {"String.valueOf(" + value.text + ")"};
  }

  void emitModule(ModuleOp module) override {
    openBlock("public class " + className);
    bool first = true;
    auto separate = [&] {
      if (!first)
        blank();
      first = false;
    };
    for (GlobalOp global : module.getOps<GlobalOp>()) {
      first = false;
      line(std::string("public static ") + (global.getIsConst() ? "final " : "") +
           typeName(global.getType()) + " " + symbolNames.lookup(global.getSymName()) + " = " +
           globalInit(global) + ";");
    }
    for (FuncOp func : module.getOps<FuncOp>()) {
      separate();
      std::string result =
          func.getResultTypes().empty() ? "void" : typeName(func.getResultTypes().front());
      if (func.getEntry())
        openBlock("public static void main(String[] args)");
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
  profile.reservedWords = {
      "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const",
      "continue", "default", "do", "double", "else", "enum", "extends", "false", "final",
      "finally", "float", "for", "goto", "if", "implements", "import", "instanceof", "int",
      "interface", "long", "native", "new", "null", "package", "private", "protected", "public",
      "return", "short", "static", "strictfp", "super", "switch", "synchronized", "this", "throw",
      "throws", "transient", "true", "try", "void", "volatile", "while", "_"};
  return profile;
}

class JavaPlugin : public LanguagePlugin {
public:
  std::string name() const override { return "java"; }
  std::vector<std::string> extensions() const override { return {"java"}; }
  const TargetProfile &profile() const override {
    static const TargetProfile instance = makeProfile();
    return instance;
  }
  OwningOpRef<ModuleOp> importSource(const std::string &source, const std::string &fileName,
                                     MLIRContext &context, Diagnostics &diagnostics) const override {
    return importJava(source, fileName, context, diagnostics);
  }
  std::string emit(ModuleOp module, const EmitOptions &options) const override {
    return JavaEmitter().emit(module, options);
  }
};

} // namespace

const LanguagePlugin &grand::javaPlugin() {
  static const JavaPlugin plugin;
  return plugin;
}
