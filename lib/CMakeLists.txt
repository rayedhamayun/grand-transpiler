add_subdirectory(Dialect)

add_library(GrandCore STATIC
  Core/Diagnostics.cpp
  Core/Transpiler.cpp
  Passes/Legalize.cpp
  Emit/CLikeEmitter.cpp
  Import/CStyleImporter.cpp)
add_dependencies(GrandCore GrandIncGen)
target_link_libraries(GrandCore PUBLIC GrandDialect tree-sitter)
