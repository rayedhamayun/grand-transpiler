#ifndef GRAND_LANGUAGES_PLUGINS_H
#define GRAND_LANGUAGES_PLUGINS_H

#include "grand/Core/LanguagePlugin.h"

namespace grand {

mlir::OwningOpRef<mlir::ModuleOp> importJava(const std::string &source, const std::string &fileName,
                                             mlir::MLIRContext &context, Diagnostics &diagnostics);
mlir::OwningOpRef<mlir::ModuleOp> importCSharp(const std::string &source,
                                               const std::string &fileName,
                                               mlir::MLIRContext &context,
                                               Diagnostics &diagnostics);
mlir::OwningOpRef<mlir::ModuleOp> importTypeScript(const std::string &source,
                                                   const std::string &fileName,
                                                   mlir::MLIRContext &context,
                                                   Diagnostics &diagnostics);

const LanguagePlugin &javaPlugin();
const LanguagePlugin &csharpPlugin();
const LanguagePlugin &typescriptPlugin();

} // namespace grand

#endif // GRAND_LANGUAGES_PLUGINS_H
