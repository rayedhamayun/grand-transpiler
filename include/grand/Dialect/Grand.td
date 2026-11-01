// The core `grand` dialect: the shared hub IR for all languages.
//
// grand stays close to source so emitters can produce readable code:
//  - Variables are memory-like (`var`, `load`, `store`), so names survive.
//  - Control flow is structured, and `return`, `break` and `continue` are
//    ordinary ops that may appear inside regions.
//  - An expression is a tree of single-use SSA values in evaluation order. An
//    op whose result is used is inlined at its use; anything else is a
//    statement.

include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/IR/RegionKindInterface.td"
include "mlir/Interfaces/FunctionInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def Grand_Dialect : Dialect {
  let name = "grand";
  let cppNamespace = "::grand";
  let useDefaultTypePrinterParser = 1;
}

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

class Grand_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<Grand_Dialect, name, traits> {
  let mnemonic = typeMnemonic;
}

def Grand_IntType : Grand_Type<"Int", "int"> {
  let summary = "Signed integer of a given bit width";
  let parameters = (ins "unsigned":$width);
  let assemblyFormat = "`<` $width `>`";
}

def Grand_BoolType : Grand_Type<"Bool", "bool"> {
  let summary = "Boolean";
}

def Grand_StringType : Grand_Type<"String", "string"> {
  let summary = "Immutable UTF-16 string";
}

def Grand_RefType : Grand_Type<"Ref", "ref"> {
  let summary = "Assignable location holding a value";
  let parameters = (ins "::mlir::Type":$pointee);
  let assemblyFormat = "`<` $pointee `>`";
}

def Grand_ValueType : AnyTypeOf<[Grand_IntType, Grand_BoolType, Grand_StringType]>;

//===----------------------------------------------------------------------===//
// Op helpers
//===----------------------------------------------------------------------===//

class Grand_Op<string mnemonic, list<Trait> traits = []>
    : Op<Grand_Dialect, mnemonic, traits>;

class Grand_BinaryIntOp<string mnemonic>
    : Grand_Op<mnemonic, [Pure, SameOperandsAndResultType]> {
  let arguments = (ins Grand_IntType:$lhs, Grand_IntType:$rhs);
  let results = (outs Grand_IntType:$result);
  let assemblyFormat = "$lhs `,` $rhs attr-dict `:` type($result)";
}

class Grand_UnaryIntOp<string mnemonic>
    : Grand_Op<mnemonic, [Pure, SameOperandsAndResultType]> {
  let arguments = (ins Grand_IntType:$value);
  let results = (outs Grand_IntType:$result);
  let assemblyFormat = "$value attr-dict `:` type($result)";
}

// Short-circuiting: the rhs region runs only when needed.
class Grand_LogicalOp<string mnemonic>
    : Grand_Op<mnemonic, [NoTerminator, SingleBlock]> {
  let arguments = (ins Grand_BoolType:$lhs);
  let results = (outs Grand_BoolType:$result);
  let regions = (region SizedRegion<1>:$rhs);
  let assemblyFormat = "$lhs $rhs attr-dict";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Structure
//===----------------------------------------------------------------------===//

def Grand_FuncOp : Grand_Op<"func", [
    FunctionOpInterface, IsolatedFromAbove, NoTerminator, Symbol]> {
  let summary = "Function; `entry` marks the program entry point";
  let arguments = (ins
    SymbolNameAttr:$sym_name,
    TypeAttrOf<FunctionType>:$function_type,
    OptionalAttr<DictArrayAttr>:$arg_attrs,
    OptionalAttr<DictArrayAttr>:$res_attrs,
    UnitAttr:$entry,
    OptionalAttr<StrAttr>:$source_name);
  let regions = (region MaxSizedRegion<1>:$body);
  let hasCustomAssemblyFormat = 1;
  let hasVerifier = 1;
  let extraClassDeclaration = [{
    ::llvm::ArrayRef<::mlir::Type> getArgumentTypes() {
      return getFunctionType().getInputs();
    }
    ::llvm::ArrayRef<::mlir::Type> getResultTypes() {
      return getFunctionType().getResults();
    }
    ::mlir::Region *getCallableRegion() { return &getBody(); }
  }];
}

def Grand_GlobalOp : Grand_Op<"global", [IsolatedFromAbove, NoTerminator, Symbol]> {
  let summary = "Module-level variable; the optional region computes its initial value";
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttrOf<Grand_ValueType>:$type,
                       UnitAttr:$is_const);
  let regions = (region MaxSizedRegion<1>:$init);
  let assemblyFormat = "(`const` $is_const^)? $sym_name `:` $type $init attr-dict";
  let hasVerifier = 1;
}

def Grand_ReturnOp : Grand_Op<"return"> {
  let arguments = (ins Optional<Grand_ValueType>:$value);
  let assemblyFormat = "($value^ `:` type($value))? attr-dict";
  let hasVerifier = 1;
}

def Grand_CallOp : Grand_Op<"call"> {
  let arguments = (ins FlatSymbolRefAttr:$callee, Variadic<Grand_ValueType>:$args);
  let results = (outs Optional<Grand_ValueType>:$result);
  let assemblyFormat = "$callee `(` $args `)` attr-dict `:` functional-type($args, $result)";
}

// Carries the value of a condition, ternary arm, logical rhs or global
// initialiser out of its region. Unrelated to generators.
def Grand_RegionResultOp : Grand_Op<"region_result"> {
  let arguments = (ins Grand_ValueType:$value);
  let assemblyFormat = "$value `:` type($value) attr-dict";
}

//===----------------------------------------------------------------------===//
// Variables
//===----------------------------------------------------------------------===//

def Grand_VarOp : Grand_Op<"var"> {
  let summary = "Local variable; `is_param` binds a function argument to its name";
  let arguments = (ins StrAttr:$name, Optional<Grand_ValueType>:$init,
                       UnitAttr:$is_const, UnitAttr:$is_param);
  let results = (outs Grand_RefType:$ref);
  let assemblyFormat = [{
    $name (`const` $is_const^)? (`param` $is_param^)?
    (`init` $init^ `:` type($init))? attr-dict `:` type($ref)
  }];
  let hasVerifier = 1;
}

def Grand_GlobalRefOp : Grand_Op<"global_ref", [Pure]> {
  let arguments = (ins FlatSymbolRefAttr:$global);
  let results = (outs Grand_RefType:$ref);
  let assemblyFormat = "$global attr-dict `:` type($ref)";
}

def Grand_LoadOp : Grand_Op<"load"> {
  let arguments = (ins Grand_RefType:$ref);
  let results = (outs Grand_ValueType:$result);
  let assemblyFormat = "$ref attr-dict `:` type($ref)  `->` type($result)";
  let hasVerifier = 1;
}

def Grand_StoreOp : Grand_Op<"store", [AllTypesMatch<["value", "result"]>]> {
  let summary = "Assignment; the result is the assigned value";
  let arguments = (ins Grand_RefType:$ref, Grand_ValueType:$value);
  let results = (outs Grand_ValueType:$result);
  let assemblyFormat = "$value `to` $ref attr-dict `:` type($ref) `->` type($result)";
  let hasVerifier = 1;
}

def Grand_CompoundAssignOp : Grand_Op<"compound_assign"> {
  let summary = "`x op= v`; kind is the name of an arithmetic or bitwise op, or concat";
  let arguments = (ins StrAttr:$kind, Grand_RefType:$ref, Grand_ValueType:$value);
  let results = (outs Grand_ValueType:$result);
  let assemblyFormat = "$kind $ref `,` $value attr-dict `:` type($ref) `,` type($value) `->` type($result)";
  let hasVerifier = 1;
}

def Grand_IncDecOp : Grand_Op<"incdec"> {
  let summary = "`++`/`--`; delta is +1 or -1, `prefix` selects the result value";
  let arguments = (ins Grand_RefType:$ref, SI32Attr:$delta, UnitAttr:$prefix);
  let results = (outs Grand_IntType:$result);
  let assemblyFormat = "$ref $delta (`prefix` $prefix^)? attr-dict `:` type($ref) `->` type($result)";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Literals
//===----------------------------------------------------------------------===//

def Grand_IntLitOp : Grand_Op<"int_lit", [Pure]> {
  let arguments = (ins SI64Attr:$value);
  let results = (outs Grand_IntType:$result);
  let assemblyFormat = "$value attr-dict `:` type($result)";
}

def Grand_BoolLitOp : Grand_Op<"bool_lit", [Pure]> {
  let arguments = (ins BoolAttr:$value);
  let results = (outs Grand_BoolType:$result);
  let assemblyFormat = "$value attr-dict";
}

def Grand_StrLitOp : Grand_Op<"str_lit", [Pure]> {
  let arguments = (ins StrAttr:$value);
  let results = (outs Grand_StringType:$result);
  let assemblyFormat = "$value attr-dict";
}

//===----------------------------------------------------------------------===//
// Arithmetic and bitwise
//===----------------------------------------------------------------------===//

def Grand_AddOp : Grand_BinaryIntOp<"add">;
def Grand_SubOp : Grand_BinaryIntOp<"sub">;
def Grand_MulOp : Grand_BinaryIntOp<"mul">;
def Grand_DivOp : Grand_BinaryIntOp<"div"> { let summary = "Truncating integer division"; }
def Grand_RemOp : Grand_BinaryIntOp<"rem"> { let summary = "Remainder with the dividend's sign"; }
def Grand_BitAndOp : Grand_BinaryIntOp<"bit_and">;
def Grand_BitOrOp : Grand_BinaryIntOp<"bit_or">;
def Grand_BitXorOp : Grand_BinaryIntOp<"bit_xor">;
def Grand_ShlOp : Grand_BinaryIntOp<"shl">;
def Grand_ShrOp : Grand_BinaryIntOp<"shr"> { let summary = "Arithmetic shift right"; }
def Grand_UShrOp : Grand_BinaryIntOp<"ushr"> { let summary = "Logical shift right"; }
def Grand_NegOp : Grand_UnaryIntOp<"neg">;
def Grand_BitNotOp : Grand_UnaryIntOp<"bit_not">;

//===----------------------------------------------------------------------===//
// Logic and comparison
//===----------------------------------------------------------------------===//

def Grand_CmpOp : Grand_Op<"cmp", [Pure, AllTypesMatch<["lhs", "rhs"]>]> {
  let summary = "Comparison; pred is one of eq, ne, lt, le, gt, ge";
  let arguments = (ins StrAttr:$pred, Grand_ValueType:$lhs, Grand_ValueType:$rhs);
  let results = (outs Grand_BoolType:$result);
  let assemblyFormat = "$pred $lhs `,` $rhs attr-dict `:` type($lhs)";
  let builders = [
    OpBuilder<(ins "::llvm::StringRef":$pred, "::mlir::Value":$lhs, "::mlir::Value":$rhs), [{
      build($_builder, $_state, $_builder.getType<BoolType>(), pred, lhs, rhs);
    }]>
  ];
  let hasVerifier = 1;
}

def Grand_NotOp : Grand_Op<"not", [Pure]> {
  let arguments = (ins Grand_BoolType:$value);
  let results = (outs Grand_BoolType:$result);
  let assemblyFormat = "$value attr-dict";
}

def Grand_LogicalAndOp : Grand_LogicalOp<"logical_and">;
def Grand_LogicalOrOp : Grand_LogicalOp<"logical_or">;

def Grand_TernaryOp : Grand_Op<"ternary", [NoTerminator]> {
  let arguments = (ins Grand_BoolType:$cond);
  let results = (outs Grand_ValueType:$result);
  let regions = (region SizedRegion<1>:$thenRegion, SizedRegion<1>:$elseRegion);
  let assemblyFormat = "$cond `:` type($result) $thenRegion `else` $elseRegion attr-dict";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Control flow
//===----------------------------------------------------------------------===//

def Grand_IfOp : Grand_Op<"if", [NoTerminator]> {
  let arguments = (ins Grand_BoolType:$cond);
  let regions = (region SizedRegion<1>:$thenRegion, MaxSizedRegion<1>:$elseRegion);
  let assemblyFormat = "$cond $thenRegion `else` $elseRegion attr-dict";
}

def Grand_WhileOp : Grand_Op<"while", [NoTerminator]> {
  let regions = (region SizedRegion<1>:$condRegion, MaxSizedRegion<1>:$bodyRegion);
  let assemblyFormat = "$condRegion `do` $bodyRegion attr-dict";
  let hasVerifier = 1;
}

def Grand_DoWhileOp : Grand_Op<"do_while", [NoTerminator]> {
  let regions = (region MaxSizedRegion<1>:$bodyRegion, SizedRegion<1>:$condRegion);
  let assemblyFormat = "$bodyRegion `while` $condRegion attr-dict";
  let hasVerifier = 1;
}

// A C-style for. Variables declared by the init clause must be visible to the
// condition, step and body, so those live in a for_loop nested after them.
def Grand_ForOp : Grand_Op<"for", [NoTerminator, SingleBlock]> {
  let summary = "Scope of a C-style for; its region ends in a for_loop";
  let regions = (region SizedRegion<1>:$initRegion);
  let assemblyFormat = "$initRegion attr-dict";
  let hasVerifier = 1;
  let extraClassDeclaration = [{
    ForLoopOp getLoop();
  }];
}

def Grand_ForLoopOp : Grand_Op<"for_loop", [NoTerminator]> {
  let summary = "Condition, step and body of a for; an empty condition loops forever";
  let regions = (region MaxSizedRegion<1>:$condRegion, MaxSizedRegion<1>:$stepRegion,
                        MaxSizedRegion<1>:$bodyRegion);
  let assemblyFormat = "`cond` $condRegion `step` $stepRegion `do` $bodyRegion attr-dict";
  let hasVerifier = 1;
}

def Grand_BreakOp : Grand_Op<"break"> {
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def Grand_ContinueOp : Grand_Op<"continue"> {
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Strings and I/O
//===----------------------------------------------------------------------===//

def Grand_StrConcatOp : Grand_Op<"str_concat", [Pure]> {
  let arguments = (ins Grand_StringType:$lhs, Grand_StringType:$rhs);
  let results = (outs Grand_StringType:$result);
  let assemblyFormat = "$lhs `,` $rhs attr-dict";
}

def Grand_StrLenOp : Grand_Op<"str_len", [Pure]> {
  let arguments = (ins Grand_StringType:$value);
  let results = (outs Grand_IntType:$result);
  let assemblyFormat = "$value attr-dict `:` type($result)";
}

def Grand_StrEqOp : Grand_Op<"str_eq", [Pure]> {
  let arguments = (ins Grand_StringType:$lhs, Grand_StringType:$rhs);
  let results = (outs Grand_BoolType:$result);
  let assemblyFormat = "$lhs `,` $rhs attr-dict";
}

def Grand_ToStringOp : Grand_Op<"to_string", [Pure]> {
  let summary = "Convert to string; bool_style is `lower` or `pascal` for bool operands";
  let arguments = (ins Grand_ValueType:$value, OptionalAttr<StrAttr>:$bool_style);
  let results = (outs Grand_StringType:$result);
  let assemblyFormat = "$value attr-dict `:` type($value)";
  let hasVerifier = 1;
}

def Grand_PrintOp : Grand_Op<"print"> {
  let summary = "Write a string to stdout, optionally followed by a newline";
  let arguments = (ins Optional<Grand_StringType>:$value, UnitAttr:$newline);
  let assemblyFormat = "($value^)? (`newline` $newline^)? attr-dict";
}
