#include "languages/Plugins.h"

#include <algorithm>

using namespace grand;

const std::vector<const LanguagePlugin *> &grand::allLanguages() {
  static const std::vector<const LanguagePlugin *> languages = {
      &javaPlugin(), &csharpPlugin(), &typescriptPlugin()};
  return languages;
}

const LanguagePlugin *grand::findLanguage(const std::string &name) {
  for (const LanguagePlugin *language : allLanguages()) {
    if (language->name() == name)
      return language;
    auto extensions = language->extensions();
    if (std::find(extensions.begin(), extensions.end(), name) != extensions.end())
      return language;
  }
  return nullptr;
}

const LanguagePlugin *grand::findLanguageByExtension(const std::string &extension) {
  return findLanguage(extension);
}
