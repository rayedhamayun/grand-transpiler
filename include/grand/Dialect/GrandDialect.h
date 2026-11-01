#ifndef GRAND_DIALECT_GRANDDIALECT_H
#define GRAND_DIALECT_GRANDDIALECT_H

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/RegionKindInterface.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "grand/Dialect/GrandDialect.h.inc"

#define GET_TYPEDEF_CLASSES
#include "grand/Dialect/GrandTypes.h.inc"

#define GET_OP_CLASSES
#include "grand/Dialect/GrandOps.h.inc"

#endif // GRAND_DIALECT_GRANDDIALECT_H
