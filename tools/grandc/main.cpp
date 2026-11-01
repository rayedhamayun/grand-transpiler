// grandc: translates a source file between registered languages.
//
//   grandc input.java --to typescript -o output.ts
//   grandc input.java --emit-ir -o output.mlir
//   grandc input.mlir --to csharp

#include "grand/Core/Transpiler.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace grand;

static void usage() {
  std::cerr << "usage: grandc <input> (--to <language> | --emit-ir) [-o <output>] "
               "[--class <name>]\n";
  std::cerr << "languages:";
  for (const LanguagePlugin *language : allLanguages())
    std::cerr << " " << language->name();
  std::cerr << "\n";
}

static std::string extensionOf(const std::string &path) {
  size_t dot = path.find_last_of('.');
  return dot == std::string::npos ? "" : path.substr(dot + 1);
}

static std::string stemOf(const std::string &path) {
  size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  return name.substr(0, name.find_last_of('.'));
}

int main(int argc, char **argv) {
  std::string input, output, targetName, className;
  bool emitIR = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        usage();
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--to")
      targetName = next();
    else if (arg == "-o")
      output = next();
    else if (arg == "--class")
      className = next();
    else if (arg == "--emit-ir")
      emitIR = true;
    else if (input.empty())
      input = arg;
    else {
      usage();
      return 2;
    }
  }
  if (input.empty() || (targetName.empty() == !emitIR)) {
    usage();
    return 2;
  }

  std::ifstream file(input, std::ios::binary);
  if (!file) {
    std::cerr << "grandc: cannot read " << input << "\n";
    return 1;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();

  std::string extension = extensionOf(input);
  const LanguagePlugin *from = findLanguageByExtension(extension);
  const LanguagePlugin *to = targetName.empty() ? nullptr : findLanguage(targetName);
  if (!emitIR && !to) {
    std::cerr << "grandc: unknown language '" << targetName << "'\n";
    usage();
    return 2;
  }
  if (!from && extension != "mlir") {
    std::cerr << "grandc: unknown input language for " << input << "\n";
    return 2;
  }

  EmitOptions options;
  options.className = className;
  // Java requires the class to match its file name.
  if (options.className.empty() && !output.empty() && extensionOf(output) == "java")
    options.className = stemOf(output);

  Diagnostics diagnostics;
  std::string result;
  if (emitIR) {
    if (!from) {
      std::cerr << "grandc: input is already IR\n";
      return 2;
    }
    result = importToIR(buffer.str(), input, *from, diagnostics);
  } else if (!from) {
    result = emitFromIR(buffer.str(), *to, options, diagnostics);
  } else {
    TranspileRequest request;
    request.source = buffer.str();
    request.fileName = input;
    request.from = from;
    request.to = to;
    request.options = options;
    result = transpile(request, diagnostics);
  }

  for (const Diagnostic &diagnostic : diagnostics.all())
    std::cerr << diagnostic.format(input) << "\n";
  if (diagnostics.hasErrors())
    return 1;

  if (output.empty()) {
    std::cout << result;
    return 0;
  }
  std::ofstream outFile(output, std::ios::binary);
  outFile << result;
  if (!outFile) {
    std::cerr << "grandc: cannot write " << output << "\n";
    return 1;
  }
  return 0;
}
