#ifndef GRAND_CORE_DIAGNOSTICS_H
#define GRAND_CORE_DIAGNOSTICS_H

#include <string>
#include <vector>

namespace grand {

struct Diagnostic {
  enum class Severity { Warning, Error };
  Severity severity;
  std::string message;
  // 1-based source position; 0 when the diagnostic has no location.
  unsigned line = 0;
  unsigned column = 0;

  std::string format(const std::string &file) const;
};

// Collects warnings and errors, dropping exact duplicates.
class Diagnostics {
public:
  void warning(const std::string &message, unsigned line = 0, unsigned column = 0);
  void error(const std::string &message, unsigned line = 0, unsigned column = 0);

  bool hasErrors() const { return errorCount != 0; }
  const std::vector<Diagnostic> &all() const { return diagnostics; }

private:
  void add(Diagnostic diagnostic);

  std::vector<Diagnostic> diagnostics;
  unsigned errorCount = 0;
};

} // namespace grand

#endif // GRAND_CORE_DIAGNOSTICS_H
