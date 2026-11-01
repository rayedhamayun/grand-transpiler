#include "grand/Core/Transpiler.h"

#include <gtest/gtest.h>

#include <string>

using namespace grand;

namespace {

struct Result {
  std::string output;
  Diagnostics diagnostics;

  bool hasMessage(const std::string &text) const {
    for (const Diagnostic &diagnostic : diagnostics.all())
      if (diagnostic.message.find(text) != std::string::npos)
        return true;
    return false;
  }
};

Result translate(const std::string &from, const std::string &to, const std::string &source) {
  TranspileRequest request;
  request.source = source;
  request.fileName = "input";
  request.from = findLanguage(from);
  request.to = findLanguage(to);
  request.options.className = "Prog";
  Result result;
  result.output = transpile(request, result.diagnostics);
  return result;
}

#define EXPECT_CONTAINS(haystack, needle) EXPECT_NE((haystack).find(needle), std::string::npos) << (haystack)
#define EXPECT_LACKS(haystack, needle) EXPECT_EQ((haystack).find(needle), std::string::npos) << (haystack)

void expectError(const std::string &from, const std::string &source, const std::string &message) {
  Result result = translate(from, "java", source);
  EXPECT_TRUE(result.diagnostics.hasErrors()) << source;
  EXPECT_TRUE(result.hasMessage(message)) << source;
}

TEST(Registry, FindsLanguagesByNameAndExtension) {
  EXPECT_EQ(findLanguage("typescript"), findLanguageByExtension("ts"));
  EXPECT_EQ(findLanguage("cs"), findLanguage("csharp"));
  EXPECT_EQ(findLanguage("cobol"), nullptr);
}

TEST(Program, JavaCompactSourceFile) {
  Result r = translate("java", "typescript", R"(
    int counter = 1;
    int next() { return ++counter; }
    void main() { IO.println(next()); }
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_CONTAINS(r.output, "export let counter = 1;");
  EXPECT_CONTAINS(r.output, "export function next(): number {");
  EXPECT_CONTAINS(r.output, "console.log(next());");
}

TEST(Program, CSharpTopLevelStatementsBecomeMain) {
  Result r = translate("csharp", "java", R"(
    using System;
    int total = 2;
    int twice() { return total * 2; }
    Console.WriteLine(twice());
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_CONTAINS(r.output, "public static int total = 2;");
  EXPECT_CONTAINS(r.output, "public static void main(String[] args) {");
  EXPECT_TRUE(r.hasMessage("a driver must call"));
}

TEST(Program, DeclarationsOnlyNeedNoMain) {
  Result r = translate("typescript", "java", R"(
    const LIMIT = 3;
    let value = LIMIT + 1;
    export function get(): number { return value; }
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_LACKS(r.output, "main");
  EXPECT_CONTAINS(r.output, "public static final int LIMIT = 3;");
  EXPECT_CONTAINS(r.output, "public static int value = LIMIT + 1;");
}

TEST(Program, GlobalAfterStatementsIsAssignedInOrder) {
  Result r = translate("typescript", "java", R"(
    let a = 1;
    console.log(a);
    let b = a + 1;
    function f(): number { return b; }
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_CONTAINS(r.output, "public static int b = 0;");
  EXPECT_CONTAINS(r.output, "b = a + 1;");
}

TEST(Program, JavaThisAndClassQualifiedMembers) {
  Result r = translate("java", "csharp", R"(
    public class Main {
      int n = 1;
      int get() { return this.n + Main.n; }
    }
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_CONTAINS(r.output, "return n + n;");
}

TEST(Semantics, DivisionTruncatesInTypeScript) {
  Result r = translate("java", "typescript", R"(class P { static int f(int a, int b) {
    a /= b;
    a >>>= 1;
    return a / (b * 2) + (a >>> 2);
  } })");
  EXPECT_CONTAINS(r.output, "a = Math.trunc(a / b);");
  EXPECT_CONTAINS(r.output, "a = (a >>> 1) | 0;");
  EXPECT_CONTAINS(r.output, "return Math.trunc(a / (b * 2)) + ((a >>> 2) | 0);");
  EXPECT_TRUE(r.hasMessage("assumes no 32-bit overflow"));
}

TEST(Semantics, TypeScriptDivisionMapsBack) {
  Result r = translate("typescript", "java", R"(
    export function f(a: number, b: number): number { return Math.trunc(a / b) + ((a >>> b) | 0); }
    export function g(a: number): number { return a / 2; }
  )");
  EXPECT_CONTAINS(r.output, "return a / b + (a >>> b);");
  EXPECT_CONTAINS(r.output, "return a / 2;");
  EXPECT_TRUE(r.hasMessage("assumed to divide exactly"));
}

TEST(Semantics, TruthinessBecomesExplicit) {
  Result r = translate("typescript", "java", R"(
    export function f(n: number, s: string): boolean {
      if (n && s) { return true; }
      return !n;
    }
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_CONTAINS(r.output, "if (n != 0 && !s.equals(\"\")) {");
  EXPECT_CONTAINS(r.output, "return !(n != 0);");
}

TEST(Semantics, BoolTextKeepsSourceBehaviour) {
  Result r = translate("csharp", "java", R"(class P { static void f(bool b) {
    Console.WriteLine(b);
    Console.WriteLine("v=" + b);
  } })");
  EXPECT_CONTAINS(r.output, "System.out.println(b ? \"True\" : \"False\");");
  EXPECT_CONTAINS(r.output, "\"v=\" + (b ? \"True\" : \"False\")");

  Result back = translate("java", "csharp", R"(class P { static void f(boolean b) {
    System.out.println(b);
  } })");
  EXPECT_CONTAINS(back.output, "Console.WriteLine(b ? \"true\" : \"false\");");
}

TEST(Semantics, AdjacentInterpolationsStayStrings) {
  Result r = translate("typescript", "java", R"(
    export function f(a: number, b: number): string { return `${a}${b}`; }
  )");
  EXPECT_CONTAINS(r.output, "String.valueOf(a) + String.valueOf(b)");
}

TEST(Semantics, ConsoleLogJoinsArguments) {
  Result r = translate("typescript", "java", R"(console.log("a", 1);)");
  EXPECT_CONTAINS(r.output, "System.out.println(\"a\" + \" \" + 1);");
}

TEST(Semantics, JavaStringIdentityWarns) {
  Result r = translate("java", "csharp", R"(class P { static boolean f(String a, String b) {
    return a == b;
  } })");
  EXPECT_CONTAINS(r.output, "return a == b;");
  EXPECT_TRUE(r.hasMessage("compares references"));
}

TEST(Legalize, RemovesUnreachableCode) {
  Result r = translate("typescript", "java", R"(
    export function f(n: number): number {
      if (n > 0) { return 1; } else { return 2; }
      console.log("never");
    }
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_LACKS(r.output, "never");
}

TEST(Legalize, DropsDiscardedExpressionsButKeepsEffects) {
  Result r = translate("typescript", "java", R"(
    function g(): number { return 1; }
    export function f(a: number): void {
      a + 1;
      g() + 2;
    }
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_LACKS(r.output, "a + 1");
  EXPECT_CONTAINS(r.output, "g();");
}

TEST(Legalize, RenamesShadowedLocals) {
  Result r = translate("typescript", "java", R"(
    export function f(): number {
      const x = 1;
      { const x = 2; console.log(x); }
      return x;
    }
  )");
  EXPECT_CONTAINS(r.output, "int x_2 = 2;");
  EXPECT_CONTAINS(r.output, "System.out.println(x_2);");
  EXPECT_CONTAINS(r.output, "return x;");
}

TEST(Legalize, RenamesInnerNameReusedByLaterOuterDeclaration) {
  Result r = translate("typescript", "csharp", R"(
    export function f(a: number): void {
      if (a > 0) { const x = 1; console.log(x); }
      const x = 2;
      console.log(x);
    }
  )");
  EXPECT_CONTAINS(r.output, "int x_2 = 1;");
  EXPECT_CONTAINS(r.output, "int x = 2;");
}

TEST(Legalize, EscapesReservedWordsPerTarget) {
  Result r = translate("java", "csharp", R"(class P { static int object(int string) {
    return string;
  } })");
  EXPECT_CONTAINS(r.output, "public static int @object(int @string)");
  EXPECT_TRUE(r.hasMessage("is emitted as '@object'"));

  Result ts = translate("csharp", "typescript", R"(class P { static int @var(int @object) {
    return @object;
  } })");
  EXPECT_CONTAINS(ts.output, "export function var_(object: number): number {");
}

TEST(Legalize, SplitsOverloadsForTypeScript) {
  Result r = translate("java", "typescript", R"(class P {
    static int f(int a) { return a; }
    static String f(String a) { return a; }
    static void g() { f(1); f("x"); }
  })");
  EXPECT_CONTAINS(r.output, "export function fString(a: string): string {");
  EXPECT_CONTAINS(r.output, "fString(\"x\");");
  EXPECT_TRUE(r.hasMessage("'f' is emitted as 'fString'"));
}

TEST(Types, TypeScriptReturnTypeIsInferredThroughCalls) {
  Result r = translate("typescript", "java", R"(
    function a() { return b() + "!"; }
    function b() { return "hi"; }
  )");
  ASSERT_FALSE(r.diagnostics.hasErrors());
  EXPECT_CONTAINS(r.output, "public static String a() {");
  EXPECT_CONTAINS(r.output, "public static String b() {");
}

TEST(Types, VarIsInferred) {
  Result r = translate("java", "csharp",
                       R"(class P { static void f() { var s = "x"; var n = 1; } })");
  EXPECT_CONTAINS(r.output, "string s = \"x\";");
  EXPECT_CONTAINS(r.output, "int n = 1;");
}

TEST(Errors, ReportedWithLocation) {
  Result r = translate("java", "csharp", "class P {\n  long x = 1;\n}\n");
  ASSERT_TRUE(r.diagnostics.hasErrors());
  EXPECT_EQ(r.diagnostics.all()[0].line, 2u);
  EXPECT_EQ(r.diagnostics.all()[0].format("A.java").rfind("A.java:2:", 0), 0u);
}

TEST(Errors, UnsupportedConstructs) {
  expectError("java", "class A {} class B {}", "only one class");
  expectError("java", "class A { void f() { A a = new A(); } }", "unsupported type");
  expectError("java", "class A { void f() { int[] x; } }", "unsupported type");
  expectError("java", "class A { A() {} }", "unsupported construct");
  expectError("java", "class A { void f() { switch (1) { default: break; } } }",
              "unsupported construct");
  expectError("java", "class A { String s; }", "null");
  expectError("java", "class A { void f() { long y = 5L; } }", "unsupported type");
  expectError("java", "class A { public static void main(String[] args) { IO.println(args); } }",
              "unknown identifier 'args'");
  expectError("java", "class A { void f() { System.out.printf(\"x\"); } }",
              "unsupported construct");
  expectError("csharp", "class A { void f() { var s = Console.ReadLine(); } }",
              "unsupported construct");
  expectError("csharp", "class A { void f() { Console.WriteLine(\"{0}\", 1); } }",
              "format strings");
  expectError("typescript", "const f = () => 1;", "unsupported construct");
  expectError("typescript", "function f(): void { function g(): void {} }",
              "unsupported construct");
  expectError("typescript", "class A {}", "unsupported construct");
  expectError("typescript", "function* g(): any { yield 1; }", "unsupported construct");
  expectError("typescript", "let x = 1.5;", "32-bit integer");
  expectError("typescript", "function f(a: number): number { return a || 2; }",
              "non-booleans");
  expectError("typescript", "function f(n: number) { return n > 0 ? 1 + f(n - 1) : 0; }",
              "needs a return type annotation");
  expectError("typescript", "function f(a): void {}", "type annotation");
  expectError("typescript", "if (true) { var x = 1; } console.log(x);", "unknown identifier");
}

TEST(Errors, SyntaxErrors) {
  expectError("java", "class {", "syntax error");
}

TEST(IR, ImportedIRCanBeEmittedLater) {
  Diagnostics diagnostics;
  std::string ir = importToIR("export function f(a: number): number { return a + 1; }", "input",
                              *findLanguage("typescript"), diagnostics);
  ASSERT_FALSE(diagnostics.hasErrors());
  EXPECT_CONTAINS(ir, "grand.func @f");

  EmitOptions options;
  options.className = "Prog";
  std::string java = emitFromIR(ir, *findLanguage("java"), options, diagnostics);
  EXPECT_CONTAINS(java, "public class Prog {");
  EXPECT_CONTAINS(java, "return a + 1;");
}

} // namespace
