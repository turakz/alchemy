# `alchemy`: command-line refactoring tool

[![C++20](https://img.shields.io/badge/c++-20-blue)](https://cppreference.com/w/cpp/compiler_support/20.html)
[![LLVM](https://img.shields.io/badge/llvm-18-blue)](https://llvm.org/)
[![Tests](https://img.shields.io/badge/tests-315%20passing-brightgreen)](https://bitbucket.org/msasafety/alchemy/src/develop/tests/)
[![Coverage](https://img.shields.io/badge/coverage-89.3%25-brightgreen)](https://bitbucket.org/msasafety/alchemy/src/develop/)

**version**: v0.1.0-alpha

- struct alignment refactoring, cross-compiler support

## supported platforms

### targets
cross-compiled binaries for Linux and Windows are planned for official releases.

- **languages**: C, C++
- **compilers**: GCC, Clang, MSVC, IAR (embedded ARM)

### linux development
building alchemy from source is supported on Linux (Ubuntu 20.04, 22.04, 24.04) and macOS.

### windows development
building from source on Windows is not **yet** supported. the official LLVM Windows installer does
not include the development libraries (`LLVMConfig.cmake`) required by `find_package(LLVM)`.

## why `alchemy`?

performance and efficiency:

- **memory footprint**: memory constrained systems cannot afford to waste bytes
- **cache usage**: better alignment, better cache locality -> better performance

## features

### refactoring

- c/cxx struct alignment optimizations (`--salign`)
- **cross-compiler support**: works with GCC, Clang, MSVC, and IAR projects

### metrics

- struct alignment: total size, wasted padding, cache utilization

### dry-runs

- where applicable, will redirect transmutations to `stdout`

### flags

| flag | short | description |
|------|-------|-------------|
| `--salign` | | enable struct alignment optimization |
| `--build-dir` | `-b` | path to build directory (with `compile_commands.json`) |
| `--output-dir` | `-o` | output directory |
| `--jobs` | `-j` | number of parallel jobs (default: 1, 0 = auto-detect) |
| `--exclude` | | exclude source file patterns |
| `--dry-run` | | preview transmutations on `stdout` without writing files |
| `--dump-config` | | write resolved CLI args to `.alchemy/alchemy.toml` |

### configuration file

`alchemy` supports a TOML configuration file at `.alchemy/alchemy.toml` in the project root (cwd).
if present, it is automatically loaded and merged with CLI args. CLI args always take precedence.

use `--dump-config` to generate a config from the current CLI invocation:

```bash
alchemy --salign -b "./build" --jobs 4 "src/**/*.h" --dump-config
```

this writes `.alchemy/alchemy.toml`:

```toml
[options]
build-dir = "./build"
sources = ["src/**/*.h"]
exclude = []
salign = true
jobs = 4
dry-run = false
```

subsequent runs can use the config directly:

```bash
alchemy                        # entire config is used
alchemy --salign "src/**/*.h"  # build-dir and jobs come from config
```

long CLI flag names match TOML keys exactly (`--build-dir` = `build-dir`, `--jobs` = `jobs`, etc.).

- short aliases (`-b`, `-o`, `-j`) are available for convenience.

## quick start

```bash
git clone git@bitbucket.org:msasafety/alchemy.git
cd alchemy
make doctor           # verify your environment before building
make setup            # install dependencies (llvm, clang, cmake, ninja, fmt)
make alchemy.release  # or: make alchemy.debug
```

the binary is written to `build/alchemy`. you can temporarily add this to your `PATH` for
the current `bash` session via:

```bash
export PATH="/full/path/to/repo/alchemy/build:$PATH"
```

### LLVM version handling

`make setup` detects your Ubuntu version and installs the correct native LLVM packages automatically:

| Ubuntu Version | LLVM Ver | Packages installed                     |
|----------------|----------|----------------------------------------|
| 20.04 (Focal)  | 12       | clang-12, llvm-12-dev, libclang-12-dev |
| 22.04 (Jammy)  | 14       | clang-14, llvm-14-dev, libclang-14-dev |
| 24.04 (Noble)  | 18       | clang-18, llvm-18-dev, libclang-18-dev |

the build system forwards the detected version to CMake via `ALCHEMY_LLVM_VERSION`, which
the toolchain uses to resolve `clang-N`, `clang++-N`, `llvm-config-N`, and `find_package(LLVM)`.

to override manually:

```bash
# via CMake variable
cmake -S . -B build -DALCHEMY_LLVM_VERSION=18 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang.cmake

# via environment variable
export ALCHEMY_LLVM_VERSION=18
make alchemy.debug
```

## usage

`alchemy` is built with the command-line (cli) in mind (plus a dash of minimalism). if you are familiar with any `clang`, `gcc` family tool-chains, some
of this should come as no surprise to you.

to use `alchemy`, you must first build your project and produce a build directory (with `compile_commands.json`),
or tell `alchemy` where to find one.

- `alchemy` will auto-detect build dirs starting from the executing directory (current working dir),
so it is possible it finds the wrong one. when in doubt, be explicit.

```bash
alchemy [options] <source0> [... sourceN]
alchemy --help
```

### examples

```bash
alchemy --salign "inc/**/*.h"
alchemy --salign "inc/**/*.hpp"

alchemy --salign "inc/*.h"
alchemy --salign "inc/*.hpp"

alchemy --salign "inc/sensor.h" "inc/memory.h"
alchemy --salign "inc/sensor.hpp" "inc/memory.hpp"

alchemy --salign "inc/**/*.h" --dry-run # redirect transmutations to stdout
alchemy --salign "inc/**/*.hpp" --dry-run

alchemy --salign "inc/**/*.h" --exclude="inc/external/**/*.h" --dry-run # do not transmute excluded files
alchemy --salign "inc/**/*.h" --exclude="inc/external/**/*.hpp" --dry-run
```

`--build-dir` / `-b` lets you specify build directories:

```bash
alchemy -b "project/build" --salign "inc/**/*.h" --exclude="inc/external/**/*.h" --dry-run
```

### multi-threading

```bash
alchemy --salign "inc/**/*.h" --jobs 4  # use 4 cores
alchemy --salign "inc/**/*.h" -j0       # auto-detect based on CPU cores (default: 1)
```

## example input/output

### input (72 bytes, 10 bytes wasted)

```c
#ifndef _TEST_H
#define _TEST_H
struct TestStruct {
  char x;      // 1 byte + 7 padding
  double y;    // 8 bytes
  char z;      // 1 byte + 3 padding
  int a;       // 4 bytes
  double b;    // 8 bytes
  char c[40];  // 40 bytes + 2 trailing padding
};
#endif
```

### output (64 bytes, 2 bytes wasted -> 33% reduction)
![output](docs/images/alchemy-example.png)

### behavior notes

- **trailing comments**: inline comments on the same line as a field (e.g., `/* ... */`, `// ...`), or comments immediately above the field travel with the field when it is reordered. a warning is printed reminding developers to review affected structs.
- **type spelling**: replacement text preserves the developer's source spelling (e.g., `bool` not `_Bool`, `uint8_t` not `unsigned char`).
- **array fields**: array dimensions are placed on the identifier (`uint8_t data[3]`), not the type.
- **non-reorderable fields**: bitfields and anonymous unions/structs are detected and excluded from reordering. if a swap would involve a non-reorderable field, the entire struct is skipped.

## development

### running tests

```bash
make test.unit          # run unit tests
make test.integration   # run integration tests
make test.performance   # run performance benchmarks
make test.all           # run all tests (unit + integration)
```

### formatting & linting

```bash
make format       # format source code with clang-format
make format.check # check formatting without modifying files
make lint         # run clang-tidy linter
make check        # run both format.check + lint
```

### coverage

analyze code coverage to see which lines, functions, and branches are exercised by tests.

```bash
make setup.coverage  # one-time: installs lcov + compiler-rt
```

```bash
make coverage              # full coverage (all tests)
make coverage.unit         # unit tests only
make coverage.integration  # integration tests only
```

open `build/coverage_html/index.html` in your browser.

### cleanup

```bash
make clean           # remove build artifacts
make coverage.clean  # remove coverage artifacts
```

### uninstalling dependencies

```bash
make teardown           # remove dependencies installed by 'make setup'
make teardown.coverage  # remove coverage tools installed by 'make setup.coverage'
```

note: see `make help` for the full list of targets and descriptions

## documentation

for architecture details, see:

- `docs/design/component-diagrams.md` -> architecture overview
- `docs/design/sequence-diagrams.md`  -> interaction flows

## license

see LICENSE file for details
