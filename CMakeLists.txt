cmake_minimum_required(VERSION 3.20)
project(grand_transpiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSYS2's MLIR is built with clang; GCC fails to link it (duplicate TypeID
# symbols). Use the CMake preset, which selects clang and lld.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "grand-transpiler must be built with clang. Run: cmake --preset default")
endif()

find_package(MLIR REQUIRED CONFIG)
if(NOT LLVM_VERSION_MAJOR EQUAL 23)
  message(FATAL_ERROR "Expected MLIR 23.x, found ${LLVM_PACKAGE_VERSION}")
endif()

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
include(TableGen)
include(AddLLVM)
include(AddMLIR)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_BINARY_DIR}/include ${PROJECT_SOURCE_DIR})
# LLVM_DEFINITIONS is one space-separated string in the MSYS2 package.
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
add_definitions(${LLVM_DEFINITIONS_LIST})

add_subdirectory(third_party)
add_subdirectory(include/grand/Dialect)
add_subdirectory(lib)
add_subdirectory(languages)
add_subdirectory(tools/grandc)

enable_testing()
add_subdirectory(tests)
