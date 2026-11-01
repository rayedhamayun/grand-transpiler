#ifndef GRAND_CORE_TRANSPILER_H
#define GRAND_CORE_TRANSPILER_H

#include "grand/Core/Diagnostics.h"
#include "grand/Core/LanguagePlugin.h"

#include <string>

namespace grand {

// Module attributes set by importers.
inline constexpr char kClassNameAttr[] = "grand.class_name";
inline constexpr char kSourceLanguageAttr[] = "grand.source_language";
// Set on an entry function built from top-level statements.
inline constexpr char kTopLevelAttr[] = "grand.top_level";
// Set by legalizeForTarget: the name to print for a function, global or local.
inline constexpr char kEmitNameAttr[] = "grand.emit_name";

void registerGrandDialect(mlir::MLIRContext &context);

// Rewrites `module` so `target` can print it: removes code the target rejects,
// assigns legal names and reports the assumptions being made.
void legalizeForTarget(mlir::ModuleOp module, const TargetProfile &target,
                       Diagnostics &diagnostics);

struct TranspileRequest {
  std::string source;
  std::string fileName;
  const LanguagePlugin *from = nullptr;
  const LanguagePlugin *to = nullptr;
  EmitOptions options;
};

// Returns the translated source; check `diagnostics` for errors.
std::string transpile(const TranspileRequest &request, Diagnostics &diagnostics);

// Parses grand IR text and prints it as `to`.
std::string emitFromIR(const std::string &ir, const LanguagePlugin &to,
                       const EmitOptions &options, Diagnostics &diagnostics);

// Returns the verified grand IR for a source file.
std::string importToIR(const std::string &source, const std::string &fileName,
                       const LanguagePlugin &from, Diagnostics &diagnostics);

} // namespace grand

#endif // GRAND_CORE_TRANSPILER_H
