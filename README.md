# grand-transpiler

Translates programs between languages through one shared MLIR dialect, `grand`.
A language implements one importer and one emitter, and can then translate to
and from every other registered language. Java, C# and TypeScript are supported
at feature level 1.

```
source -> tree-sitter CST -> importer -> grand IR -> target passes -> emitter -> source
```

## Build

The project is C++17 and builds in an MSYS2 UCRT64 environment:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{clang,lld,mlir,llvm,cmake,ninja,gtest}
cmake --preset default    # downloads the grammars on first run
cmake --build --preset default
```

Use clang, not GCC. MSYS2's MLIR is built with clang and GCC cannot link it.
CMake stops if the compiler is wrong, or if MLIR is not version 23.

## Usage

```sh
build/tools/grandc/grandc input.java --to typescript -o output.ts
build/tools/grandc/grandc input.ts --to csharp --class Program -o Program.cs
build/tools/grandc/grandc input.cs --emit-ir -o program.mlir
build/tools/grandc/grandc program.mlir --to java -o Program.java
```

Warnings go to stderr. Read them: they state the assumptions behind the output
and list any function that had to be renamed.

## Level 1 features

- Variables, including member variables of the single class
- Integer arithmetic, bitwise operators, `++`/`--`, compound assignment
- `if`, `while`, `do`, `for`, `break`, `continue`, `?:`
- Functions, recursion, and Java and C# overloads
- `int`, `boolean`, `string`; TypeScript `number` is treated as a 32-bit integer
- Printing, string concatenation, length, equality, conversion to string, and
  TypeScript and C# interpolated strings

Java and C# input may contain one class, whose members act as globals. Java
compact source files and C# top-level statements are accepted.

Not supported: further classes and `new`, arrays, `long` and other numeric
types, `char`, `null`, `switch`, closures and anonymous functions, generators,
stdin, command-line arguments. Each is reported as an error with a location.

## Calling translated code

Programs are driven by separate code that calls their functions, so emitted
names and visibility are a contract:

- Function names are kept exactly, including in C#.
- Java and C# emit one public class of `public static` members. Java's class
  name comes from the `-o` file name, or from `--class` for either language.
- TypeScript exports its functions and globals. Importing the module runs any
  top-level statements.

Any rename forced by a reserved word or a TypeScript overload produces a warning
naming the old and new names.

## Behaviour that is not preserved

| Difference | What happens |
|---|---|
| 32-bit overflow vs doubles | Assumed not to occur; warned about when crossing to or from TypeScript |
| TypeScript bare `/` | Assumed to divide exactly, with a warning. Use `Math.trunc(a / b)` for integer division |
| Division by zero | Throws in Java and C#, not in TypeScript |
| Java `==` on strings | Treated as value equality, with a warning |
| Comments | Dropped |

C# prints booleans as `True`/`False`. That text is preserved: translated code
prints what the source would have printed, using a small ternary if needed.

## Tests

```sh
ctest --preset default -LE e2e   # unit and golden tests
ctest --preset default           # also runs the differential E2E suite
```

- `tests/unit`: dialect verifiers, translation behaviour and diagnostics.
- `tests/programs/<feature>`: one program in each language, plus the expected
  output of every translation under `golden/`. After an intended output change,
  run `grand_unit_tests` with `GRAND_UPDATE_GOLDEN=1` and review the diff.
- `tests/e2e/run_e2e.py`: compiles and runs each source and each translation
  through a driver, and compares stdout, including round trips. It needs
  `javac`, `dotnet` (a .NET 8 SDK) and `node`, and it runs TypeScript with
  Node's type stripping, so it does not type-check TypeScript output.

## Code layout

| Path | Contents |
|---|---|
| `include/grand/Dialect/Grand.td` | The dialect's types and ops |
| `include/grand/Import`, `lib/Import` | Importer shared by the C-style grammars |
| `lib/Passes/Legalize.cpp` | Target passes: unreachable code, discarded expressions, naming |
| `include/grand/Emit`, `lib/Emit` | Emitter shared by curly-brace languages |
| `languages/<language>/` | One importer and plugin per language |
| `tools/grandc` | Command-line tool |
| `third_party` | Vendored tree-sitter core; grammars are fetched at pinned commits |

## Adding a language

1. Add `languages/<name>/` with an importer and a `LanguagePlugin` that provides
   a `TargetProfile` and an emitter. C-style languages can subclass
   `CStyleImporter` and `CLikeEmitter`; others implement the plugin directly.
2. List the plugin in `languages/Languages.cpp` and the sources in
   `languages/CMakeLists.txt`.
3. Add the language to `RUNNERS` and `LANGUAGES` in `tests/e2e/run_e2e.py`, and
   a `Prog.<ext>` to each program in `tests/programs`.

Later features with no meaning in the core belong in extension dialects beside
`grand`, so target passes can decide per language how to lower or reject them.
