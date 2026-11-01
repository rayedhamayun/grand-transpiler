add_library(GrandLanguages STATIC
  Languages.cpp
  java/JavaImporter.cpp
  java/JavaPlugin.cpp
  csharp/CSharpImporter.cpp
  csharp/CSharpPlugin.cpp
  typescript/TypeScriptImporter.cpp
  typescript/TypeScriptPlugin.cpp)
target_link_libraries(GrandLanguages PUBLIC GrandCore tree-sitter-grammars)
