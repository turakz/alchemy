# Environment Checks:
#   run 'make doctor' to verify your environment before building
.DEFAULT_GOAL := help

# ---------- Configuration ----------

# platform detection
ifeq ($(OS),Windows_NT)
  IS_WINDOWS := 1
  NULL := NUL
else
  IS_WINDOWS := 0
  NULL := /dev/null
endif

# cmake toolchain (forward slashes are portable)
CMAKE_TOOLCHAIN := cmake/toolchains/clang.cmake

# allow user override via env
ifdef LLVM_DIR
  CMAKE_LLVM_ARG := -DLLVM_DIR=$(LLVM_DIR)
endif
ifdef Clang_DIR
  CMAKE_LLVM_ARG += -DClang_DIR=$(Clang_DIR)
endif

# sanitizer support
ifdef ASAN
  SANITIZER_FLAG := -DENABLE_SANITIZERS=ON
endif

# common cmake args
# note: CMAKE_LLVM_VER_ARG uses deferred expansion (=) because LLVM_VER
# is detected later in the Setup section
CMAKE_LLVM_VER_ARG = $(if $(LLVM_VER),-DALCHEMY_LLVM_VERSION=$(LLVM_VER))
CMFLAGS = -S. -Bbuild -GNinja -DCMAKE_TOOLCHAIN_FILE=$(CMAKE_TOOLCHAIN) $(CMAKE_LLVM_ARG) $(CMAKE_LLVM_VER_ARG) $(SANITIZER_FLAG)


# Platform detection for parallel linting
ifneq ($(IS_WINDOWS),1)
  HAS_BASH := $(shell command -v bash 2>$(NULL))
  HAS_FIND := $(shell command -v find 2>$(NULL))
  HAS_XARGS := $(shell command -v xargs 2>$(NULL))
  CAN_PARALLEL := $(and $(HAS_BASH),$(HAS_FIND),$(HAS_XARGS))
endif

# parallelism
ifeq ($(IS_WINDOWS),1)
  NPROC := $(NUMBER_OF_PROCESSORS)
else
  NPROC := $(shell nproc 2>$(NULL) || sysctl -n hw.ncpu 2>$(NULL) || echo 4)
endif

# ---------- Help ----------

.PHONY: help
help: ## show this help message
	@echo "alchemy build system - available targets:"
	@echo ""
	@grep -E '^[a-zA-Z_.-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[32m%-25s\033[0m \033[1;36m%s\033[0m\n", $$1, $$2}'
	@echo ""

# ---------- Clean ----------

.PHONY: clean
clean: ## remove build artifacts
	@cmake -P cmake/scripts/clean.cmake
	@echo "✅ alchemy::build artifacts removed!"

.PHONY: coverage.clean
coverage.clean: ## remove coverage artifacts
	@echo "alchemy::cleaning build coverage..."
	@rm -rf build/coverage.info build/coverage_filtered.info build/coverage_html
	@find build -name "*.gcda" -delete 2>/dev/null || true
	@find build -name "*.gcno" -delete 2>/dev/null || true
	@echo "✅ alchemy::coverage artifacts removed!"

# ---------- Setup ----------

# detect Unix flavor (Linux vs macOS) for setup target
ifneq ($(IS_WINDOWS),1)
  UNAME_S := $(shell uname -s 2>$(NULL) || echo Unknown)
endif

# detect Ubuntu version -> select native LLVM version
# - 20.04 (focal):  LLVM 12
# - 22.04 (jammy):  LLVM 14
# - 24.04 (noble):  LLVM 18
# coverage runtime package differs by version:
# - 20.04/22.04: libclang-common-N-dev (runtime bundled)
# - 24.04:       libclang-rt-N-dev     (standalone package)
ifeq ($(UNAME_S),Linux)
  UBUNTU_VER := $(shell lsb_release -rs 2>/dev/null)
  ifeq ($(UBUNTU_VER),20.04)
    LLVM_VER := 12
    COVERAGE_RT_PKG = libclang-common-$(LLVM_VER)-dev
  else ifeq ($(UBUNTU_VER),22.04)
    LLVM_VER := 14
    COVERAGE_RT_PKG = libclang-common-$(LLVM_VER)-dev
  else ifeq ($(UBUNTU_VER),24.04)
    LLVM_VER := 18
    COVERAGE_RT_PKG = libclang-rt-$(LLVM_VER)-dev
  else ifeq ($(UBUNTU_VER),26.04)
    LLVM_VER := 20
    COVERAGE_RT_PKG = libclang-rt-$(LLVM_VER)-dev
  endif
endif

# versioned tool binaries (Linux uses clang-tidy-N, clang-format-N)
# macOS/Windows use unversioned names (provided by brew/choco)
ifeq ($(UNAME_S),Linux)
ifdef LLVM_VER
  CLANG_TIDY := clang-tidy-$(LLVM_VER)
  CLANG_FORMAT := clang-format-$(LLVM_VER)
endif
else
  CLANG_TIDY := clang-tidy
  CLANG_FORMAT := clang-format
endif

.PHONY: setup
ifeq ($(IS_WINDOWS),1)
setup: ## install dependencies for your platform
	@echo "alchemy::Windows development (building from source) is not yet supported."
	@echo "  the official LLVM Windows installer does not include development libraries"
	@echo "  (LLVMConfig.cmake) required by find_package(LLVM)."
	@echo "  for now, please dev/build on Linux (Ubuntu 20.04, 22.04, 24.04, or 26.04)."
else ifeq ($(UNAME_S),Linux)
setup:
ifndef LLVM_VER
	@echo "alchemy::ERROR: unsupported Ubuntu version '$(UBUNTU_VER)'. Supported: 20.04, 22.04, 24.04, 26.04"
	@exit 1
endif
	@echo "alchemy::Linux (Ubuntu $(UBUNTU_VER)) detected, using LLVM $(LLVM_VER)..."
	@sudo apt update
	@sudo apt install -y \
		clang-$(LLVM_VER) \
		clang-format-$(LLVM_VER) \
		clang-tidy-$(LLVM_VER) \
		llvm-$(LLVM_VER)-dev \
		libclang-$(LLVM_VER)-dev \
		build-essential \
		libfmt-dev
	@echo "alchemy::Linux dependencies installed (LLVM $(LLVM_VER))."
else ifeq ($(UNAME_S),Darwin)
setup:
	@echo "alchemy::macOS detected. Installing with brew..."
	@brew install llvm@18 fmt || (echo "brew install failed - ensure Homebrew is installed: https://brew.sh"; exit 1)
	@echo "alchemy::macOS dependencies installed."
else
setup:
	@echo "alchemy::unsupported OS: $(UNAME_S). Manual install required."
	@exit 1
endif

.PHONY: setup.coverage
ifeq ($(IS_WINDOWS),1)
setup.coverage: ## install coverage tools (lcov) - optional dev tool
	@echo "alchemy::coverage tools are not supported on Windows (lcov is Linux-only)"
else ifeq ($(UNAME_S),Linux)
setup.coverage:
ifndef LLVM_VER
	@echo "alchemy::ERROR: unsupported Ubuntu version '$(UBUNTU_VER)'. Supported: 20.04, 22.04, 24.04, 26.04"
	@exit 1
endif
	@echo "alchemy::installing coverage tools (lcov, compiler-rt) for LLVM $(LLVM_VER)..."
	@sudo apt install -y lcov $(COVERAGE_RT_PKG)
	@echo "✅ alchemy::coverage tools installed!"
else ifeq ($(UNAME_S),Darwin)
setup.coverage:
	@echo "alchemy::installing coverage tools (lcov)..."
	@brew install lcov
	@echo "✅ alchemy::coverage tools installed!"
else
setup.coverage:
	@echo "alchemy::coverage tools not available for this platform"
endif

# ---------- Teardown ----------

.PHONY: teardown
ifeq ($(IS_WINDOWS),1)
teardown: teardown.coverage ## remove all essential depends installed by 'make setup'
	@echo "alchemy::Windows development is not yet supported. nothing to teardown."
else ifeq ($(UNAME_S),Linux)
teardown: teardown.coverage
ifndef LLVM_VER
	@echo "alchemy::ERROR: unsupported Ubuntu version '$(UBUNTU_VER)'. Supported: 20.04, 22.04, 24.04, 26.04"
	@exit 1
endif
	@echo "alchemy::removing dependencies (Linux, LLVM $(LLVM_VER))..."
	@sudo apt remove -y \
		clang-$(LLVM_VER) \
		clang-format-$(LLVM_VER) \
		clang-tidy-$(LLVM_VER) \
		llvm-$(LLVM_VER)-dev \
		libclang-$(LLVM_VER)-dev \
		build-essential \
		libfmt-dev
	@sudo apt autoremove -y
	@echo "alchemy::Linux dependencies removed."
else ifeq ($(UNAME_S),Darwin)
teardown: teardown.coverage
	@echo "alchemy::removing dependencies (macOS)..."
	@brew uninstall llvm@18 fmt
	@echo "alchemy::macOS dependencies removed."
else
teardown: teardown.coverage
	@echo "alchemy::unsupported OS: $(UNAME_S). Manual removal required."
	@exit 1
endif

.PHONY: teardown.coverage
ifeq ($(IS_WINDOWS),1)
teardown.coverage: ## remove coverage tools installed by 'make setup.coverage'
	@echo "alchemy::coverage tools are not supported on Windows (lcov is Linux-only)"
else ifeq ($(UNAME_S),Linux)
teardown.coverage:
ifndef LLVM_VER
	@echo "alchemy::ERROR: unsupported Ubuntu version '$(UBUNTU_VER)'. Supported: 20.04, 22.04, 24.04, 26.04"
	@exit 1
endif
	@echo "alchemy::removing coverage tools..."
	@sudo apt remove -y lcov $(COVERAGE_RT_PKG)
	@sudo apt autoremove -y
	@echo "alchemy::coverage tools removed."
else ifeq ($(UNAME_S),Darwin)
teardown.coverage:
	@echo "alchemy::removing coverage tools..."
	@brew uninstall lcov || true
	@echo "alchemy::coverage tools removed."
else
teardown.coverage:
	@echo "alchemy::coverage tools not available for this platform"
endif

# ---------- Environment Checks ----------

.PHONY: doctor
doctor: ## run environment diagnostics
ifdef LLVM_VER
	@cmake -DLLVM_VER=$(LLVM_VER) -P cmake/scripts/doctor.cmake
else
	@cmake -P cmake/scripts/doctor.cmake
endif

.PHONY: validate-clang
validate-clang: ## ensure clang is available
ifeq ($(IS_WINDOWS),1)
	@where clang++ || (echo "alchemy::ERROR: clang++ not found - run 'make setup'"; exit 1)
	@where clang || (echo "alchemy::ERROR: clang not found - run 'make setup'"; exit 1)
	@echo "alchemy::validation::clang found (Windows)"
else ifdef LLVM_VER
	@command -v clang++-$(LLVM_VER) >/dev/null 2>&1 || { echo "alchemy::ERROR: clang++-$(LLVM_VER) not found - run 'make setup'"; exit 1; }
	@command -v clang-$(LLVM_VER) >/dev/null 2>&1 || { echo "alchemy::ERROR: clang-$(LLVM_VER) not found - run 'make setup'"; exit 1; }
	@echo "alchemy::validation::clang found: $$(clang++-$(LLVM_VER) --version | head -n1)"
else
	@command -v clang++ >/dev/null 2>&1 || { echo "alchemy::ERROR: clang++ not found - run 'make setup'"; exit 1; }
	@command -v clang >/dev/null 2>&1 || { echo "alchemy::ERROR: clang not found - run 'make setup'"; exit 1; }
	@echo "alchemy::validation::clang found: $$(clang++ --version | head -n1)"
endif

# ---------- Formatting ----------

.PHONY: format
format: ## format all source code with clang-format
	@cmake -DCLANG_FORMAT_BIN=$(CLANG_FORMAT) -P cmake/scripts/format.cmake

.PHONY: format.check
format.check: ## check if code is properly formatted
	@cmake -DCLANG_FORMAT_BIN=$(CLANG_FORMAT) -P cmake/scripts/format_check.cmake

# ---------- Linting ----------

# parameterized clang-tidy runner
# $(1) = directories to scan
# $(2) = extra clang-tidy flags (e.g., --warnings-as-errors="*" or --fix --fix-errors)
# $(3) = label for status messages
define run_clang_tidy
	@test -f build/compile_commands.json || { echo "❌ compile_commands.json not found - build the project first"; exit 1; }
	@echo "alchemy::running clang-tidy on $(3) ($(NPROC) cores)..."
	@find $(1) -type f -name "*.cpp" -print0 | \
	xargs -0 -n1 -P$(NPROC) -I{} bash -c ' \
		echo "  ✓ {}"; \
		$(CLANG_TIDY) "{}" -p=. \
			--format-style=file \
			--system-headers=false \
			$(2) \
	' || (echo "❌ alchemy::linting found issues"; exit 1)
	@echo "✅ alchemy::linting complete!"
endef

.PHONY: lint
lint: alchemy.debug ## run clang-tidy linter against project files
	$(call run_clang_tidy,inc src,--header-filter="^.*/alchemy/(inc|src)/.*" --warnings-as-errors="*",project files)

.PHONY: lint.fix
lint.fix: ## run clang-tidy with auto-fixes on project files
	$(call run_clang_tidy,inc src,--header-filter="^.*/alchemy/(inc|src)/.*" --fix --fix-errors,project files)

.PHONY: lint.test.unit
lint.test.unit: test.unit ## run clang-tidy against unit test files
	$(call run_clang_tidy,tests/unit,--warnings-as-errors="*",unit tests)

.PHONY: lint.test.unit.fix
lint.test.unit.fix: ## run clang-tidy with auto-fixes on unit test files
	$(call run_clang_tidy,tests/unit,--fix --fix-errors,unit tests)

.PHONY: lint.test.integration
lint.test.integration: test.integration ## run clang-tidy against integration tests
	$(call run_clang_tidy,tests/integration,--warnings-as-errors="*",integration tests)

.PHONY: lint.test.integration.fix
lint.test.integration.fix: ## run clang-tidy with auto-fixes on integration tests
	$(call run_clang_tidy,tests/integration,--fix --fix-errors,integration tests)

# ---------- Quality Checks ----------

.PHONY: check
check: format.check lint ## run all quality checks (format + lint)
	@echo "✅ alchemy::all quality checks passed!"

# ---------- Build ----------

.PHONY: alchemy.debug
alchemy.debug: validate-clang ## build debug binary
	@echo "alchemy::configuring and building (Debug)..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF
	@cmake --build build --target alchemy
ifeq ($(IS_WINDOWS),1)
	@if exist build\compile_commands.json ( copy /Y build\compile_commands.json compile_commands.json ) else ( echo "alchemy::no compile_commands.json in build" )
else
	@if [ -e build/compile_commands.json ]; then ln -sf build/compile_commands.json; fi
endif
	@echo "✅ alchemy::debug build complete!"

.PHONY: alchemy.release
alchemy.release: validate-clang ## build release binary
	@echo "alchemy::configuring and building (Release)..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Release -DENABLE_COVERAGE=OFF
	@cmake --build build --target alchemy
ifeq ($(IS_WINDOWS),1)
	@if exist build\compile_commands.json ( copy /Y build\compile_commands.json compile_commands.json ) else ( echo "alchemy::no compile_commands.json in build" )
else
	@if [ -e build/compile_commands.json ]; then ln -sf build/compile_commands.json; fi
endif
	@echo "✅ alchemy::release build complete!"

# ---------- Tests ----------
.PHONY: mock_iccarm
mock_iccarm:
	@cmake --build build --target mock_iccarm

.PHONY: mock_cl
mock_cl:
	@cmake --build build --target mock_cl

.PHONY: test.unit
test.unit: validate-clang ## run unit tests with mock compilers
	@echo "alchemy::running unit tests..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF
	@cmake --build build --target unit_tests
	@(cd build && ctest --output-on-failure --verbose -L "unit_tests")
	@echo "✅ alchemy::unit tests passed!"

.PHONY: test.integration
test.integration: validate-clang ## run integration tests with mock compilers
	@echo "alchemy::running integration tests..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF
	@cmake --build build --target integration_tests
	@(cd build && ctest --output-on-failure --verbose -L "integration_tests")
	@echo "✅ alchemy::integration tests passed!"

.PHONY: test.all
test.all: validate-clang ## run all tests (unit + integration)
	@echo "alchemy::running all tests..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF
	@cmake --build build --target unit_tests --target integration_tests
	@(cd build && ctest --output-on-failure -L "unit_tests|integration_tests")
	@echo "✅ alchemy::all tests passed!"

.PHONY: test.performance
test.performance: validate-clang ## run performance tests
	@echo "alchemy::running performance tests..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Release # force release
	@cmake --build build --target performance_tests
	@(cd build && ctest --output-on-failure --verbose -L "performance_tests")
	@echo "✅ alchemy::performance tests passed!"

.PHONY: test.performance.stress
test.performance.stress: validate-clang ## run performance stress tests
	@echo "alchemy::running performance stress tests..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Release
	@cmake --build build --target performance_stress_tests
	@(cd build && ctest --output-on-failure --verbose -L "performance_stress_tests")
	@echo "✅ alchemy::performance stress tests passed!"

.PHONY: test.parallel
test.parallel: validate-clang ## run tests in parallel
	@echo "alchemy::running tests in parallel..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF
	@cmake --build build
	@(cd build && ctest --output-on-failure --verbose --parallel $(NPROC))
	@echo "✅ alchemy::parallel tests passed!"

# ---------- Coverage ----------

# parameterized coverage runner
# $(1) = test targets to build (space-separated)
# $(2) = ctest label filter
# $(3) = label for status messages
define run_coverage
	@command -v lcov >/dev/null 2>&1 || { echo "❌ alchemy::lcov not found - run 'make setup.coverage' first"; exit 1; }
	@echo "alchemy::building with coverage enabled..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
	@for target in $(1); do cmake --build build --target $$target; done
	@echo "alchemy::running $(3)..."
	@(cd build && ctest --output-on-failure -L "$(2)")
	@echo "alchemy::generating coverage report..."
	@lcov --gcov-tool $(CURDIR)/scripts/llvm-gcov.sh --ignore-errors inconsistent --capture --directory build --output-file build/coverage.info
	@lcov --gcov-tool $(CURDIR)/scripts/llvm-gcov.sh --ignore-errors inconsistent --remove build/coverage.info '/usr/*' '*/tests/*' '*/_deps/*' '*/external/*' --output-file build/coverage_filtered.info
	@genhtml --ignore-errors inconsistent build/coverage_filtered.info --output-directory build/coverage_html
	@echo "✅ alchemy::$(3) coverage report generated!"
	@echo "   📊 Open: build/coverage_html/index.html"
endef

.PHONY: coverage
coverage: validate-clang ## generate code coverage report (requires lcov)
	$(call run_coverage,unit_tests integration_tests,unit_tests|integration_tests,all tests)

.PHONY: coverage.unit
coverage.unit: validate-clang ## generate coverage for unit tests only
	$(call run_coverage,unit_tests,unit_tests,unit tests)

.PHONY: coverage.integration
coverage.integration: validate-clang ## generate coverage for integration tests only
	$(call run_coverage,integration_tests,integration_tests,integration tests)
