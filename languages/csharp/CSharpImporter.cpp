#include "languages/Plugins.h"

#include "grand/Import/CStyleImporter.h"

extern "C" const TSLanguage *tree_sitter_c_sharp(void);

using namespace mlir;
using namespace grand;

namespace {

class CSharpImporter : public CStyleImporter {
public:
  using CStyleImporter::CStyleImporter;

protected:
  std::string boolStyle() const override { return "pascal"; }

  void collectDeclarations(TSNode root) override {
    std::vector<TSNode> classes, statements;
    std::set<std::string> usedInFunctions;

    for (TSNode child : namedChildren(root)) {
      std::string k = kind(child);
      if (k == "using_directive")
        continue;
      if (k == "class_declaration") {
        classes.push_back(child);
      } else if (k == "global_statement") {
        TSNode inner = namedChildren(child).at(0);
        if (kind(inner) == "local_function_statement") {
          localFunction(inner);
          collectIdentifiers(field(inner, "body"), usedInFunctions);
        } else {
          statements.push_back(inner);
        }
      } else {
        unsupported(child);
      }
    }
    if (classes.size() > 1)
      fail(classes[1], "only one class is supported");
    if (!classes.empty()) {
      className = text(field(classes[0], "name"));
      for (TSNode member : namedChildren(field(classes[0], "body")))
        classMember(member);
    }
    if (statements.empty())
      return;

    // Top-level variables that functions read become globals.
    std::vector<TSNode> declared;
    for (TSNode statement : statements)
      if (kind(statement) == "local_declaration_statement")
        for (TSNode declarator : declarators(namedChildren(statement).back()))
          declared.push_back(declarator);
    std::vector<TSNode> initializers;
    for (TSNode declarator : declared)
      initializers.push_back(initializerOf(declarator));
    promoteDependencies(initializers, usedInFunctions,
                        [&](size_t i) { return text(field(declared[i], "name")); });

    bool sawStatement = false;
    bool needsEntry = false;
    for (TSNode statement : statements) {
      if (kind(statement) != "local_declaration_statement") {
        sawStatement = true;
        needsEntry = true;
        continue;
      }
      TSNode declaration = namedChildren(statement).back();
      Type type = parseType(field(declaration, "type"));
      for (TSNode declarator : declarators(declaration)) {
        std::string name = text(field(declarator, "name"));
        if (!usedInFunctions.count(name)) {
          needsEntry = true;
          continue;
        }
        addGlobal(name, declarator, type, initializerOf(declarator),
                  isConstDeclaration(statement), sawStatement);
        needsEntry = needsEntry || sawStatement;
      }
    }
    if (!needsEntry)
      return;
    Function &entry = addFunction("__entry");
    entry.declaration = statements[0];
    entry.statements = statements;
    entry.isEntry = true;
    entry.isTopLevel = true;
    entry.returnKnown = true;
  }

  // `i * j` in argument position parses as declaring a pointer `j`.
  Value importLanguageExpr(TSNode node) override {
    if (kind(node) == "declaration_expression") {
      TSNode typeNode = field(node, "type");
      TSNode name = field(node, "name");
      if (kind(typeNode) == "pointer_type" && !ts_node_is_null(name)) {
        Value lhs = identifier(namedChildren(typeNode).at(0));
        Value rhs = identifier(name);
        return binary(node, "*", lhs, rhs);
      }
    }
    unsupported(node);
  }

  bool isConstDeclaration(TSNode statement) {
    return text(statement).rfind("const", 0) == 0;
  }

  std::vector<TSNode> declarators(TSNode declaration) {
    std::vector<TSNode> out;
    for (TSNode child : namedChildren(declaration))
      if (kind(child) == "variable_declarator")
        out.push_back(child);
    return out;
  }

  // A declarator's initialiser is its only unnamed-field named child.
  TSNode initializerOf(TSNode declarator) {
    TSNode name = field(declarator, "name");
    for (TSNode child : namedChildren(declarator))
      if (!ts_node_eq(child, name))
        return child;
    return TSNode{};
  }

  void classMember(TSNode node) {
    std::string k = kind(node);
    if (k == "method_declaration") {
      method(node);
    } else if (k == "field_declaration") {
      bool isConst = false;
      for (TSNode child : namedChildren(node)) {
        std::string modifier = text(child);
        isConst = isConst || (kind(child) == "modifier" &&
                              (modifier == "const" || modifier == "readonly"));
      }
      TSNode declaration = namedChildren(node).back();
      Type type = parseType(field(declaration, "type"));
      if (!type)
        fail(declaration, "'var' is not allowed for member variables");
      for (TSNode declarator : declarators(declaration)) {
        TSNode initializer = initializerOf(declarator);
        if (ts_node_is_null(initializer) && isa<StringType>(type))
          fail(declarator, "an uninitialised string member is null, which is not supported");
        addGlobal(text(field(declarator, "name")), declarator, type, initializer, isConst, false);
      }
    } else {
      unsupported(node);
    }
  }

  void parameters(TSNode list, Function &function, bool allowArgs) {
    auto params = namedChildren(list);
    for (TSNode param : params) {
      TSNode typeNode = field(param, "type");
      if (kind(param) != "parameter" || ts_node_is_null(typeNode))
        unsupported(param);
      if (allowArgs && params.size() == 1 && kind(typeNode) == "array_type")
        continue; // `string[] args`; using it fails as an unknown identifier.
      function.isEntry = false;
      for (TSNode child : namedChildren(param))
        if (kind(child) == "modifier")
          fail(child, "ref, out and params parameters are not supported");
      Type type = parseType(typeNode);
      if (!type)
        fail(param, "parameters need a type");
      function.paramNames.push_back(text(field(param, "name")));
      function.paramTypes.push_back(type);
    }
  }

  void method(TSNode node) {
    TSNode returns = field(node, "returns");
    if (ts_node_is_null(returns))
      returns = field(node, "type");
    TSNode body = field(node, "body");
    if (ts_node_is_null(body))
      fail(node, "methods without bodies are not supported");
    if (kind(body) == "arrow_expression_clause")
      body = namedChildren(body).at(0);

    std::string name = text(field(node, "name"));
    bool isVoid = text(returns) == "void";
    Function &function = addFunction(name);
    function.declaration = node;
    function.body = body;
    function.returnKnown = true;
    function.returnType = isVoid ? Type() : parseType(returns);
    function.isEntry = name == "Main" && isVoid;
    parameters(field(node, "parameters"), function, function.isEntry);
  }

  void localFunction(TSNode node) {
    TSNode returns = field(node, "type");
    TSNode body = field(node, "body");
    if (kind(body) == "arrow_expression_clause")
      body = namedChildren(body).at(0);
    Function &function = addFunction(text(field(node, "name")));
    function.declaration = node;
    function.body = body;
    function.returnKnown = true;
    function.returnType = text(returns) == "void" ? Type() : parseType(returns);
    parameters(field(node, "parameters"), function, false);
  }

  Type parseType(TSNode node) override {
    std::string name = text(node);
    if (name == "int" || name == "Int32")
      return intType();
    if (name == "bool" || name == "Boolean")
      return boolType();
    if (name == "string" || name == "String")
      return stringType();
    if (name == "var")
      return nullptr;
    fail(node, "unsupported type '" + name + "'");
  }

  void importLocalDeclaration(TSNode node) override {
    TSNode declaration = kind(node) == "variable_declaration" ? node : namedChildren(node).back();
    bool isConst = kind(node) != "variable_declaration" && isConstDeclaration(node);
    Type type = parseType(field(declaration, "type"));
    for (TSNode declarator : declarators(declaration))
      declareVariable(declarator, text(field(declarator, "name")), type,
                      initializerOf(declarator), isConst);
  }

  Value importCall(TSNode node) override {
    TSNode function = field(node, "function");
    auto args = argumentNodes(field(node, "arguments"));
    Location l = loc(node);

    if (kind(function) == "identifier")
      return callFunction(node, text(function), args);
    if (kind(function) != "member_access_expression")
      unsupported(node);

    TSNode objectNode = field(function, "expression");
    std::string object = text(objectNode);
    std::string method = text(field(function, "name"));
    if (isSelf(object))
      return callFunction(node, method, args);

    if (object == "Console" || object == "System.Console") {
      if (method != "WriteLine" && method != "Write")
        unsupported(node);
      if (args.size() > 1)
        fail(node, "format strings are not supported; use interpolation");
      Value value = args.empty() ? Value() : requiredExpr(args[0]);
      print(l, value, method == "WriteLine");
      return nullptr;
    }
    if (method == "ToString" && args.empty())
      return toStringValue(l, requiredExpr(objectNode));
    if (method == "Equals" && args.size() == 1) {
      Value lhs = requiredExpr(objectNode);
      Value rhs = requiredExpr(args[0]);
      if (isa<StringType>(lhs.getType()) && isa<StringType>(rhs.getType()))
        return StrEqOp::create(builder, l, boolType(), lhs, rhs);
    }
    unsupported(node);
  }

  // `$"a{x}b"` becomes a concatenation of its pieces.
  Value importInterpolated(TSNode node) override {
    Location l = loc(node);
    std::string raw = text(node);
    if (raw.rfind("$\"", 0) != 0)
      fail(node, "only plain $\"...\" interpolated strings are supported");

    Value result;
    auto append = [&](Value piece) {
      result = result ? concat(l, result, piece) : piece;
    };
    auto literal = [&](const std::string &part) {
      std::string unescaped;
      for (size_t i = 0; i < part.size(); ++i) {
        unescaped += part[i];
        if ((part[i] == '{' || part[i] == '}') && i + 1 < part.size() && part[i + 1] == part[i])
          ++i;
      }
      if (!unescaped.empty())
        append(stringLiteral(l, decodeString(node, unescaped)));
    };

    uint32_t base = ts_node_start_byte(node);
    size_t position = 2;
    for (TSNode child : namedChildren(node)) {
      if (kind(child) != "interpolation")
        continue;
      literal(raw.substr(position, ts_node_start_byte(child) - base - position));
      position = ts_node_end_byte(child) - base;
      auto parts = namedChildren(child);
      std::vector<TSNode> values;
      for (TSNode part : parts)
        if (kind(part) != "interpolation_brace")
          values.push_back(part);
      if (values.size() != 1)
        fail(child, "format specifiers are not supported in interpolated strings");
      append(toStringValue(l, requiredExpr(values[0])));
    }
    literal(raw.substr(position, raw.size() - 1 - position));
    return result ? result : stringLiteral(l, "");
  }
};

} // namespace

OwningOpRef<ModuleOp> grand::importCSharp(const std::string &source, const std::string &fileName,
                                          MLIRContext &context, Diagnostics &diagnostics) {
  CSharpImporter importer(source, fileName, context, diagnostics);
  return importer.run(tree_sitter_c_sharp(), "csharp");
}
