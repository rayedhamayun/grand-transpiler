#include "languages/Plugins.h"

#include "grand/Import/CStyleImporter.h"

extern "C" const TSLanguage *tree_sitter_java(void);

using namespace mlir;
using namespace grand;

namespace {

class JavaImporter : public CStyleImporter {
public:
  using CStyleImporter::CStyleImporter;

protected:
  std::string boolStyle() const override { return "lower"; }

  void collectDeclarations(TSNode root) override {
    std::vector<TSNode> classes;
    for (TSNode child : namedChildren(root)) {
      std::string k = kind(child);
      if (k == "package_declaration" || k == "import_declaration")
        continue;
      if (k == "class_declaration")
        classes.push_back(child);
      else
        member(child); // A compact source file has members at the top level.
    }
    if (classes.size() > 1)
      fail(classes[1], "only one class is supported");
    if (classes.empty())
      return;
    TSNode cls = classes[0];
    className = text(field(cls, "name"));
    for (TSNode child : namedChildren(field(cls, "body")))
      member(child);
  }

  void member(TSNode node) {
    std::string k = kind(node);
    if (k == "method_declaration")
      method(node);
    else if (k == "field_declaration" || k == "local_variable_declaration")
      memberVariable(node);
    else
      unsupported(node);
  }

  bool hasModifier(TSNode node, const std::string &modifier) {
    for (TSNode child : namedChildren(node))
      if (kind(child) == "modifiers")
        return text(child).find(modifier) != std::string::npos;
    return false;
  }

  void memberVariable(TSNode node) {
    TSNode typeNode = field(node, "type");
    Type type = parseType(typeNode);
    if (!type)
      fail(typeNode, "'var' is not allowed for member variables");
    bool isConst = hasModifier(node, "final");
    for (TSNode declarator : fieldAll(node, "declarator")) {
      if (!ts_node_is_null(field(declarator, "dimensions")))
        unsupported(declarator);
      TSNode initializer = field(declarator, "value");
      if (ts_node_is_null(initializer) && isa<StringType>(type))
        fail(declarator, "an uninitialised String member is null, which is not supported");
      addGlobal(text(field(declarator, "name")), declarator, type, initializer, isConst, false);
    }
  }

  void method(TSNode node) {
    TSNode body = field(node, "body");
    if (ts_node_is_null(body))
      fail(node, "methods without bodies are not supported");
    std::string name = text(field(node, "name"));
    bool entryCandidate = name == "main" && kind(field(node, "type")) == "void_type";

    std::vector<std::string> names;
    std::vector<Type> types;
    bool isEntry = entryCandidate;
    auto params = namedChildren(field(node, "parameters"));
    for (TSNode param : params) {
      TSNode typeNode = field(param, "type");
      if (kind(param) != "formal_parameter")
        unsupported(param);
      if (entryCandidate && params.size() == 1 && kind(typeNode) == "array_type")
        continue; // `String[] args`; using it fails as an unknown identifier.
      isEntry = false;
      Type type = parseType(typeNode);
      if (!type)
        fail(param, "parameters need a type");
      names.push_back(text(field(param, "name")));
      types.push_back(type);
    }

    Function &function = addFunction(name);
    function.declaration = node;
    function.body = body;
    function.paramNames = names;
    function.paramTypes = types;
    function.returnKnown = true;
    function.returnType = parseReturnType(field(node, "type"));
    function.isEntry = isEntry;
  }

  Type parseReturnType(TSNode node) {
    return kind(node) == "void_type" ? Type() : parseType(node);
  }

  Type parseType(TSNode node) override {
    std::string name = text(node);
    if (name == "int")
      return intType();
    if (name == "boolean")
      return boolType();
    if (name == "String")
      return stringType();
    if (name == "var")
      return nullptr;
    fail(node, "unsupported type '" + name + "'");
  }

  void importLocalDeclaration(TSNode node) override {
    Type type = parseType(field(node, "type"));
    bool isConst = hasModifier(node, "final");
    for (TSNode declarator : fieldAll(node, "declarator")) {
      if (!ts_node_is_null(field(declarator, "dimensions")))
        unsupported(declarator);
      declareVariable(declarator, text(field(declarator, "name")), type,
                      field(declarator, "value"), isConst);
    }
  }

  Value importCall(TSNode node) override {
    TSNode objectNode = field(node, "object");
    std::string method = text(field(node, "name"));
    auto args = argumentNodes(field(node, "arguments"));
    Location l = loc(node);

    if (ts_node_is_null(objectNode))
      return callFunction(node, method, args);
    std::string object = text(objectNode);
    if (isSelf(object))
      return callFunction(node, method, args);

    if (object == "System.out" || object == "IO") {
      if (method != "println" && method != "print")
        unsupported(node);
      if (args.size() > 1)
        fail(node, "print takes at most one argument");
      Value value = args.empty() ? Value() : requiredExpr(args[0]);
      print(l, value, method == "println");
      return nullptr;
    }
    if ((object == "String" && method == "valueOf") ||
        ((object == "Integer" || object == "Boolean") && method == "toString")) {
      if (args.size() != 1)
        unsupported(node);
      return toStringValue(l, requiredExpr(args[0]));
    }

    if (method == "length" && args.empty()) {
      Value value = requiredExpr(objectNode);
      if (isa<StringType>(value.getType()))
        return StrLenOp::create(builder, l, intType(), value);
    }
    if (method == "equals" && args.size() == 1) {
      Value lhs = requiredExpr(objectNode);
      Value rhs = requiredExpr(args[0]);
      if (isa<StringType>(lhs.getType()) && isa<StringType>(rhs.getType()))
        return StrEqOp::create(builder, l, boolType(), lhs, rhs);
    }
    unsupported(node);
  }

  Value importSpecialBinary(TSNode node) override {
    std::string op = operatorOf(node);
    if (op != "==" && op != "!=")
      return nullptr;
    Value lhs = requiredExpr(field(node, "left"));
    Value rhs = requiredExpr(field(node, "right"));
    if (isa<StringType>(lhs.getType()))
      warn(node, "'" + op + "' on Strings compares references; treated as value equality");
    return binary(node, op, lhs, rhs);
  }
};

} // namespace

OwningOpRef<ModuleOp> grand::importJava(const std::string &source, const std::string &fileName,
                                        MLIRContext &context, Diagnostics &diagnostics) {
  JavaImporter importer(source, fileName, context, diagnostics);
  return importer.run(tree_sitter_java(), "java");
}
