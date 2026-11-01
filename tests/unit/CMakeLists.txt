find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(grand_unit_tests DialectTest.cpp TranspileTest.cpp GoldenTest.cpp)
target_link_libraries(grand_unit_tests PRIVATE GrandLanguages GTest::gtest_main)
target_compile_definitions(grand_unit_tests PRIVATE
  GRAND_PROGRAMS_DIR="${PROJECT_SOURCE_DIR}/tests/programs")
gtest_discover_tests(grand_unit_tests)
