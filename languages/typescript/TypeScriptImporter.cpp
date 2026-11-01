#include "languages/Plugins.h"

#include "grand/Import/CStyleImporter.h"

extern "C" const TSLanguage *tree_sitter_typescript(void);

using namespace mlir;
using namespace grand;

namespace {

class TypeScriptImporter : public CStyleImporter {
public:
  using CStyleImporter::CStyleImporter;

protected:
  std::string boolStyle() const override { return "lower"; }
  bool divisionCanBeFractional() const override { return true; }

  void collectDeclarations(TSNode root) override {
    std::vector<TSNode> statements;
    std::set<std::string> usedInFunctions;

    for (TSNode child : namedChildren(root)) {
      if (kind(child) == "export_statement") {
        TSNode declaration = field(child, "declaration");
        if (ts_node_is_null(declaration))
          unsupported(child);
        child = declaration;
      }
      if (kind(child) == "function_declaration") {
        function(child);
        collectIdentifiers(field(child, "body"), usedInFunctions);
      } else {
        statements.push_back(child);
      }
    }
    if (statements.empty())
      return;

    // Top-level variables that functions read become globals.
    std::vector<TSNode> declarations;
    for (TSNode statement : statements)
      if (kind(statement) == "lexical_declaration" || kind(statement) == "variable_declaration")
        for (TSNode declarator : declarators(statement))
          declarations.push_back(declarator);
    std::vector<TSNode> initializers;
    for (TSNode declarator : declarations)
      initializers.push_back(field(declarator, "value"));
    promoteDependencies(initializers, usedInFunctions,
                        [&](size_t i) { return text(field(declarations[i], "name")); });

    bool sawStatement = false;
    bool needsEntry = false;
    std::vector<TSNode> executable;
    for (TSNode statement : statements) {
      std::string k = kind(statement);
      if (k != "lexical_declaration" && k != "variable_declaration") {
        sawStatement = true;
        needsEntry = true;
        executable.push_back(statement);
        continue;
      }
      bool isConst = text(statement).rfind("const", 0) == 0;
      for (TSNode declarator : declarators(statement)) {
        std::string name = text(field(declarator, "name"));
        if (!usedInFunctions.count(name)) {
          needsEntry = true;
          continue;
        }
        TSNode typeNode = field(declarator, "type");
        Type type = ts_node_is_null(typeNode) ? Type() : parseType(typeNode);
        addGlobal(name, declarator, type, field(declarator, "value"), isConst, sawStatement);
        needsEntry = needsEntry || sawStatement;
      }
      executable.push_back(statement);
    }
    if (!needsEntry)
      return;

    Function &entry = addFunction("__entry");
    entry.declaration = statements[0];
    entry.statements = executable;
    entry.isEntry = true;
    entry.isTopLevel = true;
    entry.returnKnown = true;
  }

  std::vector<TSNode> declarators(TSNode declaration) {
    std::vector<TSNode> out;
    for (TSNode child : namedChildren(declaration))
      if (kind(child) == "variable_declarator")
        out.push_back(child);
    return out;
  }

  void function(TSNode node) {
    Function &function = addFunction(text(field(node, "name")));
    function.declaration = node;
    function.body = field(node, "body");
    for (TSNode param : namedChildren(field(node, "parameters"))) {
      if (kind(param) != "required_parameter")
        unsupported(param);
      TSNode typeNode = field(param, "type");
      if (ts_node_is_null(typeNode))
        fail(param, "parameters need a type annotation");
      function.paramNames.push_back(text(field(param, "pattern")));
      function.paramTypes.push_back(parseType(typeNode));
    }
    TSNode returns = field(node, "return_type");
    if (!ts_node_is_null(returns)) {
      function.returnKnown = true;
      function.returnType = isVoid(returns) ? Type() : parseType(returns);
    }
  }

  static std::string annotationText(const std::string &annotation) {
    size_t start = annotation.find_first_not_of(": \t");
    return start == std::string::npos ? "" : annotation.substr(start);
  }

  bool isVoid(TSNode node) { return annotationText(text(node)) == "void"; }

  Type parseType(TSNode node) override {
    std::string name = annotationText(text(node));
    if (name == "number")
      return intType();
    if (name == "boolean")
      return boolType();
    if (name == "string")
      return stringType();
    fail(node, "unsupported type '" + name + "'");
  }

  void importLocalDeclaration(TSNode node) override {
    bool isConst = text(node).rfind("const", 0) == 0;
    for (TSNode declarator : declarators(node)) {
      TSNode nameNode = field(declarator, "name");
      if (kind(nameNode) != "identifier")
        unsupported(nameNode);
      TSNode typeNode = field(declarator, "type");
      Type type = ts_node_is_null(typeNode) ? Type() : parseType(typeNode);
      declareVariable(declarator, text(nameNode), type, field(declarator, "value"), isConst);
    }
  }

  // `Math.trunc(a / b)` is integer division; bare `/` is assumed exact.
  Value truncate(TSNode node, TSNode argument) {
    TSNode inner = unwrapParens(argument);
    if (kind(inner) == "binary_expression" && operatorOf(inner) == "/") {
      Value lhs = requiredExpr(field(inner, "left"));
      Value rhs = requiredExpr(field(inner, "right"));
      return binary(node, "/", lhs, rhs);
    }
    return requiredExpr(argument);
  }

  Value importCall(TSNode node) override {
    TSNode callee = field(node, "function");
    auto args = argumentNodes(field(node, "arguments"));
    Location l = loc(node);

    if (kind(callee) == "identifier") {
      std::string name = text(callee);
      if (name == "String" && args.size() == 1)
        return toStringValue(l, requiredExpr(args[0]));
      return callFunction(node, name, args);
    }
    if (kind(callee) != "member_expression")
      unsupported(node);

    TSNode objectNode = field(callee, "object");
    std::string object = text(objectNode);
    std::string method = text(field(callee, "property"));

    if (object == "console" && method == "log") {
      // console.log separates its arguments with spaces.
      Value line;
      for (TSNode arg : args) {
        Value value = requiredExpr(arg);
        line = line ? concat(l, concat(l, line, stringLiteral(l, " ")), value) : value;
      }
      print(l, line, true);
      return nullptr;
    }
    if (object == "process.stdout" && method == "write" && args.size() == 1) {
      print(l, requiredExpr(args[0]), false);
      return nullptr;
    }
    if (object == "Math" && method == "trunc" && args.size() == 1)
      return truncate(node, args[0]);
    if (method == "toString" && args.empty())
      return toStringValue(l, requiredExpr(objectNode));
    unsupported(node);
  }

  // `(a >>> b) | 0` is how TypeScript spells a signed 32-bit logical shift.
  Value importSpecialBinary(TSNode node) override {
    if (operatorOf(node) != "|")
      return nullptr;
    TSNode left = unwrapParens(field(node, "left"));
    TSNode right = field(node, "right");
    if (text(right) != "0" || kind(left) != "binary_expression" || operatorOf(left) != ">>>")
      return nullptr;
    Value lhs = requiredExpr(field(left, "left"));
    Value rhs = requiredExpr(field(left, "right"));
    return binary(node, ">>>", lhs, rhs);
  }

  Value importInterpolated(TSNode node) override {
    Location l = loc(node);
    std::string raw = text(node);
    Value result;
    auto append = [&](Value piece) {
      result = result ? concat(l, result, piece) : piece;
    };
    auto literal = [&](const std::string &part) {
      if (!part.empty())
        append(stringLiteral(l, decodeString(node, part)));
    };

    uint32_t base = ts_node_start_byte(node);
    size_t position = 1;
    for (TSNode child : namedChildren(node)) {
      if (kind(child) != "template_substitution")
        continue;
      literal(raw.substr(position, ts_node_start_byte(child) - base - position));
      position = ts_node_end_byte(child) - base;
      append(toStringValue(l, requiredExpr(namedChildren(child).at(0))));
    }
    literal(raw.substr(position, raw.size() - 1 - position));
    return result ? result : stringLiteral(l, "");
  }
};

} // namespace

OwningOpRef<ModuleOp> grand::importTypeScript(const std::string &source,
                                              const std::string &fileName, MLIRContext &context,
                                              Diagnostics &diagnostics) {
  TypeScriptImporter importer(source, fileName, context, diagnostics);
  return importer.run(tree_sitter_typescript(), "typescript");
}
