#include "grand/Import/CStyleImporter.h"

#include "grand/Core/Transpiler.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

using namespace mlir;
using namespace grand;

static IntegerAttr int64Attr(OpBuilder &builder, int64_t value) {
  return builder.getIntegerAttr(builder.getIntegerType(64, /*isSigned=*/true), value);
}

CStyleImporter::CStyleImporter(const std::string &source, const std::string &fileName,
                               MLIRContext &context, Diagnostics &diagnostics)
    : builder(&context), context(context), source(source), diagnostics(diagnostics),
      fileName(fileName) {}

//===----------------------------------------------------------------------===//
// CST helpers
//===----------------------------------------------------------------------===//

std::string CStyleImporter::text(TSNode node) const {
  uint32_t start = ts_node_start_byte(node);
  std::string value = source.substr(start, ts_node_end_byte(node) - start);
  // C# escapes reserved words as `@name`; the name itself has no `@`.
  if (kind(node) == "identifier" && !value.empty() && value[0] == '@')
    value.erase(0, 1);
  return value;
}

std::string CStyleImporter::kind(TSNode node) { return ts_node_type(node); }

TSNode CStyleImporter::field(TSNode node, const char *name) {
  return ts_node_child_by_field_name(node, name, static_cast<uint32_t>(std::strlen(name)));
}

std::vector<TSNode> CStyleImporter::fieldAll(TSNode node, const char *name) {
  std::vector<TSNode> out;
  // Separators such as commas also carry the field name.
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  if (ts_tree_cursor_goto_first_child(&cursor)) {
    do {
      const char *current = ts_tree_cursor_current_field_name(&cursor);
      TSNode child = ts_tree_cursor_current_node(&cursor);
      if (current && std::strcmp(current, name) == 0 && ts_node_is_named(child))
        out.push_back(child);
    } while (ts_tree_cursor_goto_next_sibling(&cursor));
  }
  ts_tree_cursor_delete(&cursor);
  return out;
}

std::vector<TSNode> CStyleImporter::namedChildren(TSNode node) {
  std::vector<TSNode> out;
  uint32_t count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    TSNode child = ts_node_named_child(node, i);
    if (!ts_node_is_extra(child))
      out.push_back(child);
  }
  return out;
}

// The operator token: the `operator` field, or the first anonymous child.
std::string CStyleImporter::operatorOf(TSNode node) {
  TSNode op = field(node, "operator");
  if (ts_node_is_null(op)) {
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_child(node, i);
      if (!ts_node_is_named(child)) {
        op = child;
        break;
      }
    }
  }
  return ts_node_is_null(op) ? "" : ts_node_type(op);
}

void CStyleImporter::collectIdentifiers(TSNode node, const std::string &source,
                                        std::set<std::string> &names) {
  if (kind(node) == "identifier") {
    uint32_t start = ts_node_start_byte(node);
    std::string name = source.substr(start, ts_node_end_byte(node) - start);
    names.insert(!name.empty() && name[0] == '@' ? name.substr(1) : name);
  }
  uint32_t count = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < count; ++i)
    collectIdentifiers(ts_node_named_child(node, i), source, names);
}

TSNode CStyleImporter::unwrapParens(TSNode node) {
  while (kind(node) == "parenthesized_expression") {
    auto children = namedChildren(node);
    if (children.size() != 1)
      break;
    node = children[0];
  }
  return node;
}

Location CStyleImporter::loc(TSNode node) {
  TSPoint point = ts_node_start_point(node);
  return FileLineColLoc::get(&context, fileName, point.row + 1, point.column + 1);
}

void CStyleImporter::fail(TSNode node, const std::string &message) {
  TSPoint point = ts_node_start_point(node);
  diagnostics.error(message, point.row + 1, point.column + 1);
  throw ImportFailure{};
}

void CStyleImporter::unsupported(TSNode node) {
  std::string snippet = text(node);
  snippet = snippet.substr(0, snippet.find('\n'));
  if (snippet.size() > 40)
    snippet = snippet.substr(0, 40) + "...";
  fail(node, "unsupported construct '" + kind(node) + "': " + snippet);
}

void CStyleImporter::warn(TSNode node, const std::string &message) {
  TSPoint point = ts_node_start_point(node);
  diagnostics.warning(message, point.row + 1, point.column + 1);
}

Type CStyleImporter::intType() { return IntType::get(&context, 32); }
Type CStyleImporter::boolType() { return BoolType::get(&context); }
Type CStyleImporter::stringType() { return StringType::get(&context); }

bool CStyleImporter::isSelf(const std::string &object) const {
  return object == "this" || (!className.empty() && object == className);
}

//===----------------------------------------------------------------------===//
// Top level
//===----------------------------------------------------------------------===//

OwningOpRef<ModuleOp> CStyleImporter::run(const TSLanguage *language,
                                          const std::string &languageName) {
  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, language);
  TSTree *tree =
      ts_parser_parse_string(parser, nullptr, source.data(), static_cast<uint32_t>(source.size()));
  TSNode root = ts_tree_root_node(tree);

  OwningOpRef<ModuleOp> owned = ModuleOp::create(UnknownLoc::get(&context));
  module = *owned;
  module->setAttr(kSourceLanguageAttr, builder.getStringAttr(languageName));
  builder.setInsertionPointToEnd(module.getBody());

  auto reportSyntaxError = [&](TSNode node, auto &self) -> bool {
    if (ts_node_is_error(node) || ts_node_is_missing(node)) {
      TSPoint point = ts_node_start_point(node);
      diagnostics.error("syntax error", point.row + 1, point.column + 1);
      return true;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
      if (self(ts_node_child(node, i), self))
        return true;
    return false;
  };
  reportSyntaxError(root, reportSyntaxError);

  if (!diagnostics.hasErrors()) {
    try {
      collectDeclarations(root);
    } catch (const ImportFailure &) {
    }
  }
  if (!diagnostics.hasErrors()) {
    if (!className.empty())
      module->setAttr(kClassNameAttr, builder.getStringAttr(className));
    createGlobalOps();
    for (Global *global : globalOrder)
      importGlobal(*global);
    for (Function *function : functionOrder)
      importFunction(*function);
  }

  ts_tree_delete(tree);
  ts_parser_delete(parser);
  if (diagnostics.hasErrors())
    return nullptr;
  return owned;
}

CStyleImporter::Function &CStyleImporter::addFunction(std::string name) {
  auto &overloads = functions[name];
  auto function = std::make_unique<Function>();
  function->name = name;
  function->symbol =
      overloads.empty() ? name : name + "$" + std::to_string(overloads.size());
  functionOrder.push_back(function.get());
  overloads.push_back(std::move(function));
  return *overloads.back();
}

CStyleImporter::Global &CStyleImporter::addGlobal(std::string name, TSNode declaration,
                                                  Type type, TSNode initializer, bool isConst,
                                                  bool deferred) {
  auto global = std::make_unique<Global>();
  global->name = name;
  global->declaration = declaration;
  global->type = type;
  global->initializer = initializer;
  global->isConst = isConst;
  global->deferred = deferred;
  globalOrder.push_back(global.get());
  auto &slot = globals[name];
  slot = std::move(global);
  return *slot;
}

// Creates every op up front so module order follows source order however the
// bodies end up being imported.
void CStyleImporter::createGlobalOps() {
  for (Global *global : globalOrder) {
    Type placeholder = global->type ? global->type : intType();
    global->op = GlobalOp::create(builder, loc(global->declaration), global->name, placeholder,
                                  global->isConst && !global->deferred);
  }
  for (Function *function : functionOrder) {
    auto type = builder.getFunctionType(function->paramTypes, {});
    if (function->returnKnown && function->returnType)
      type = builder.getFunctionType(function->paramTypes, function->returnType);
    function->op = FuncOp::create(builder, loc(function->declaration), function->symbol, type,
                                 ArrayAttr(), ArrayAttr(), /*entry=*/false, StringAttr());
    if (function->isEntry)
      function->op.setEntry(true);
    if (function->symbol != function->name)
      function->op.setSourceName(function->name);
    if (function->isTopLevel)
      function->op->setAttr(kTopLevelAttr, builder.getUnitAttr());
    Block *block = new Block();
    function->op.getBody().push_back(block);
    for (Type param : function->paramTypes)
      block->addArgument(param, function->op.getLoc());
  }
}

void CStyleImporter::importGlobal(Global &global) {
  if (global.state == Global::State::Done)
    return;
  if (global.state == Global::State::InProgress)
    fail(global.declaration, "initialiser of '" + global.name + "' refers to itself");
  global.state = Global::State::InProgress;

  OpBuilder::InsertionGuard guard(builder);
  auto savedScopes = std::move(scopes);
  Function *savedFunction = currentFunction;
  scopes.clear();
  scopes.emplace_back();
  currentFunction = nullptr;

  try {
    if (!ts_node_is_null(global.initializer)) {
      Region &region = global.op.getInit();
      Block *block = new Block();
      region.push_back(block);
      builder.setInsertionPointToEnd(block);
      Value value = requiredExpr(global.initializer);
      if (global.type && value.getType() != global.type)
        fail(global.initializer, "initialiser type does not match '" + global.name + "'");
      global.type = value.getType();
      global.op.setType(global.type);
      if (global.deferred) {
        // Only the type was needed; the entry function assigns the value.
        block->dropAllDefinedValueUses();
        region.getBlocks().clear();
      } else {
        RegionResultOp::create(builder, loc(global.initializer), value);
      }
    }
  } catch (const ImportFailure &) {
    // Reported; later uses still need some type.
    if (!global.type)
      global.type = intType();
  }

  scopes = std::move(savedScopes);
  currentFunction = savedFunction;
  global.state = Global::State::Done;
}

void CStyleImporter::importFunction(Function &function) {
  if (function.state != Function::State::Pending)
    return;
  function.state = Function::State::InProgress;

  OpBuilder::InsertionGuard guard(builder);
  auto savedScopes = std::move(scopes);
  Function *savedFunction = currentFunction;
  scopes.clear();
  scopes.emplace_back();
  currentFunction = &function;

  Block &block = function.op.getBody().front();
  builder.setInsertionPointToEnd(&block);
  for (size_t i = 0; i < function.paramNames.size(); ++i) {
    Value arg = block.getArgument(i);
    auto ref = VarOp::create(builder, function.op.getLoc(), RefType::get(&context, arg.getType()),
                             function.paramNames[i], arg, /*is_const=*/false, /*is_param=*/true);
    scopes.back()[function.paramNames[i]] = {ref, arg.getType()};
  }

  try {
    if (ts_node_is_null(function.body)) {
      for (TSNode node : function.statements)
        statement(node);
    } else if (kind(function.body) == "block" || kind(function.body) == "statement_block") {
      blockContents(function.body);
    } else if (function.returnKnown && !function.returnType) {
      expr(function.body);
    } else {
      Value value = requiredExpr(function.body);
      function.returnKnown = true;
      function.returnType = value.getType();
      ReturnOp::create(builder, loc(function.body), value);
    }
  } catch (const ImportFailure &) {
  }

  if (!function.returnKnown)
    function.returnKnown = true;
  auto type = function.returnType
                  ? builder.getFunctionType(function.paramTypes, function.returnType)
                  : builder.getFunctionType(function.paramTypes, {});
  function.op.setFunctionTypeAttr(TypeAttr::get(type));

  scopes = std::move(savedScopes);
  currentFunction = savedFunction;
  function.state = Function::State::Done;
}

//===----------------------------------------------------------------------===//
// Statements
//===----------------------------------------------------------------------===//

void CStyleImporter::blockContents(TSNode node) {
  for (TSNode child : namedChildren(node))
    statement(child);
}

// A body may be a block or a single statement; either way it gets a scope.
void CStyleImporter::statementBody(TSNode node) {
  pushScope();
  if (kind(node) == "block" || kind(node) == "statement_block")
    blockContents(node);
  else
    statement(node);
  popScope();
}

void CStyleImporter::importInto(Region &region, TSNode node) {
  OpBuilder::InsertionGuard guard(builder);
  if (region.empty())
    region.push_back(new Block());
  builder.setInsertionPointToEnd(&region.front());
  statementBody(node);
}

void CStyleImporter::statement(TSNode node) {
  try {
    std::string k = kind(node);
    if (k == "block" || k == "statement_block") {
      statementBody(node);
    } else if (k == "expression_statement") {
      expr(namedChildren(node).at(0));
    } else if (k == "if_statement") {
      ifStatement(node);
    } else if (k == "while_statement") {
      whileStatement(node);
    } else if (k == "do_statement") {
      doStatement(node);
    } else if (k == "for_statement") {
      forStatement(node);
    } else if (k == "return_statement") {
      returnStatement(node);
    } else if (k == "break_statement") {
      BreakOp::create(builder, loc(node));
    } else if (k == "continue_statement") {
      ContinueOp::create(builder, loc(node));
    } else if (k == "global_statement") {
      statement(namedChildren(node).at(0));
    } else if (k == "empty_statement" || k == ";") {
      return;
    } else if (k == "local_variable_declaration" || k == "local_declaration_statement" ||
               k == "lexical_declaration" || k == "variable_declaration") {
      importLocalDeclaration(node);
    } else {
      unsupported(node);
    }
  } catch (const ImportFailure &) {
    // Reported; keep going to find further errors.
  }
}

void CStyleImporter::ifStatement(TSNode node) {
  Value condition = conditionExpr(field(node, "condition"));
  auto op = IfOp::create(builder, loc(node), condition);
  importInto(op.getThenRegion(), field(node, "consequence"));
  TSNode alternative = field(node, "alternative");
  if (!ts_node_is_null(alternative)) {
    if (kind(alternative) == "else_clause")
      alternative = namedChildren(alternative).at(0);
    importInto(op.getElseRegion(), alternative);
  }
}

void CStyleImporter::regionResult(Region &region, TSNode node, bool asCondition,
                                  Type *resultType) {
  OpBuilder::InsertionGuard guard(builder);
  if (region.empty())
    region.push_back(new Block());
  builder.setInsertionPointToEnd(&region.front());
  Value value = asCondition ? conditionExpr(node) : requiredExpr(node);
  if (resultType)
    *resultType = value.getType();
  RegionResultOp::create(builder, loc(node), value);
}

void CStyleImporter::whileStatement(TSNode node) {
  auto op = WhileOp::create(builder, loc(node));
  regionResult(op.getCondRegion(), field(node, "condition"), true);
  importInto(op.getBodyRegion(), field(node, "body"));
}

void CStyleImporter::doStatement(TSNode node) {
  auto op = DoWhileOp::create(builder, loc(node));
  importInto(op.getBodyRegion(), field(node, "body"));
  regionResult(op.getCondRegion(), field(node, "condition"), true);
}

// Runs one init or update clause, which may be a declaration or expressions.
void CStyleImporter::forClause(TSNode node) {
  std::string k = kind(node);
  if (k == "empty_statement")
    return;
  if (k == "expression_statement" || k == "sequence_expression") {
    for (TSNode child : namedChildren(node))
      forClause(child);
  } else if (k == "local_variable_declaration" || k == "lexical_declaration" ||
             k == "variable_declaration") {
    importLocalDeclaration(node);
  } else {
    expr(node);
  }
}

void CStyleImporter::forStatement(TSNode node) {
  auto op = ForOp::create(builder, loc(node));
  OpBuilder::InsertionGuard guard(builder);
  op.getInitRegion().push_back(new Block());
  builder.setInsertionPointToEnd(&op.getInitRegion().front());
  pushScope();

  std::vector<TSNode> init = fieldAll(node, "init");
  for (TSNode child : fieldAll(node, "initializer"))
    init.push_back(child);
  for (TSNode child : init)
    forClause(child);
  auto loop = ForLoopOp::create(builder, loc(node));

  TSNode condition = field(node, "condition");
  loop.getCondRegion().push_back(new Block());
  if (!ts_node_is_null(condition) && kind(condition) != "empty_statement") {
    if (kind(condition) == "expression_statement")
      condition = namedChildren(condition).at(0);
    regionResult(loop.getCondRegion(), condition, true);
  }

  std::vector<TSNode> step = fieldAll(node, "update");
  for (TSNode child : fieldAll(node, "increment"))
    step.push_back(child);
  {
    OpBuilder::InsertionGuard stepGuard(builder);
    loop.getStepRegion().push_back(new Block());
    builder.setInsertionPointToEnd(&loop.getStepRegion().front());
    for (TSNode child : step)
      forClause(child);
  }

  importInto(loop.getBodyRegion(), field(node, "body"));
  popScope();
}

void CStyleImporter::returnStatement(TSNode node) {
  if (!currentFunction)
    fail(node, "return outside a function");
  auto children = namedChildren(node);
  if (children.empty()) {
    if (!currentFunction->returnKnown)
      currentFunction->returnKnown = true;
    ReturnOp::create(builder, loc(node), Value());
    return;
  }
  Value value = requiredExpr(children[0]);
  if (!currentFunction->returnKnown) {
    currentFunction->returnKnown = true;
    currentFunction->returnType = value.getType();
  }
  ReturnOp::create(builder, loc(node), value);
}

void CStyleImporter::declareVariable(TSNode node, const std::string &name, Type type,
                                     TSNode initializer, bool isConst) {
  bool hasInit = !ts_node_is_null(initializer);
  // A top-level variable promoted to a global only needs its assignment here.
  if (scopes.size() == 1 && currentFunction && currentFunction->isTopLevel &&
      globals.count(name)) {
    Global &global = *globals[name];
    if (global.deferred && hasInit) {
      Value value = requiredExpr(initializer);
      auto ref = GlobalRefOp::create(builder, loc(node), RefType::get(&context, value.getType()),
                                     name);
      StoreOp::create(builder, loc(node), ref, value);
    }
    return;
  }
  Value init = hasInit ? requiredExpr(initializer) : Value();
  if (!type) {
    if (!init)
      fail(node, "cannot infer the type of '" + name + "'; it needs an annotation");
    type = init.getType();
  }
  if (init && init.getType() != type)
    fail(node, "initialiser type does not match '" + name + "'");
  auto ref = VarOp::create(builder, loc(node), RefType::get(&context, type), name, init, isConst,
                           /*is_param=*/false);
  scopes.back()[name] = {ref, type};
}

//===----------------------------------------------------------------------===//
// Expressions
//===----------------------------------------------------------------------===//

Value CStyleImporter::requiredExpr(TSNode node) {
  Value value = expr(node);
  if (!value)
    fail(node, "expression has no value");
  return value;
}

Value CStyleImporter::expr(TSNode node) {
  std::string k = kind(node);
  if (k == "parenthesized_expression")
    return expr(namedChildren(node).at(0));
  if (k == "decimal_integer_literal" || k == "hex_integer_literal" || k == "binary_integer_literal" ||
      k == "integer_literal" || k == "number")
    return integerLiteral(node);
  if (k == "true" || k == "false")
    return BoolLitOp::create(builder, loc(node), boolType(), builder.getBoolAttr(k == "true"));
  if (k == "boolean_literal")
    return BoolLitOp::create(builder, loc(node), boolType(),
                             builder.getBoolAttr(text(node) == "true"));
  if (k == "string_literal" || k == "string" || k == "verbatim_string_literal")
    return stringLiteral(loc(node), decodeString(node, text(node)));
  if (k == "template_string" || k == "interpolated_string_expression")
    return importInterpolated(node);
  if (k == "identifier")
    return identifier(node);
  if (k == "binary_expression") {
    std::string op = operatorOf(node);
    if (op == "&&" || op == "||")
      return logical(node, /*coerce=*/false);
    if (op == "/")
      return divide(node, field(node, "left"), field(node, "right"));
    if (Value special = importSpecialBinary(node))
      return special;
    Value lhs = requiredExpr(field(node, "left"));
    Value rhs = requiredExpr(field(node, "right"));
    return binary(node, op, lhs, rhs);
  }
  if (k == "unary_expression" || k == "prefix_unary_expression")
    return unary(node);
  if (k == "update_expression" || k == "postfix_unary_expression")
    return update(node);
  if (k == "assignment_expression" || k == "augmented_assignment_expression")
    return assignment(node);
  if (k == "ternary_expression" || k == "conditional_expression")
    return ternary(node);
  if (k == "method_invocation" || k == "call_expression" || k == "invocation_expression")
    return importCall(node);
  if (k == "field_access" || k == "member_expression" || k == "member_access_expression")
    return member(node);
  return importLanguageExpr(node);
}

Value CStyleImporter::importLanguageExpr(TSNode node) { unsupported(node); }

Value CStyleImporter::integerLiteral(TSNode node) {
  std::string raw;
  for (char c : text(node))
    if (c != '_')
      raw += c;
  bool isHex = raw.rfind("0x", 0) == 0 || raw.rfind("0X", 0) == 0;
  bool isBinary = raw.rfind("0b", 0) == 0 || raw.rfind("0B", 0) == 0;
  std::string digits = isHex || isBinary ? raw.substr(2) : raw;
  char *end = nullptr;
  long long value = std::strtoll(digits.c_str(), &end, isHex ? 16 : isBinary ? 2 : 10);
  if (*end != '\0' || digits.empty())
    fail(node, "only 32-bit integer literals are supported, found '" + text(node) + "'");
  // Allow 2147483648 so it can be negated; hex literals may use all 32 bits.
  if (isHex || isBinary) {
    if (value > 0xFFFFFFFFLL)
      fail(node, "integer literal does not fit in 32 bits");
    value = static_cast<int32_t>(static_cast<uint32_t>(value));
  } else if (value > 2147483648LL) {
    fail(node, "integer literal does not fit in 32 bits");
  }
  return IntLitOp::create(builder, loc(node), intType(), int64Attr(builder, value));
}

Value CStyleImporter::stringLiteral(Location location, const std::string &value) {
  return StrLitOp::create(builder, location, stringType(), builder.getStringAttr(value));
}

static void appendUtf8(std::string &out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Turns a literal's source text, with or without quotes, into its value.
std::string CStyleImporter::decodeString(TSNode node, const std::string &raw) {
  std::string body = raw;
  bool verbatim = false;
  if (body.rfind("\"\"\"", 0) == 0)
    fail(node, "text blocks and raw string literals are not supported");
  if (body.rfind("@\"", 0) == 0) {
    verbatim = true;
    body = body.substr(1);
  }
  if (body.size() >= 2 && (body[0] == '"' || body[0] == '\'') && body.back() == body[0])
    body = body.substr(1, body.size() - 2);

  std::string out;
  for (size_t i = 0; i < body.size(); ++i) {
    char c = body[i];
    if (verbatim) {
      if (c == '"' && i + 1 < body.size() && body[i + 1] == '"')
        ++i;
      out += c;
      continue;
    }
    if (c != '\\' || i + 1 >= body.size()) {
      out += c;
      continue;
    }
    char e = body[++i];
    auto hex = [&](size_t count) {
      if (i + count >= body.size())
        fail(node, "malformed escape sequence");
      uint32_t value = static_cast<uint32_t>(std::strtoul(body.substr(i + 1, count).c_str(), nullptr, 16));
      i += count;
      return value;
    };
    switch (e) {
    case 'n': out += '\n'; break;
    case 't': out += '\t'; break;
    case 'r': out += '\r'; break;
    case 'b': out += '\b'; break;
    case 'f': out += '\f'; break;
    case '0': out += '\0'; break;
    case '\\': case '"': case '\'': case '`': case '$': case '{': out += e; break;
    case 'x': appendUtf8(out, hex(2)); break;
    case 'u': {
      uint32_t cp = hex(4);
      if (cp >= 0xD800 && cp < 0xDC00 && i + 2 < body.size() && body[i + 1] == '\\' &&
          body[i + 2] == 'u') {
        i += 2;
        uint32_t low = hex(4);
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
      }
      appendUtf8(out, cp);
      break;
    }
    default:
      fail(node, std::string("unsupported escape sequence \\") + e);
    }
  }
  return out;
}

Value CStyleImporter::importInterpolated(TSNode node) { unsupported(node); }

Value CStyleImporter::toStringValue(Location location, Value value) {
  if (isa<StringType>(value.getType()))
    return value;
  auto op = ToStringOp::create(builder, location, stringType(), value, StringAttr());
  if (isa<BoolType>(value.getType()))
    op.setBoolStyle(boolStyle());
  return op;
}

Value CStyleImporter::concat(Location location, Value lhs, Value rhs) {
  lhs = toStringValue(location, lhs);
  rhs = toStringValue(location, rhs);
  return StrConcatOp::create(builder, location, stringType(), lhs, rhs);
}

void CStyleImporter::print(Location location, Value value, bool newline) {
  if (value)
    value = toStringValue(location, value);
  PrintOp::create(builder, location, value, newline);
}

Value CStyleImporter::binary(TSNode node, const std::string &op, Value lhs, Value rhs) {
  Location l = loc(node);
  Type left = lhs.getType(), right = rhs.getType();
  bool anyString = isa<StringType>(left) || isa<StringType>(right);

  if (op == "+" && anyString)
    return concat(l, lhs, rhs);

  if (op == "==" || op == "===" || op == "!=" || op == "!==") {
    if (left != right)
      fail(node, "comparison between different types");
    bool negate = op[0] == '!';
    if (isa<StringType>(left)) {
      Value equal = StrEqOp::create(builder, l, boolType(), lhs, rhs);
      return negate ? Value(NotOp::create(builder, l, boolType(), equal)) : equal;
    }
    return CmpOp::create(builder, l, negate ? "ne" : "eq", lhs, rhs);
  }

  if (!isa<IntType>(left) || !isa<IntType>(right))
    fail(node, "operator '" + op + "' is only supported on integers");

  if (op == "<" || op == "<=" || op == ">" || op == ">=") {
    const char *pred = op == "<" ? "lt" : op == "<=" ? "le" : op == ">" ? "gt" : "ge";
    return CmpOp::create(builder, l, pred, lhs, rhs);
  }
  if (op == "+") return AddOp::create(builder, l, lhs, rhs);
  if (op == "-") return SubOp::create(builder, l, lhs, rhs);
  if (op == "*") return MulOp::create(builder, l, lhs, rhs);
  if (op == "/") return DivOp::create(builder, l, lhs, rhs);
  if (op == "%") return RemOp::create(builder, l, lhs, rhs);
  if (op == "&") return BitAndOp::create(builder, l, lhs, rhs);
  if (op == "|") return BitOrOp::create(builder, l, lhs, rhs);
  if (op == "^") return BitXorOp::create(builder, l, lhs, rhs);
  if (op == "<<") return ShlOp::create(builder, l, lhs, rhs);
  if (op == ">>") return ShrOp::create(builder, l, lhs, rhs);
  if (op == ">>>") return UShrOp::create(builder, l, lhs, rhs);
  fail(node, "unsupported operator '" + op + "'");
}

Value CStyleImporter::divide(TSNode node, TSNode lhs, TSNode rhs) {
  if (divisionCanBeFractional())
    warn(node, "'/' is assumed to divide exactly; use Math.trunc for integer division");
  Value left = requiredExpr(lhs);
  Value right = requiredExpr(rhs);
  return binary(node, "/", left, right);
}

Value CStyleImporter::toBoolean(TSNode node, Value value) {
  if (!value)
    fail(node, "expression has no value");
  Location l = loc(node);
  Type type = value.getType();
  if (isa<BoolType>(type))
    return value;
  if (isa<IntType>(type)) {
    Value zero = IntLitOp::create(builder, l, type, int64Attr(builder, 0));
    return CmpOp::create(builder, l, "ne", value, zero);
  }
  Value equal = StrEqOp::create(builder, l, boolType(), value, stringLiteral(l, ""));
  return NotOp::create(builder, l, boolType(), equal);
}

// An expression in boolean position. TypeScript's truthiness only applies
// here, where its value-preserving `&&` and `||` act as plain logic.
Value CStyleImporter::conditionExpr(TSNode node) {
  node = unwrapParens(node);
  std::string k = kind(node);
  if (k == "binary_expression") {
    std::string op = operatorOf(node);
    if (op == "&&" || op == "||")
      return logical(node, /*coerce=*/true);
  }
  if ((k == "unary_expression" || k == "prefix_unary_expression") && operatorOf(node) == "!") {
    TSNode operand = field(node, "argument");
    if (ts_node_is_null(operand))
      operand = field(node, "operand");
    if (ts_node_is_null(operand))
      operand = namedChildren(node).at(0);
    Value inner = conditionExpr(operand);
    return NotOp::create(builder, loc(node), boolType(), inner);
  }
  return toBoolean(node, expr(node));
}

Value CStyleImporter::logical(TSNode node, bool coerce) {
  TSNode left = field(node, "left"), right = field(node, "right");
  Value lhs = coerce ? conditionExpr(left) : requiredExpr(left);
  if (!isa<BoolType>(lhs.getType()))
    fail(left, "value-returning '&&' and '||' on non-booleans are not supported");

  auto build = [&](auto op) {
    Region &region = op.getRhs();
    region.push_back(new Block());
    OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToEnd(&region.front());
    Value rhs = coerce ? conditionExpr(right) : requiredExpr(right);
    if (!isa<BoolType>(rhs.getType()))
      fail(right, "value-returning '&&' and '||' on non-booleans are not supported");
    RegionResultOp::create(builder, loc(right), rhs);
    return Value(op.getResult());
  };
  if (operatorOf(node) == "&&")
    return build(LogicalAndOp::create(builder, loc(node), boolType(), lhs));
  return build(LogicalOrOp::create(builder, loc(node), boolType(), lhs));
}

Value CStyleImporter::ternary(TSNode node) {
  Value condition = conditionExpr(field(node, "condition"));
  // The result type is only known once the first arm has been imported.
  OperationState state(loc(node), TernaryOp::getOperationName());
  state.addOperands(condition);
  Region *thenRegion = state.addRegion();
  Region *elseRegion = state.addRegion();
  Type thenType, elseType;
  regionResult(*thenRegion, field(node, "consequence"), false, &thenType);
  regionResult(*elseRegion, field(node, "alternative"), false, &elseType);
  if (thenType != elseType)
    fail(node, "both arms of '?:' must have the same type");
  state.addTypes(thenType);
  return builder.create(state)->getResult(0);
}

Value CStyleImporter::unary(TSNode node) {
  std::string op = operatorOf(node);
  TSNode operand = field(node, "argument");
  if (ts_node_is_null(operand))
    operand = field(node, "operand");
  if (ts_node_is_null(operand))
    operand = namedChildren(node).at(0);
  Location l = loc(node);

  if (op == "++" || op == "--")
    return IncDecOp::create(builder, l, intType(), refOf(operand),
                            builder.getSI32IntegerAttr(op == "++" ? 1 : -1),
                            builder.getUnitAttr());
  if (op == "!") {
    Value inner = requiredExpr(operand);
    if (!isa<BoolType>(inner.getType()))
      inner = toBoolean(operand, inner);
    return NotOp::create(builder, l, boolType(), inner);
  }
  Value inner = requiredExpr(operand);
  if (!isa<IntType>(inner.getType()))
    fail(node, "operator '" + op + "' is only supported on integers");
  if (op == "+")
    return inner;
  if (op == "~")
    return BitNotOp::create(builder, l, inner);
  if (op != "-")
    fail(node, "unsupported operator '" + op + "'");
  // Negative literals stay literals.
  if (auto literal = inner.getDefiningOp<IntLitOp>()) {
    int64_t value = -literal.getValue();
    literal.erase();
    return IntLitOp::create(builder, l, intType(), int64Attr(builder, value));
  }
  return NegOp::create(builder, l, inner);
}

Value CStyleImporter::update(TSNode node) {
  TSNode operand = field(node, "argument");
  if (ts_node_is_null(operand))
    operand = namedChildren(node).at(0);
  std::string op = operatorOf(node);
  // Java's update_expression covers both forms, so position decides.
  bool prefix = ts_node_start_byte(operand) != ts_node_start_byte(node);
  UnitAttr prefixAttr = prefix ? builder.getUnitAttr() : UnitAttr();
  return IncDecOp::create(builder, loc(node), intType(), refOf(operand),
                          builder.getSI32IntegerAttr(op == "++" ? 1 : -1), prefixAttr);
}

Value CStyleImporter::assignment(TSNode node) {
  Location l = loc(node);
  TSNode left = field(node, "left"), right = field(node, "right");
  std::string op = operatorOf(node);
  Value ref = refOf(left);
  Value value = requiredExpr(right);
  Type target = cast<RefType>(ref.getType()).getPointee();
  if (op == "=") {
    if (value.getType() != target)
      fail(node, "assignment changes the variable's type");
    return StoreOp::create(builder, l, ref, value);
  }

  std::string base = op.substr(0, op.size() - 1);
  if (base == "/" && divisionCanBeFractional())
    warn(node, "'/=' is assumed to divide exactly");
  if (isa<StringType>(target) && base == "+") {
    value = toStringValue(l, value);
    return CompoundAssignOp::create(builder, l, target, "concat", ref, value);
  }
  if (!isa<IntType>(target) || !isa<IntType>(value.getType()))
    fail(node, "operator '" + op + "' is only supported on integers");
  static const std::map<std::string, const char *> kinds = {
      {"+", "add"},     {"-", "sub"},    {"*", "mul"},     {"/", "div"},
      {"%", "rem"},     {"&", "bit_and"}, {"|", "bit_or"}, {"^", "bit_xor"},
      {"<<", "shl"},    {">>", "shr"},   {">>>", "ushr"}};
  auto found = kinds.find(base);
  if (found == kinds.end())
    fail(node, "unsupported operator '" + op + "'");
  return CompoundAssignOp::create(builder, l, target, found->second, ref, value);
}

Value CStyleImporter::lookupRef(TSNode node, const std::string &name) {
  for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
    auto found = scope->find(name);
    if (found != scope->end())
      return found->second.ref;
  }
  auto global = globals.find(name);
  if (global == globals.end())
    fail(node, "unknown identifier '" + name + "'");
  importGlobal(*global->second);
  return GlobalRefOp::create(builder, loc(node),
                             RefType::get(&context, global->second->type), name);
}

void CStyleImporter::promoteDependencies(const std::vector<TSNode> &initializers,
                                         std::set<std::string> &used,
                                         const std::function<std::string(size_t)> &nameOf) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < initializers.size(); ++i) {
      if (!used.count(nameOf(i)) || ts_node_is_null(initializers[i]))
        continue;
      size_t before = used.size();
      collectIdentifiers(initializers[i], used);
      changed = changed || used.size() != before;
    }
  }
}

Value CStyleImporter::identifier(TSNode node) {
  Value ref = lookupRef(node, text(node));
  Type type = cast<RefType>(ref.getType()).getPointee();
  return LoadOp::create(builder, loc(node), type, ref);
}

Value CStyleImporter::refOf(TSNode node) {
  node = unwrapParens(node);
  std::string k = kind(node);
  if (k == "identifier")
    return lookupRef(node, text(node));
  if (k == "field_access" || k == "member_expression" || k == "member_access_expression") {
    TSNode object = field(node, "object");
    if (ts_node_is_null(object))
      object = field(node, "expression");
    TSNode name = field(node, "field");
    if (ts_node_is_null(name))
      name = field(node, "property");
    if (ts_node_is_null(name))
      name = field(node, "name");
    if (isSelf(text(object)) && isGlobal(text(name)))
      return lookupRef(name, text(name));
  }
  unsupported(node);
}

Value CStyleImporter::member(TSNode node) {
  TSNode object = field(node, "object");
  if (ts_node_is_null(object))
    object = field(node, "expression");
  TSNode name = field(node, "field");
  if (ts_node_is_null(name))
    name = field(node, "property");
  if (ts_node_is_null(name))
    name = field(node, "name");
  std::string property = text(name);

  if (isSelf(text(object)) && isGlobal(property)) {
    Value ref = lookupRef(name, property);
    return LoadOp::create(builder, loc(node), cast<RefType>(ref.getType()).getPointee(), ref);
  }
  if (property == "length" || property == "Length") {
    Value value = requiredExpr(object);
    if (isa<StringType>(value.getType()))
      return StrLenOp::create(builder, loc(node), intType(), value);
  }
  unsupported(node);
}

std::vector<TSNode> CStyleImporter::argumentNodes(TSNode argumentList) {
  std::vector<TSNode> out;
  for (TSNode child : namedChildren(argumentList)) {
    if (kind(child) == "argument") {
      if (!ts_node_is_null(field(child, "name")))
        fail(child, "named arguments are not supported");
      auto inner = namedChildren(child);
      if (inner.size() != 1)
        fail(child, "ref, out and in arguments are not supported");
      out.push_back(inner[0]);
    } else {
      out.push_back(child);
    }
  }
  return out;
}

Value CStyleImporter::callFunction(TSNode node, const std::string &name,
                                   std::vector<TSNode> arguments) {
  auto found = functions.find(name);
  if (found == functions.end())
    fail(node, "unknown function '" + name + "'");

  std::vector<Value> values;
  for (TSNode argument : arguments)
    values.push_back(requiredExpr(argument));

  Function *target = nullptr;
  for (auto &candidate : found->second) {
    if (candidate->paramTypes.size() != values.size())
      continue;
    bool matches = true;
    for (size_t i = 0; i < values.size(); ++i)
      matches = matches && candidate->paramTypes[i] == values[i].getType();
    if (matches) {
      target = candidate.get();
      break;
    }
  }
  if (!target)
    fail(node, "no function '" + name + "' accepts these arguments");

  if (!target->returnKnown) {
    if (target->state == Function::State::InProgress)
      fail(node, "recursive function '" + name + "' needs a return type annotation");
    importFunction(*target);
  }
  Type result = target->returnType;
  auto call = CallOp::create(builder, loc(node), result ? TypeRange(result) : TypeRange(),
                             target->symbol, ValueRange(values));
  return result ? call.getResult() : Value();
}
