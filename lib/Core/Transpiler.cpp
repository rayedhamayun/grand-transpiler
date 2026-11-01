#include "grand/Core/Transpiler.h"

#include "grand/Dialect/GrandDialect.h"

#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/raw_ostream.h"

using namespace grand;

void grand::registerGrandDialect(mlir::MLIRContext &context) {
  context.loadDialect<GrandDialect>();
}

namespace {

// Routes MLIR diagnostics into `diagnostics` while in scope.
class MlirDiagnosticBridge {
public:
  MlirDiagnosticBridge(mlir::MLIRContext &context, Diagnostics &diagnostics)
      : handler(&context, [&diagnostics](mlir::Diagnostic &d) {
          diagnostics.error("internal IR error: " + d.str());
          return mlir::success();
        }) {}

private:
  mlir::ScopedDiagnosticHandler handler;
};

} // namespace

std::string grand::transpile(const TranspileRequest &request, Diagnostics &diagnostics) {
  mlir::MLIRContext context;
  registerGrandDialect(context);
  MlirDiagnosticBridge bridge(context, diagnostics);

  auto module =
      request.from->importSource(request.source, request.fileName, context, diagnostics);
  if (!module || diagnostics.hasErrors())
    return {};
  if (mlir::failed(mlir::verify(*module)))
    return {};
  legalizeForTarget(*module, request.to->profile(), diagnostics);
  if (diagnostics.hasErrors() || mlir::failed(mlir::verify(*module)))
    return {};
  return request.to->emit(*module, request.options);
}

std::string grand::emitFromIR(const std::string &ir, const LanguagePlugin &to,
                              const EmitOptions &options, Diagnostics &diagnostics) {
  mlir::MLIRContext context;
  registerGrandDialect(context);
  MlirDiagnosticBridge bridge(context, diagnostics);

  auto module = mlir::parseSourceString<mlir::ModuleOp>(ir, &context);
  if (!module)
    return {};
  legalizeForTarget(*module, to.profile(), diagnostics);
  if (diagnostics.hasErrors())
    return {};
  return to.emit(*module, options);
}

std::string grand::importToIR(const std::string &source, const std::string &fileName,
                              const LanguagePlugin &from, Diagnostics &diagnostics) {
  mlir::MLIRContext context;
  registerGrandDialect(context);
  MlirDiagnosticBridge bridge(context, diagnostics);

  auto module = from.importSource(source, fileName, context, diagnostics);
  if (!module || diagnostics.hasErrors() || mlir::failed(mlir::verify(*module)))
    return {};
  std::string out;
  llvm::raw_string_ostream os(out);
  module->print(os);
  return out;
}
