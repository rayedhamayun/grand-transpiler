#include "grand/Core/Diagnostics.h"

using namespace grand;

std::string Diagnostic::format(const std::string &file) const {
  std::string out = file;
  if (line != 0)
    out += ":" + std::to_string(line) + ":" + std::to_string(column);
  out += severity == Severity::Error ? ": error: " : ": warning: ";
  return out + message;
}

void Diagnostics::add(Diagnostic diagnostic) {
  for (const Diagnostic &existing : diagnostics)
    if (existing.severity == diagnostic.severity && existing.line == diagnostic.line &&
        existing.column == diagnostic.column && existing.message == diagnostic.message)
      return;
  if (diagnostic.severity == Diagnostic::Severity::Error)
    ++errorCount;
  diagnostics.push_back(std::move(diagnostic));
}

void Diagnostics::warning(const std::string &message, unsigned line, unsigned column) {
  add({Diagnostic::Severity::Warning, message, line, column});
}

void Diagnostics::error(const std::string &message, unsigned line, unsigned column) {
  add({Diagnostic::Severity::Error, message, line, column});
}
