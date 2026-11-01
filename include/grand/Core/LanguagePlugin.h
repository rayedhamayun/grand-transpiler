#ifndef GRAND_CORE_LANGUAGEPLUGIN_H
#define GRAND_CORE_LANGUAGEPLUGIN_H

#include "grand/Core/Diagnostics.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"

#include <set>
#include <string>
#include <vector>

namespace grand {

// What the shared target passes need to know about an output language.
struct TargetProfile {
  std::set<std::string> reservedWords;
  // Prefix used to escape a reserved word (C# `@`); otherwise `_` is appended.
  std::string reservedEscapePrefix;
  bool allowsLocalShadowing = false;
  bool allowsOverloads = true;
  // Numbers are doubles, so 32-bit integer assumptions must be reported.
  bool numbersAreDoubles = false;
  // The entry body runs as top-level statements beside the module's functions.
  bool entryIsTopLevel = false;
};

struct EmitOptions {
  // Name of the wrapping class for languages that need one.
  std::string className;
};

// Adding a language means implementing this and listing it in
// languages/Languages.cpp; nothing else needs to change.
class LanguagePlugin {
public:
  virtual ~LanguagePlugin() = default;

  virtual std::string name() const = 0;
  virtual std::vector<std::string> extensions() const = 0;
  virtual const TargetProfile &profile() const = 0;

  // Returns null after reporting errors to `diagnostics`.
  virtual mlir::OwningOpRef<mlir::ModuleOp>
  importSource(const std::string &source, const std::string &fileName,
               mlir::MLIRContext &context, Diagnostics &diagnostics) const = 0;

  virtual std::string emit(mlir::ModuleOp module, const EmitOptions &options) const = 0;
};

const std::vector<const LanguagePlugin *> &allLanguages();
const LanguagePlugin *findLanguage(const std::string &name);
const LanguagePlugin *findLanguageByExtension(const std::string &extension);

} // namespace grand

#endif // GRAND_CORE_LANGUAGEPLUGIN_H
