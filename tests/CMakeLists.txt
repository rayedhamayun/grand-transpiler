add_subdirectory(unit)

# Runs real compilers, so it is labelled: `ctest -LE e2e` skips it.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME e2e_differential
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_e2e.py
            $<TARGET_FILE:grandc> ${CMAKE_CURRENT_SOURCE_DIR}/programs)
  set_tests_properties(e2e_differential PROPERTIES LABELS e2e TIMEOUT 1800)
endif()
