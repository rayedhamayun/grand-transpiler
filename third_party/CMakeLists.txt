include(FetchContent)

# tree-sitter core is vendored (only lib/, at commit ef95bde) because the full
# repo has fixture paths too long for Windows. Grammars are fetched at pinned
# commits: the generated C# parser alone is 32 MB.
add_library(tree-sitter STATIC tree-sitter/src/lib.c)
target_include_directories(tree-sitter
  PUBLIC tree-sitter/include
  PRIVATE tree-sitter/src)
target_compile_definitions(tree-sitter PRIVATE _DEFAULT_SOURCE)
set_target_properties(tree-sitter PROPERTIES C_STANDARD 11)

FetchContent_Declare(tree_sitter_java
  URL https://github.com/tree-sitter/tree-sitter-java/archive/e10607b45ff745f5f876bfa3e94fbcc6b44bdc11.tar.gz)
FetchContent_Declare(tree_sitter_c_sharp
  URL https://github.com/tree-sitter/tree-sitter-c-sharp/archive/8c0abe0b84a3681d3e6852ce2e2a1eecb4f731c5.tar.gz)
FetchContent_Declare(tree_sitter_typescript
  URL https://github.com/tree-sitter/tree-sitter-typescript/archive/75b3874edb2dc714fb1fd77a32013d0f8699989f.tar.gz)

# The grammars have no CMake build we want, so populate them only.
foreach(grammar tree_sitter_java tree_sitter_c_sharp tree_sitter_typescript)
  FetchContent_GetProperties(${grammar})
  if(NOT ${grammar}_POPULATED)
    FetchContent_Populate(${grammar})
  endif()
endforeach()

add_library(tree-sitter-grammars STATIC
  ${tree_sitter_java_SOURCE_DIR}/src/parser.c
  ${tree_sitter_c_sharp_SOURCE_DIR}/src/parser.c
  ${tree_sitter_c_sharp_SOURCE_DIR}/src/scanner.c
  ${tree_sitter_typescript_SOURCE_DIR}/typescript/src/parser.c
  ${tree_sitter_typescript_SOURCE_DIR}/typescript/src/scanner.c)
set_source_files_properties(
  ${tree_sitter_java_SOURCE_DIR}/src/parser.c
  PROPERTIES INCLUDE_DIRECTORIES ${tree_sitter_java_SOURCE_DIR}/src)
set_source_files_properties(
  ${tree_sitter_c_sharp_SOURCE_DIR}/src/parser.c
  ${tree_sitter_c_sharp_SOURCE_DIR}/src/scanner.c
  PROPERTIES INCLUDE_DIRECTORIES ${tree_sitter_c_sharp_SOURCE_DIR}/src)
set_source_files_properties(
  ${tree_sitter_typescript_SOURCE_DIR}/typescript/src/parser.c
  ${tree_sitter_typescript_SOURCE_DIR}/typescript/src/scanner.c
  PROPERTIES INCLUDE_DIRECTORIES ${tree_sitter_typescript_SOURCE_DIR}/typescript/src)
# The generated parsers are mostly tables, and optimising them is very slow.
target_compile_options(tree-sitter-grammars PRIVATE -O0 -w)
