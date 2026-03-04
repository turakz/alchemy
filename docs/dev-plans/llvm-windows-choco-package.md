# LLVM Windows Development Libraries — Chocolatey Package Plan

## background

alchemy links against LLVM/Clang C++ libraries (`clangAST`, `clangTooling`, `clangFrontend`, etc.) at
build time via `find_package(LLVM)` and `find_package(Clang)`. these CMake commands require:

- `LLVMConfig.cmake` and `ClangConfig.cmake` (package discovery)
- LLVM/Clang static libraries (`.lib` files)
- LLVM/Clang C++ development headers

the official LLVM Windows installer (whether from llvm.org, Chocolatey, or winget) does **not** include
these. it just ships the compiler binaries (`clang.exe`, `clang++.exe`) and runtime support.
this is a known upstream decision ([LLVM #53052](https://github.com/llvm/llvm-project/issues/53052),
closed as won't-fix).

on Linux, `apt install llvm-dev libclang-dev` provides everything. on Windows, not so.

**solution:** build LLVM 18 from source on Windows, package the development output as a Chocolatey
`.nupkg`, and host it on an internal feed for alchemy developers.

**note:** for Ubuntu releases, each one has 12/14/18 available for 20.04/22.04/24.04 respectively,
but for C/C++ APIs used by `alchemy` there are no breaking API changes between those in the codebase,
as of now.

---

## prerequisites

### build machine requirements

- **OS:** Windows 10/11 (x64)
- **disk space:** ~10 GB free (3 GB source, 5+ GB build artifacts, 1-3 GB install output)
- **RAM:** 16 GB recommended (linking is memory-intensive)
- **CPU:** multi-core recommended (build is parallelizable)

### software

install these before starting (elevated PowerShell):

```powershell
# Visual Studio Build Tools (provides MSVC compiler, linker, Windows SDK)
choco install -y visualstudio2022buildtools `
  --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet --wait"

# build tools
choco install -y cmake --installargs "ADD_CMAKE_TO_PATH=System"
choco install -y ninja
choco install -y python3
choco install -y git
```

restart your shell after installation to pick up PATH changes.

### verify prerequisites

```powershell
cmake --version    # >= 3.21
ninja --version    # any recent version
python --version   # >= 3.8
git --version      # any recent version
cl.exe             # run from Developer Command Prompt or after vcvarsall.bat
```

---

## phase 1: build LLVM from source

### 1.1 clone the LLVM source

```powershell
cd C:\dev
git clone --depth 1 --branch llvmorg-18.1.8 https://github.com/llvm/llvm-project.git llvm-18-src
```

using `--depth 1` avoids downloading the full git history (~3 GB savings).

### 1.2 configure with CMake

open a **Developer Command Prompt for VS 2022** (or run `vcvarsall.bat x64` first), then:

```powershell
cmake -G Ninja -S C:\dev\llvm-18-src\llvm -B C:\dev\llvm-18-build ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX=C:\dev\llvm-18-install ^
  -DLLVM_ENABLE_PROJECTS="clang" ^
  -DLLVM_TARGETS_TO_BUILD="X86" ^
  -DLLVM_INSTALL_UTILS=ON ^
  -DLLVM_USE_LINKER=lld ^
  -DLLVM_PARALLEL_LINK_JOBS=2 ^
  -DLLVM_OPTIMIZED_TABLEGEN=ON ^
  -DLLVM_INCLUDE_TESTS=OFF ^
  -DLLVM_INCLUDE_BENCHMARKS=OFF ^
  -DLLVM_INCLUDE_EXAMPLES=OFF ^
  -DCLANG_INCLUDE_TESTS=OFF
```

**key flags explained:**

| Flag | Purpose |
|------|---------|
| `-G Ninja` | use Ninja instead of MSBuild (significantly faster builds) |
| `-DCMAKE_BUILD_TYPE=Release` | Release mode — smaller output, faster build than Debug |
| `-DLLVM_ENABLE_PROJECTS="clang"` | build clang libraries (required by alchemy) |
| `-DLLVM_TARGETS_TO_BUILD="X86"` | only build X86 backend (drastically reduces build time) |
| `-DLLVM_USE_LINKER=lld` | use lld linker (faster than MSVC link.exe) |
| `-DLLVM_PARALLEL_LINK_JOBS=2` | limit parallel link jobs to avoid OOM on 16 GB machines |
| `-DLLVM_OPTIMIZED_TABLEGEN=ON` | build tablegen with optimizations (speeds up build) |
| `-DLLVM_INCLUDE_TESTS=OFF` | skip building test infrastructure |
| `-DLLVM_INCLUDE_BENCHMARKS=OFF` | skip building benchmarks |
| `-DLLVM_INCLUDE_EXAMPLES=OFF` | skip building examples |

### 1.3 build

```powershell
cmake --build C:\dev\llvm-18-build --config Release
```

**expected duration:** 30-60 minutes with Ninja on a modern multi-core machine. MSBuild can take 2+ hours.

### 1.4 install

```powershell
cmake --build C:\dev\llvm-18-build --config Release --target install
```

this copies headers, libraries, tools, and CMake config files to `C:\dev\llvm-18-install`.

---

## phase 2: verify the build

after install, verify the critical files exist:

```powershell
# CMake package config (this is the file that was missing)
Test-Path "C:\dev\llvm-18-install\lib\cmake\llvm\LLVMConfig.cmake"
Test-Path "C:\dev\llvm-18-install\lib\cmake\clang\ClangConfig.cmake"

# key static libraries alchemy links against
Test-Path "C:\dev\llvm-18-install\lib\clangAST.lib"
Test-Path "C:\dev\llvm-18-install\lib\clangASTMatchers.lib"
Test-Path "C:\dev\llvm-18-install\lib\clangBasic.lib"
Test-Path "C:\dev\llvm-18-install\lib\clangFrontend.lib"
Test-Path "C:\dev\llvm-18-install\lib\clangTooling.lib"

# headers
Test-Path "C:\dev\llvm-18-install\include\llvm\IR\Module.h"
Test-Path "C:\dev\llvm-18-install\include\clang\AST\Decl.h"

# tools
Test-Path "C:\dev\llvm-18-install\bin\clang.exe"
Test-Path "C:\dev\llvm-18-install\bin\clang++.exe"
```

all of these should return `True`.

### quick smoke test

```powershell
# verify find_package works
mkdir C:\dev\llvm-test && cd C:\dev\llvm-test

# create a minimal CMakeLists.txt
@"
cmake_minimum_required(VERSION 3.14)
project(llvm_test)
find_package(LLVM 18 REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM libraries: ${LLVM_LIBRARY_DIRS}")
message(STATUS "Clang found at: ${Clang_DIR}")
"@ | Out-File -Encoding UTF8 CMakeLists.txt

cmake -S . -B build -DLLVM_DIR="C:\dev\llvm-18-install\lib\cmake\llvm" -DClang_DIR="C:\dev\llvm-18-install\lib\cmake\clang"

# cleanup
cd C:\dev && Remove-Item -Recurse -Force C:\dev\llvm-test
```

---

## phase 3: create the Chocolatey package

### 3.1 package strategy

the LLVM install output (Release, X86-only) is typically **1-3 GB**. Chocolatey packages have a practical
limit of ~2 GB due to NuGet's underlying limitations.

**recommended approach:** keep the `.nupkg` small and host the LLVM archive separately on an internal
file share or HTTP server. the install script downloads and extracts it during `choco install`.

```
llvm-dev-libs/
├── llvm-dev-libs.nuspec
└── tools/
    ├── chocolateyInstall.ps1
    ├── chocolateyUninstall.ps1
    └── helpers.ps1
```

the LLVM archive itself lives on an internal share:
```
\\<server>\packages\llvm-dev-libs\llvm-18.1.8-dev-win64.zip
```

### 3.2 create the archive

```powershell
# compress the install output
Compress-Archive -Path "C:\dev\llvm-18-install\*" -DestinationPath "C:\dev\llvm-18.1.8-dev-win64.zip"

# copy to internal share
Copy-Item "C:\dev\llvm-18.1.8-dev-win64.zip" "\\<server>\packages\llvm-dev-libs\"
```

### 3.3 scaffold the package

```powershell
cd C:\dev
choco new llvm-dev-libs --version 18.1.8
cd llvm-dev-libs
```

### 3.4 edit the nuspec

replace the contents of `llvm-dev-libs.nuspec`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2015/06/nuspec.xsd">
  <metadata>
    <id>llvm-dev-libs</id>
    <version>18.1.8</version>
    <title>LLVM Development Libraries</title>
    <authors>LLVM Project (packaged by MSA Safety)</authors>
    <description>
      LLVM and Clang development libraries for Windows (x64, Release).
      Includes LLVMConfig.cmake, ClangConfig.cmake, static libraries (.lib),
      and C++ development headers required by projects that link against
      LLVM/Clang C++ APIs (e.g., alchemy).

      Built from source: llvmorg-18.1.8
      Configuration: Release, X86 target only, MSVCRT (dynamic CRT)
    </description>
    <summary>LLVM 18 development libraries for Windows (headers, .lib, CMake configs)</summary>
    <tags>llvm clang development libraries cmake</tags>
    <projectUrl>https://llvm.org/</projectUrl>
    <licenseUrl>https://llvm.org/LICENSE.txt</licenseUrl>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
  </metadata>
  <files>
    <file src="tools\**" target="tools" />
  </files>
</package>
```

### 3.5 create the install script

replace `tools/chocolateyInstall.ps1`:

```powershell
$ErrorActionPreference = 'Stop'

$packageName = $env:ChocolateyPackageName
$installDir  = "C:\Program Files\LLVM-Dev"
$archiveUrl  = "\\<server>\packages\llvm-dev-libs\llvm-18.1.8-dev-win64.zip"
# for HTTP hosting, use something like:
# $archiveUrl = "https://internal-host.example.com/packages/llvm-18.1.8-dev-win64.zip"

# create install directory
if (-not (Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
}

# extract archive
Write-Host "extracting LLVM development libraries to $installDir..."
Install-ChocolateyZipPackage -PackageName $packageName `
    -Url $archiveUrl `
    -UnzipLocation $installDir

# set environment variables so CMake can find LLVM
$llvmDir  = Join-Path $installDir "lib\cmake\llvm"
$clangDir = Join-Path $installDir "lib\cmake\clang"

Write-Host "setting LLVM_DIR=$llvmDir"
Install-ChocolateyEnvironmentVariable -VariableName "LLVM_DIR" -VariableValue $llvmDir -VariableType "Machine"

Write-Host "setting Clang_DIR=$clangDir"
Install-ChocolateyEnvironmentVariable -VariableName "Clang_DIR" -VariableValue $clangDir -VariableType "Machine"

Write-Host "$packageName installed to $installDir"
Write-Host "restart your shell to pick up environment changes"
```

### 3.6 create the uninstall script

replace `tools/chocolateyUninstall.ps1`:

```powershell
$ErrorActionPreference = 'Stop'

$installDir = "C:\Program Files\LLVM-Dev"

# remove install directory
if (Test-Path $installDir) {
    Write-Host "removing $installDir..."
    Remove-Item -Path $installDir -Recurse -Force
}

# remove environment variables
Uninstall-ChocolateyEnvironmentVariable -VariableName "LLVM_DIR" -VariableType "Machine"
Uninstall-ChocolateyEnvironmentVariable -VariableName "Clang_DIR" -VariableType "Machine"

Write-Host "LLVM development libraries removed"
```

### 3.7 pack the package

```powershell
cd C:\dev\llvm-dev-libs
choco pack
```

this produces `llvm-dev-libs.18.1.8.nupkg`.

### 3.8 test locally

```powershell
# install from local nupkg
choco install llvm-dev-libs --source "C:\dev\llvm-dev-libs" -y

# verify
Test-Path "$env:LLVM_DIR\LLVMConfig.cmake"
Test-Path "$env:Clang_DIR\ClangConfig.cmake"

# uninstall
choco uninstall llvm-dev-libs -y
```

---

## phase 4: host internally

### option A: file share (simplest, for small teams)

place the `.nupkg` on a network share:

```
\\<server>\choco-packages\llvm-dev-libs.18.1.8.nupkg
```

add the share as a Chocolatey source on each developer machine:

```powershell
choco source add --name="internal" --source="\\<server>\choco-packages"
```

developers can then install with:

```powershell
choco install llvm-dev-libs --source="internal" -y
```

**limitations:** no authentication, no versioning UI, no API. fine for small teams.

### option B: ProGet (recommended for organizations)

[ProGet](https://inedo.com/proget) provides a free tier with Chocolatey-specific feeds.

1. install ProGet on an internal server
2. create a Chocolatey feed (e.g., `internal-chocolatey`)
3. push the package:

```powershell
choco push llvm-dev-libs.18.1.8.nupkg --source="https://proget.internal/nuget/internal-chocolatey" --api-key="<your-key>"
```

4. add the source on developer machines:

```powershell
choco source add --name="internal" --source="https://proget.internal/nuget/internal-chocolatey"
```

### option C: JFrog Artifactory / Sonatype Nexus

if the organization already uses Artifactory or Nexus, create a NuGet-compatible repository
and push the `.nupkg` there. the `choco source add` command is the same — just point to the
feed URL.

---

## phase 5: integrate with alchemy build system

once the package is available on an internal feed, update the alchemy build system:

### 5.1 update `Makefile` — uncomment Windows setup/teardown

```makefile
# setup target for Windows
setup:
	@echo "alchemy::Windows detected. Installing with choco..."
	@choco install -y llvm-dev-libs --source="internal" || (echo "choco install llvm-dev-libs failed"; exit 1)
	@choco install -y llvm --version=18.1.8 || (echo "choco install llvm failed"; exit 1)
	@echo "alchemy::Windows dependencies installed."
	@echo "note: restart your shell to pick up environment changes"
```

### 5.2 update `Makefile` — uncomment VCVARS detection

the VCVARS auto-detection block in the Makefile (currently commented out) should be re-enabled
so that `cmake` runs in the correct MSVC environment.

### 5.3 CMake — LLVM discovery

no CMake changes needed. `find_package(LLVM)` will find `LLVMConfig.cmake` via the `LLVM_DIR`
environment variable set by the Chocolatey install script. same for `find_package(Clang)` via
`Clang_DIR`.

---

## size and time estimates

| Item | Estimate |
|------|----------|
| LLVM source clone (shallow) | ~1 GB |
| Build artifacts (Release, X86-only) | ~5 GB |
| Install output (Release, X86-only) | ~1-3 GB |
| Compressed archive (.zip) | ~500 MB - 1 GB |
| Build time (Ninja, modern 8-core) | ~30-60 min |
| Build time (MSBuild) | ~2-3 hours |
| Chocolatey `.nupkg` (metadata only, archive hosted separately) | < 100 KB |

---

## references

- [LLVM CMake build documentation](https://llvm.org/docs/CMake.html)
- [getting started with LLVM on Visual Studio](https://llvm.org/docs/GettingStartedVS.html)
- [LLVM #53052 — Windows installer missing LLVMConfig.cmake](https://github.com/llvm/llvm-project/issues/53052)
- [LLVM #60570 — install target missing LLVMConfig.cmake](https://github.com/llvm/llvm-project/issues/60570)
- [LLVM discourse — no llvm-config.exe or LLVMConfig.cmake in pre-built Windows](https://discourse.llvm.org/t/no-llvm-config-exe-or-llvmconfig-cmake-in-pre-built-windows/57692)
- [Chocolatey — create packages](https://docs.chocolatey.org/en-us/create/create-packages/)
- [Chocolatey — Install-ChocolateyZipPackage](https://docs.chocolatey.org/en-us/create/functions/install-chocolateyzippackage/)
- [Chocolatey — host packages internally](https://docs.chocolatey.org/en-us/features/host-packages/)
- [Chocolatey — handling large packages](https://docs.chocolatey.org/en-us/guides/create/create-large-files-package/)
- [vovkos/llvm-package-windows](https://github.com/vovkos/llvm-package-windows) (reference implementation, third-party)
