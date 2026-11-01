#include "grand/Core/Transpiler.h"

#include "grand/Dialect/GrandDialect.h"

#include "mlir/IR/Builders.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <set>
#include <string>

using namespace mlir;
using namespace grand;

namespace {

// True if control never reaches the op after `op`.
bool alwaysExits(Operation *op);

bool blockAlwaysExits(Region &region) {
  if (region.empty())
    return false;
  for (Operation &op : region.front())
    if (alwaysExits(&op))
      return true;
  return false;
}

bool containsBreak(Region &region) {
  bool found = false;
  region.walk<WalkOrder::PreOrder>([&](Operation *op) {
    if (isa<WhileOp, DoWhileOp, ForOp>(op))
      return WalkResult::skip();
    if (isa<BreakOp>(op)) {
      found = true;
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  return found;
}

bool isLiteralTrue(Region &region) {
  if (region.empty() || region.front().empty())
    return false;
  auto result = dyn_cast<RegionResultOp>(region.front().back());
  if (!result)
    return false;
  auto literal = result.getValue().getDefiningOp<BoolLitOp>();
  return literal && literal.getValue();
}

bool alwaysExits(Operation *op) {
  if (isa<ReturnOp, BreakOp, ContinueOp>(op))
    return true;
  if (auto ifOp = dyn_cast<IfOp>(op))
    return blockAlwaysExits(ifOp.getThenRegion()) && blockAlwaysExits(ifOp.getElseRegion());
  if (auto loop = dyn_cast<WhileOp>(op))
    return isLiteralTrue(loop.getCondRegion()) && !containsBreak(loop.getBodyRegion());
  if (auto forOp = dyn_cast<ForOp>(op)) {
    ForLoopOp loop = forOp.getLoop();
    bool forever = loop.getCondRegion().empty() || loop.getCondRegion().front().empty() ||
                   isLiteralTrue(loop.getCondRegion());
    return forever && !containsBreak(loop.getBodyRegion());
  }
  return false;
}

// Java rejects statements after a jump, and the others warn.
void removeUnreachableCode(ModuleOp module) {
  module.walk([](Block *block) {
    Operation *firstDead = nullptr;
    for (Operation &op : *block)
      if (alwaysExits(&op)) {
        firstDead = op.getNextNode();
        break;
      }
    while (firstDead) {
      Operation *next = firstDead->getNextNode();
      firstDead->dropAllUses();
      firstDead->erase();
      firstDead = next;
    }
  });
}

bool isDiscardable(Operation *op) {
  return isa<AddOp, SubOp, MulOp, DivOp, RemOp, NegOp, BitAndOp, BitOrOp, BitXorOp, BitNotOp,
             ShlOp, ShrOp, UShrOp, CmpOp, NotOp, IntLitOp, BoolLitOp, StrLitOp, StrConcatOp,
             StrLenOp, StrEqOp, ToStringOp, LoadOp, GlobalRefOp>(op);
}

// `a + b;` is legal in TypeScript but not Java or C#. Operands that have side
// effects stay behind as statements.
void removeDiscardedExpressions(ModuleOp module) {
  bool changed = true;
  while (changed) {
    changed = false;
    SmallVector<Operation *> dead;
    module.walk([&](Operation *op) {
      if (isDiscardable(op) && op->use_empty())
        dead.push_back(op);
    });
    for (Operation *op : dead) {
      op->erase();
      changed = true;
    }
  }
}

std::string legalName(const std::string &name, const TargetProfile &target) {
  if (!target.reservedWords.count(name))
    return name;
  return target.reservedEscapePrefix.empty() ? name + "_" : target.reservedEscapePrefix + name;
}

std::string uniqueName(const std::string &base, const std::set<std::string> &taken) {
  if (!taken.count(base))
    return base;
  for (unsigned i = 2;; ++i) {
    std::string candidate = base + "_" + std::to_string(i);
    if (!taken.count(candidate))
      return candidate;
  }
}

std::string typeSuffix(Type type) {
  if (isa<IntType>(type))
    return "Int";
  if (isa<BoolType>(type))
    return "Bool";
  return "String";
}

// Assigns names to locals. `visible` holds every enclosing name.
void nameLocals(Block &block, std::set<std::string> visible, const TargetProfile &target);

void nameRegions(Operation &op, const std::set<std::string> &visible,
                 const TargetProfile &target) {
  for (Region &region : op.getRegions())
    if (!region.empty())
      nameLocals(region.front(), visible, target);
}

void nameLocals(Block &block, std::set<std::string> visible, const TargetProfile &target) {
  std::set<std::string> declaredHere;
  // C# also rejects an inner name that a later outer declaration reuses.
  std::set<std::string> inBlock;
  for (Operation &op : block)
    if (auto var = dyn_cast<VarOp>(op))
      inBlock.insert(legalName(var.getName().str(), target));
  for (Operation &op : block) {
    if (auto var = dyn_cast<VarOp>(op)) {
      std::string name = legalName(var.getName().str(), target);
      const std::set<std::string> &taken = target.allowsLocalShadowing ? declaredHere : visible;
      name = uniqueName(name, taken);
      var->setAttr(kEmitNameAttr, StringAttr::get(op.getContext(), name));
      visible.insert(name);
      declaredHere.insert(name);
    }
    if (target.allowsLocalShadowing) {
      nameRegions(op, visible, target);
    } else {
      std::set<std::string> nested = visible;
      nested.insert(inBlock.begin(), inBlock.end());
      nameRegions(op, nested, target);
    }
  }
}

void assignNames(ModuleOp module, const TargetProfile &target, Diagnostics &diagnostics) {
  MLIRContext *context = module.getContext();
  std::set<std::string> taken;

  auto rename = [&](Operation *op, const std::string &source, std::string name) {
    if (name != source)
      diagnostics.warning("'" + source + "' is emitted as '" + name +
                          "'; update any driver code that refers to it");
    taken.insert(name);
    op->setAttr(kEmitNameAttr, StringAttr::get(context, name));
  };

  for (GlobalOp global : module.getOps<GlobalOp>()) {
    std::string source = global.getSymName().str();
    rename(global, source, legalName(source, target));
  }

  // Overloads share a name in their source, so they stay together when the
  // target has overloads. Entry functions are named by the emitter.
  llvm::StringMap<unsigned> seen;
  for (FuncOp func : module.getOps<FuncOp>()) {
    if (func.getEntry())
      continue;
    std::string source =
        func.getSourceName() ? func.getSourceName()->str() : func.getSymName().str();
    std::string name = legalName(source, target);
    bool overload = seen[source]++ != 0;
    if (overload && !target.allowsOverloads) {
      name = source;
      for (Type type : func.getArgumentTypes())
        name += typeSuffix(type);
      name = uniqueName(name, taken);
    }
    rename(func, source, name);
  }

  for (FuncOp func : module.getOps<FuncOp>()) {
    if (func.getBody().empty())
      continue;
    std::set<std::string> visible;
    // A top-level entry body shares its scope with the module's declarations.
    if (func.getEntry() && target.entryIsTopLevel)
      visible = taken;
    if (func.getEntry() && target.entryIsTopLevel) {
      TargetProfile strict = target;
      strict.allowsLocalShadowing = false;
      nameLocals(func.getBody().front(), visible, strict);
    } else {
      nameLocals(func.getBody().front(), visible, target);
    }
  }
}

void warnAboutIntegerModel(ModuleOp module, const TargetProfile &target,
                           Diagnostics &diagnostics) {
  auto source = module->getAttrOfType<StringAttr>(kSourceLanguageAttr);
  if (!source)
    return;
  bool sourceIsDoubles = source.getValue() == "typescript";
  if (sourceIsDoubles == target.numbersAreDoubles)
    return;
  bool hasArithmetic = false;
  module.walk([&](Operation *op) {
    if (isa<AddOp, SubOp, MulOp, NegOp, ShlOp, IncDecOp, CompoundAssignOp>(op))
      hasArithmetic = true;
  });
  if (!hasArithmetic)
    return;
  diagnostics.warning(sourceIsDoubles
                          ? "numbers are translated as 32-bit integers; values outside that "
                            "range are not preserved"
                          : "translated arithmetic assumes no 32-bit overflow; wrapping is "
                            "not preserved");
}

} // namespace

void grand::legalizeForTarget(ModuleOp module, const TargetProfile &target,
                              Diagnostics &diagnostics) {
  removeUnreachableCode(module);
  removeDiscardedExpressions(module);
  warnAboutIntegerModel(module, target, diagnostics);
  assignNames(module, target, diagnostics);

  module.walk([&](FuncOp func) {
    if (func.getEntry() && func->hasAttr(kTopLevelAttr) && !target.entryIsTopLevel)
      diagnostics.warning("top-level statements became a main function; a driver must call "
                          "it for them to run");
  });
}
