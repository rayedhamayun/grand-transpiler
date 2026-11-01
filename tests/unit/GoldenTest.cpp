// Compares every translation of tests/programs with its checked-in output, so
// readability changes show up in review. To accept new output, run the tests
// with GRAND_UPDATE_GOLDEN=1 and review the diff.

#include "grand/Core/Transpiler.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace grand;

namespace {

std::string readFile(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::vector<fs::path> programs() {
  std::vector<fs::path> found;
  for (const auto &entry : fs::directory_iterator(GRAND_PROGRAMS_DIR))
    if (entry.is_directory())
      found.push_back(entry.path());
  return found;
}

TEST(GoldenTest, AllTranslationsMatch) {
  bool update = std::getenv("GRAND_UPDATE_GOLDEN") != nullptr;
  ASSERT_FALSE(programs().empty());

  for (const fs::path &program : programs()) {
    for (const LanguagePlugin *from : allLanguages()) {
      for (const LanguagePlugin *to : allLanguages()) {
        fs::path source = program / ("Prog." + from->extensions()[0]);
        fs::path golden = program / "golden" /
                          (from->extensions()[0] + "_to_" + to->extensions()[0] + ".txt");
        SCOPED_TRACE(golden.string());

        TranspileRequest request;
        request.source = readFile(source);
        request.fileName = source.string();
        request.from = from;
        request.to = to;
        request.options.className = "Prog";
        Diagnostics diagnostics;
        std::string result = transpile(request, diagnostics);
        for (const Diagnostic &diagnostic : diagnostics.all())
          if (diagnostic.severity == Diagnostic::Severity::Error)
            ADD_FAILURE() << diagnostic.format(source.string());

        if (update) {
          fs::create_directories(golden.parent_path());
          std::ofstream(golden, std::ios::binary) << result;
          continue;
        }
        ASSERT_TRUE(fs::exists(golden)) << "run with GRAND_UPDATE_GOLDEN=1 to create it";
        EXPECT_EQ(readFile(golden), result);
      }
    }
  }
}

} // namespace
