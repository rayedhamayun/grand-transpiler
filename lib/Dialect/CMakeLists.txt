add_library(GrandDialect STATIC GrandDialect.cpp)
add_dependencies(GrandDialect GrandIncGen)
target_link_libraries(GrandDialect PUBLIC
  MLIRIR
  MLIRParser
  MLIRSupport
  MLIRFunctionInterfaces
  MLIRInferTypeOpInterface
  MLIRSideEffectInterfaces)
