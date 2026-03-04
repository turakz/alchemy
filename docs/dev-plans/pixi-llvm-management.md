# Research: Pixi as Windows Dev Infra Solution for Alchemy

## Context

Alchemy needs LLVM/Clang development libraries (`LLVMConfig.cmake`, `ClangConfig.cmake`, `.lib` files, C++ headers) to build. On Linux, `apt install llvm-dev libclang-dev` handles this. On Windows, the official LLVM installer omits dev libraries entirely (known upstream decision, won't-fix).

The existing plan (`docs/dev-plans/llvm-windows-choco-package.md`) proposes building LLVM from source on Windows and packaging as a Chocolatey `.nupkg`. This is a multi-day effort requiring 10+ GB disk, MSVC build tools, and ongoing maintenance.

**Question:** Can pixi + conda-forge replace the Chocolatey approach entirely?

## Research Findings

### 1. conda-forge Has Exactly What We Need

The conda-forge channel provides pre-built LLVM/Clang **development** packages for win-64:

| Package | What it provides | Win-64 | Latest |
|---------|-----------------|--------|--------|
| `llvmdev` | `LLVMConfig.cmake`, LLVM headers, `.lib` files | Yes | 22.1.0 |
| `clangdev` | `ClangConfig.cmake`, Clang headers, `.lib` files | Yes | 22.1.0 |
| `libclang` | libclang shared library | Yes | 22.1.0 |
| `libclang-cpp` | Clang C++ shared library | Yes | 22.1.0 |
| `fmt` | fmtlib (already a dependency) | Yes | latest |

The CMake config files land at `$CONDA_PREFIX/Library/lib/cmake/llvm/LLVMConfig.cmake` and `$CONDA_PREFIX/Library/lib/cmake/clang/ClangConfig.cmake` on Windows. These are exactly what `find_package(LLVM CONFIG)` and `find_package(Clang CONFIG)` need.

This is the **same** thing that `apt install llvm-dev libclang-dev` provides on Linux — just delivered through conda-forge instead of apt.

### 2. Pixi Integration Is Straightforward

Pixi is a cross-platform package manager and task runner built on the conda ecosystem. Integration into alchemy would involve:

**a) `pixi.toml` at project root** — declares dependencies and tasks:
```toml
[project]
name = "alchemy"
version = "0.1.0"
channels = ["conda-forge"]
platforms = ["linux-64", "win-64"]

[dependencies]
cmake = ">=3.14"
ninja = "*"
fmt = ">=9"

[target.linux-64.dependencies]
# on linux, llvm-dev comes from apt (system package)
# or can be pulled from conda-forge too
clangdev = ">=14"
llvmdev = ">=14"

[target.win-64.dependencies]
clangdev = ">=14"
llvmdev = ">=14"

[tasks]
configure = "cmake -S. -Bbuild -GNinja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang.cmake"
build = { cmd = "cmake --build build", depends-on = ["configure"] }
test-unit = { cmd = "cmake --build build --target unit_tests && cd build && ctest --output-on-failure --verbose -L unit_tests", depends-on = ["build"] }
test-integration = { cmd = "cmake --build build --target integration_tests && cd build && ctest --output-on-failure --verbose -L integration_tests", depends-on = ["build"] }
```

**b) Pixi tasks mirror existing Makefile targets.** The task runner supports dependencies between tasks, platform-specific overrides via `[target.win-64.tasks]`, and runs a cross-platform shell (deno_task_shell) so the same commands work on Linux and Windows.

**c) Developer workflow becomes:**
```bash
# one-time setup (any platform)
curl -fsSL https://pixi.sh/install.sh | bash   # or winget install prefix-dev.pixi
pixi install                                      # fetches all deps including LLVM

# build
pixi run build

# test
pixi run test-unit
```

### 3. Viability Assessment

**Strong pros:**
- Eliminates the need to build LLVM from source on Windows (saves days of effort)
- Pre-built binaries from conda-forge, actively maintained, version 22.1.0 available
- Cross-platform — same `pixi.toml` works on Linux and Windows
- Lockfile (`pixi.lock`) ensures reproducible builds across machines
- Task runner can replace or wrap existing Makefile targets
- No internal package hosting required (conda-forge is public)
- Pixi itself is a single binary, no admin privileges needed to install
- Fast — Rust-native solver, parallel downloads

**Known risks / considerations:**
- **zstd linking issue on Windows (RESOLVED):** conda-forge's LLVM on win-64 had an issue where `LLVMExports.cmake` references `zstd::libzstd_shared` but the target wasn't found when using Clang instead of MSVC. **This is fixed upstream** — [PR #121437](https://github.com/llvm/llvm-project/pull/121437) extended `Findzstd.cmake` to handle Clang targeting MSVC ABI, merged Jan 2 2025, backported to LLVM 19.x ([PR #121755](https://github.com/llvm/llvm-project/pull/121755)). conda-forge's current LLVM 22.1.0 includes this fix. If using an older conda-forge LLVM (<19.1.7), a CMake workaround is available:
  ```cmake
  # workaround for pre-19.1.7 LLVM on win-64 with clang
  if(WIN32 AND NOT TARGET zstd::libzstd_shared)
    find_library(ZSTD_LIBRARY NAMES zstd libzstd)
    if(ZSTD_LIBRARY)
      add_library(zstd::libzstd_shared SHARED IMPORTED)
      set_target_properties(zstd::libzstd_shared PROPERTIES IMPORTED_IMPLIB "${ZSTD_LIBRARY}")
    endif()
  endif()
  ```
- **LLVM version pinning:** conda-forge has 22.1.0, alchemy supports 12-18. Need to verify conda-forge carries older versions or that alchemy works with 22.x. The API surface alchemy uses (clangAST, clangTooling) is stable, so this is likely fine.
- **pixi-build-cmake is preview:** The pixi CMake build backend is still in preview. However, we don't need it — we just need pixi for dependency management and task running, not for building alchemy as a conda package.
- **Makefile coexistence:** Pixi doesn't replace the Makefile. Both can coexist — `pixi run` sets up the environment (PATH, LLVM_DIR, etc.), then calls the same cmake commands. Linux developers can continue using `make` directly if they prefer.
- **Corporate network:** pixi fetches from conda-forge (anaconda.org). If this is blocked, a mirror or proxy would be needed. Same concern applies to any package manager though.
- **conda-forge LLVM is built with MSVC:** On win-64, the LLVM libraries are compiled with MSVC/VS2022. Alchemy currently uses clang as its compiler. Linking clang-compiled code against MSVC-compiled LLVM libs should work (clang on Windows targets MSVC ABI by default), but this needs verification.

### 4. Comparison: Pixi vs Chocolatey Build-from-Source

| Aspect | Pixi + conda-forge | Chocolatey (build from source) |
|--------|-------------------|-------------------------------|
| Setup time | Minutes | Days (build LLVM from source) |
| Maintenance | conda-forge maintains packages | We maintain the build + package |
| Disk space | ~200 MB (pre-built) | ~10 GB (build artifacts) |
| LLVM versions | Whatever conda-forge has | Any version we build |
| Hosting | Public conda-forge | Internal Chocolatey feed |
| Cross-platform | Same pixi.toml for Linux + Win | Windows-only solution |
| Reproducibility | Lockfile | Manual version pinning |
| Risk | Version availability (low) | Build failures, ABI issues |

### 5. Recommendation

Pixi + conda-forge is the significantly lighter-weight path. It solves the Windows LLVM dev library problem without building from source, and as a bonus gives us a cross-platform task runner and dependency manager.

**Suggested approach:**
1. Add `pixi.toml` to alchemy with LLVM/Clang dev dependencies for win-64
2. Add pixi tasks that mirror the key Makefile targets (build, test, lint)
3. Keep the existing Makefile — pixi is additive, not a replacement
4. On Windows, developers use `pixi run build` instead of `make alchemy.debug`
5. On Linux, developers can use either `make` or `pixi run` — their choice
6. The zstd linking issue is fixed in LLVM >=19.1.7 (conda-forge has 22.1.0, so no workaround needed)
7. The Chocolatey dev-plan document can remain as a fallback if conda-forge doesn't work out

### Sources

- [conda-forge llvmdev package](https://prefix.dev/channels/conda-forge/packages/llvmdev)
- [conda-forge clangdev feedstock](https://github.com/conda-forge/clangdev-feedstock)
- [conda-forge llvmdev feedstock](https://github.com/conda-forge/llvmdev-feedstock)
- [Pixi CMake integration blog](https://prefix.dev/blog/pixi-build-for-cmake-projects)
- [Pixi task runner docs](https://pixi.prefix.dev/v0.50.0/workspace/advanced_tasks/)
- [Pixi C++ build tutorial](https://pixi.prefix.dev/latest/build/cpp/)
- [zstd linking issue on win-64](https://github.com/llvm/llvm-project/issues/121345)
- [CppCon 2025 Pixi talk](https://cppcon2025.sched.com/event/27bPg/cross-platform-package-management-for-modern-c++-development-with-pixi)
