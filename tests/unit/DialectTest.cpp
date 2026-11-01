#include "grand/Dialect/GrandDialect.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"

#include <gtest/gtest.h>
#include <string>

namespace {

class DialectTest : public ::testing::Test {
protected:
  DialectTest() : handler(&ctx, [this](mlir::Diagnostic &d) {
    lastError = d.str();
    return mlir::success();
  }) {
    ctx.loadDialect<grand::GrandDialect>();
  }

  mlir::OwningOpRef<mlir::ModuleOp> parse(const std::string &ir) {
    return mlir::parseSourceString<mlir::ModuleOp>(ir, &ctx);
  }

  std::string print(mlir::ModuleOp module) {
    std::string out;
    llvm::raw_string_ostream os(out);
    module.print(os);
    return out;
  }

  void expectRoundTrip(const std::string &ir) {
    auto first = parse(ir);
    ASSERT_TRUE(first) << lastError;
    std::string printed = print(*first);
    auto second = parse(printed);
    ASSERT_TRUE(second) << lastError << "\n" << printed;
    EXPECT_EQ(printed, print(*second));
  }

  void expectRejected(const std::string &ir, const std::string &message) {
    EXPECT_FALSE(parse(ir)) << "expected rejection: " << message;
    EXPECT_NE(lastError.find(message), std::string::npos) << lastError;
  }

  mlir::MLIRContext ctx;
  mlir::ScopedDiagnosticHandler handler;
  std::string lastError;
};

TEST_F(DialectTest, FunctionWithParamsAndArithmetic) {
  expectRoundTrip(R"(
    grand.func @add(%a: !grand.int<32>, %b: !grand.int<32>) -> !grand.int<32> {
      %ra = grand.var "a" param init %a : !grand.int<32> : !grand.ref<!grand.int<32>>
      %rb = grand.var "b" param init %b : !grand.int<32> : !grand.ref<!grand.int<32>>
      %0 = grand.load %ra : !grand.ref<!grand.int<32>> -> !grand.int<32>
      %1 = grand.load %rb : !grand.ref<!grand.int<32>> -> !grand.int<32>
      %2 = grand.add %0, %1 : !grand.int<32>
      grand.return %2 : !grand.int<32>
    })");
}

TEST_F(DialectTest, GlobalsAndEntry) {
  expectRoundTrip(R"(
    grand.global const @limit : !grand.int<32> {
      %0 = grand.int_lit 10 : !grand.int<32>
      grand.region_result %0 : !grand.int<32>
    }
    grand.global @count : !grand.int<32> {
    }
    grand.func @main() attributes {entry} {
      %r = grand.global_ref @count : !grand.ref<!grand.int<32>>
      %0 = grand.incdec %r 1 prefix : !grand.ref<!grand.int<32>> -> !grand.int<32>
      grand.print newline
    })");
}

TEST_F(DialectTest, StructuredControlFlowWithEarlyExits) {
  expectRoundTrip(R"(
    grand.func @f(%n: !grand.int<32>) -> !grand.int<32> {
      %r = grand.var "n" param init %n : !grand.int<32> : !grand.ref<!grand.int<32>>
      grand.for {
        %z = grand.int_lit 0 : !grand.int<32>
        %i = grand.var "i" init %z : !grand.int<32> : !grand.ref<!grand.int<32>>
        grand.for_loop cond {
          %t = grand.load %i : !grand.ref<!grand.int<32>> -> !grand.int<32>
          %u = grand.cmp "lt" %t, %t : !grand.int<32>
          grand.region_result %u : !grand.bool
        } step {
        } do {
          %c = grand.bool_lit false
          grand.if %c {
            grand.break
          } else {
            grand.continue
          }
        }
      }
      %v = grand.load %r : !grand.ref<!grand.int<32>> -> !grand.int<32>
      grand.return %v : !grand.int<32>
    })");
}

TEST_F(DialectTest, ShortCircuitAndTernary) {
  expectRoundTrip(R"(
    grand.func @f(%a: !grand.bool) -> !grand.string {
      %0 = grand.logical_and %a {
        %1 = grand.bool_lit true
        grand.region_result %1 : !grand.bool
      }
      %2 = grand.ternary %0 : !grand.string {
        %3 = grand.str_lit "yes"
        grand.region_result %3 : !grand.string
      } else {
        %4 = grand.str_lit "no"
        grand.region_result %4 : !grand.string
      }
      grand.return %2 : !grand.string
    })");
}

TEST_F(DialectTest, StringsAndPrint) {
  expectRoundTrip(R"(
    grand.func @f(%b: !grand.bool, %n: !grand.int<32>) {
      %0 = grand.to_string %b {bool_style = "pascal"} : !grand.bool
      %1 = grand.to_string %n : !grand.int<32>
      %2 = grand.str_concat %0, %1
      %3 = grand.str_len %2 : !grand.int<32>
      %4 = grand.to_string %3 : !grand.int<32>
      grand.print %4 newline
    })");
}

TEST_F(DialectTest, RejectsForWithoutLoop) {
  expectRejected(R"(grand.func @f() { grand.for { %0 = grand.bool_lit true } })",
                 "must end in for_loop");
}

TEST_F(DialectTest, RejectsBreakOutsideLoop) {
  expectRejected(R"(grand.func @f() { grand.break })", "must be inside a loop");
}

TEST_F(DialectTest, RejectsWrongReturnType) {
  expectRejected(R"(
    grand.func @f() -> !grand.int<32> {
      %0 = grand.bool_lit true
      grand.return %0 : !grand.bool
    })", "expected");
}

TEST_F(DialectTest, RejectsMissingReturnValue) {
  expectRejected(R"(grand.func @f() -> !grand.bool { grand.return })",
                 "does not match the function's return type");
}

TEST_F(DialectTest, RejectsBoolToStringWithoutStyle) {
  expectRejected(R"(
    grand.func @f(%b: !grand.bool) {
      %0 = grand.to_string %b : !grand.bool
    })", "requires bool_style");
}

TEST_F(DialectTest, RejectsStringOrderingViaCmp) {
  expectRejected(R"(
    grand.func @f(%a: !grand.string, %b: !grand.string) {
      %0 = grand.cmp "eq" %a, %b : !grand.string
    })", "str_eq");
}

TEST_F(DialectTest, RejectsNonBooleanLoopCondition) {
  expectRejected(R"(
    grand.func @f() {
      grand.while {
        %0 = grand.int_lit 1 : !grand.int<32>
        grand.region_result %0 : !grand.int<32>
      } do {
      }
    })", "cond region must produce");
}

TEST_F(DialectTest, RejectsLogicalWithoutResult) {
  expectRejected(R"(
    grand.func @f(%a: !grand.bool) {
      %0 = grand.logical_or %a {
      }
    })", "must end in region_result");
}

} // namespace
