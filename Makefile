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
CMFLAGS := -S. -Bbuild -GNinja -DCMAKE_TOOLCHAIN_FILE=$(CMAKE_TOOLCHAIN) $(CMAKE_LLVM_ARG) $(SANITIZER_FLAG)

# Platform detection for parallel linting
HAS_BASH := $(shell command -v bash 2>/dev/null)
HAS_FIND := $(shell command -v find 2>/dev/null)
HAS_XARGS := $(shell command -v xargs 2>/dev/null)
CAN_PARALLEL := $(and $(HAS_BASH),$(HAS_FIND),$(HAS_XARGS))

# parallelism
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

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

.PHONY: setup
ifeq ($(IS_WINDOWS),1)
setup: ## install dependencies for your platform
	@echo "alchemy::Windows detected. Installing with choco..."
	@choco install llvm14 llvm18 clang-format clang-tidy cmake ninja fmt -y >$(NULL) 2>&1 || (echo "choco install failed - ensure choco is installed: https://chocolatey.org/install"; exit 1)
	@echo "alchemy::Windows dependencies installed."
else ifeq ($(UNAME_S),Linux)
setup:
	@echo "alchemy::Linux detected. Installing with apt..."
	@sudo apt update
	@sudo apt install -y \
		clang-14 \
		clang-18 \
		clang-format \
		clang-tidy \
		llvm-14-dev \
		llvm-18-dev \
		libclang-14-dev \
		libclang-18-dev \
		cmake \
		build-essential \
		ninja-build \
		libfmt-dev
	@echo "alchemy::Linux dependencies installed."
else ifeq ($(UNAME_S),Darwin)
setup:
	@echo "alchemy::macOS detected. Installing with brew..."
	@brew install llvm@14 llvm@18 clang-format clang-tidy cmake ninja fmt || (echo "brew install failed - ensure Homebrew is installed: https://brew.sh"; exit 1)
	@echo "alchemy::macOS dependencies installed."
else
setup:
	@echo "alchemy::unsupported OS: $(UNAME_S). Manual install required."
	@exit 1
endif

.PHONY: setup.coverage
ifeq ($(IS_WINDOWS),1)
setup.coverage: ## install coverage tools (lcov) - optional dev tool
	@echo "alchemy::installing coverage tools (lcov)..."
	@choco install lcov -y >$(NULL) 2>&1 || (echo "choco install lcov failed"; exit 1)
	@echo "✅ alchemy::coverage tools installed!"
	@echo "⚠️  Note: lcov on Windows requires Git Bash or similar Unix-like shell"
else ifeq ($(UNAME_S),Linux)
setup.coverage:
	@echo "alchemy::installing coverage tools (lcov, compiler-rt)..."
	@sudo apt install -y lcov libclang-rt-18-dev
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
teardown: teardown.coverage ## remove dependencies installed by 'make setup'
	@echo "alchemy::removing dependencies (Windows)..."
	@choco uninstall llvm14 llvm18 clang-format clang-tidy cmake ninja fmt -y >$(NULL) 2>&1
	@echo "alchemy::Windows dependencies removed."
else ifeq ($(UNAME_S),Linux)
teardown: teardown.coverage
	@echo "alchemy::removing dependencies (Linux)..."
	@sudo apt remove -y \
		clang-14 \
		clang-18 \
		clang-format \
		clang-tidy \
		llvm-14-dev \
		llvm-18-dev \
		libclang-14-dev \
		libclang-18-dev \
		cmake \
		build-essential \
		ninja-build \
		libfmt-dev
	@sudo apt autoremove -y
	@echo "alchemy::Linux dependencies removed."
else ifeq ($(UNAME_S),Darwin)
teardown: teardown.coverage
	@echo "alchemy::removing dependencies (macOS)..."
	@brew uninstall llvm@14 llvm@18 clang-format clang-tidy cmake ninja fmt || true
	@echo "alchemy::macOS dependencies removed."
else
teardown: teardown.coverage
	@echo "alchemy::unsupported OS: $(UNAME_S). Manual removal required."
	@exit 1
endif

.PHONY: teardown.coverage
ifeq ($(IS_WINDOWS),1)
teardown.coverage: ## remove coverage tools installed by 'make setup.coverage'
	@echo "alchemy::removing coverage tools..."
	@choco uninstall lcov -y >$(NULL) 2>&1
	@echo "alchemy::coverage tools removed."
else ifeq ($(UNAME_S),Linux)
teardown.coverage:
	@echo "alchemy::removing coverage tools..."
	@sudo apt remove -y lcov libclang-rt-18-dev
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
	@cmake -P cmake/scripts/doctor.cmake

.PHONY: validate-clang
validate-clang: ## ensure clang is available
ifeq ($(IS_WINDOWS),1)
	@where clang++ >$(NULL) 2>&1 || (echo "alchemy::ERROR: clang++ not found - run 'make setup'"; exit 1)
	@where clang >$(NULL) 2>&1 || (echo "alchemy::ERROR: clang not found - run 'make setup'"; exit 1)
	@echo "alchemy::validation::clang found (Windows)"
	@where clang++ 2>$(NULL) || true
else
	@command -v clang++ >/dev/null 2>&1 || { echo "alchemy::ERROR: clang++ not found - run 'make setup'"; exit 1; }
	@command -v clang >/dev/null 2>&1 || { echo "alchemy::ERROR: clang not found - run 'make setup'"; exit 1; }
	@echo "alchemy::validation::clang found: $$(clang++ --version | head -n1)"
endif

# ---------- Formatting ----------

.PHONY: format
format: ## format all source code with clang-format
	@cmake -P cmake/scripts/format.cmake

.PHONY: format.check
format.check: ## check if code is properly formatted
	@cmake -P cmake/scripts/format_check.cmake

# ---------- Linting ----------

.PHONY: lint
lint: alchemy.debug ## run clang-tidy linter against project files (parallel if bash available)
	@cmake -P cmake/scripts/check_compile_commands.cmake
ifdef CAN_PARALLEL
	@echo "alchemy::running clang-tidy in parallel ($(NPROC) cores)..."
	@find inc src -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | \
	xargs -0 -n1 -P$(NPROC) -I{} bash -c ' \
		echo "  ✓ {}"; \
		clang-tidy "{}" -p=. \
			--format-style=file \
			--header-filter="^.*/alchemy/(inc|src)/.*" \
			--system-headers=false \
			--warnings-as-errors="*" \
	' || (echo "❌ alchemy::linting found issues"; exit 1)
	@echo "✅ alchemy::linting complete - no issues found!"
else
	@echo "alchemy::running clang-tidy (sequential mode)..."
	@echo "⚠️  For 4-8x faster parallel linting, install Git Bash: https://git-scm.com/downloads"
	@for file in inc/**/*.hpp src/**/*.cpp; do \
		[ -f "$$file" ] && echo "  ✓ $$file" && clang-tidy "$$file" -p=. \
			--format-style=file \
			--header-filter="^.*/alchemy/(inc|src)/.*" \
			--system-headers=false \
			--warnings-as-errors="*"; \
	done
	@echo "✅ alchemy::linting complete - no issues found!"
endif

.PHONY: lint.fix
lint.fix: ## run clang-tidy against project files with auto-fixes (parallel if bash available)
	@cmake -P cmake/scripts/check_compile_commands.cmake
ifdef CAN_PARALLEL
	@echo "alchemy::running clang-tidy with auto-fixes in parallel ($(NPROC) cores)..."
	@find inc src -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | \
	xargs -0 -n1 -P$(NPROC) -I{} bash -c ' \
		echo "  ✓ {}"; \
		clang-tidy "{}" -p=. \
			--format-style=file \
			--header-filter="^.*/alchemy/(inc|src)/.*" \
			--system-headers=false \
			--fix --fix-errors \
	'
	@echo "✅ alchemy::auto-fixes applied!"
else
	@echo "alchemy::running clang-tidy with auto-fixes (sequential mode)..."
	@for file in inc/**/*.hpp src/**/*.cpp; do \
		[ -f "$$file" ] && echo "  ✓ $$file" && clang-tidy "$$file" -p=. \
			--format-style=file \
			--header-filter="^.*/alchemy/(inc|src)/.*" \
			--system-headers=false \
			--fix --fix-errors; \
	done
	@echo "✅ alchemy::auto-fixes applied!"
endif

.PHONY: lint.test.unit
lint.test.unit: test.unit ## run clang-tidy against unit test files (parallel if bash available)
	@cmake -P cmake/scripts/check_compile_commands.cmake
	@echo "alchemy::linting unit test files in parallel..."
	@find tests/unit -type f -name "*.cpp" -print0 | \
	xargs -0 -n1 -P$(NPROC) -I{} bash -c ' \
		echo "  ✓ {}"; \
		clang-tidy "{}" -p=. --format-style=file --system-headers=false --warnings-as-errors="*" \
	' || (echo "❌ alchemy::unit linting found issues"; exit 1)
	@echo "✅ alchemy::unit linting complete!"

.PHONY: lint.test.unit.fix
lint.test.unit.fix: ## run clang-tidy against unit test files with auto-fixes (parallel if bash available)
	@cmake -P cmake/scripts/check_compile_commands.cmake
ifdef CAN_PARALLEL
	@echo "alchemy::running clang-tidy with auto-fixes in parallel ($(NPROC) cores)..."
	@find tests/unit -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | \
	xargs -0 -n1 -P$(NPROC) -I{} bash -c ' \
		echo "  ✓ {}"; \
		clang-tidy "{}" -p=. \
			--format-style=file \
			--system-headers=false \
			--fix --fix-errors \
	'
	@echo "✅ alchemy::auto-fixes applied!"
else
	@echo "alchemy::running clang-tidy with auto-fixes (sequential mode)..."
	@for file in inc/**/*.hpp src/**/*.cpp; do \
		[ -f "$$file" ] && echo "  ✓ $$file" && clang-tidy "$$file" -p=. \
			--format-style=file \
			--system-headers=false \
			--fix --fix-errors; \
	done
	@echo "✅ alchemy::auto-fixes applied!"
endif

.PHONY: lint.test.integration
lint.test.integration: test.integration ## run clang-tidy against integration test files (parallel if bash available)
	@cmake -P cmake/scripts/check_compile_commands.cmake
	@echo "alchemy::linting unit test files in parallel..."
	@find tests/integration -type f -name "*.cpp" -print0 | \
	xargs -0 -n1 -P$(NPROC) -I{} bash -c ' \
		echo "  ✓ {}"; \
		clang-tidy "{}" -p=. --format-style=file --system-headers=false --warnings-as-errors="*" \
	' || (echo "❌ alchemy::integration linting found issues"; exit 1)
	@echo "✅ alchemy::integration linting complete!"

.PHONY: lint.test.integration.fix
lint.test.integration.fix: ## run clang-tidy against integration test files with auto-fixes (parallel if bash available)
	@cmake -P cmake/scripts/check_compile_commands.cmake
ifdef CAN_PARALLEL
	@echo "alchemy::running clang-tidy with auto-fixes in parallel ($(NPROC) cores)..."
	@find tests/integration -type f \( -name "*.hpp" -o -name "*.cpp" \) -print0 | \
	xargs -0 -n1 -P$(NPROC) -I{} bash -c ' \
		echo "  ✓ {}"; \
		clang-tidy "{}" -p=. \
			--format-style=file \
			--system-headers=false \
			--fix --fix-errors \
	'
	@echo "✅ alchemy::auto-fixes applied!"
else
	@echo "alchemy::running clang-tidy with auto-fixes (sequential mode)..."
	@for file in inc/**/*.hpp src/**/*.cpp; do \
		[ -f "$$file" ] && echo "  ✓ $$file" && clang-tidy "$$file" -p=. \
			--format-style=file \
			--system-headers=false \
			--fix --fix-errors; \
	done
	@echo "✅ alchemy::auto-fixes applied!"
endif

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
	@if exist build\compile_commands.json ( copy /Y build\compile_commands.json compile_commands.json >$(NULL) ) else ( echo "alchemy::no compile_commands.json in build" )
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
	@if exist build\compile_commands.json ( copy /Y build\compile_commands.json compile_commands.json >$(NULL) ) else ( echo "alchemy::no compile_commands.json in build" )
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
	@cmake --build build --target mock_iccarm
	@cmake --build build --target mock_cl
	@cmake --build build --target unit_tests
	@(cd build && ctest --output-on-failure --verbose -L "unit_tests")
	@echo "✅ alchemy::unit tests passed!"

.PHONY: test.integration
test.integration: validate-clang ## run integration tests with mock compilers
	@echo "alchemy::running integration tests..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF
	@cmake --build build --target mock_iccarm
	@cmake --build build --target mock_cl
	@cmake --build build --target integration_tests
	@(cd build && ctest --output-on-failure --verbose -L "integration_tests")
	@echo "✅ alchemy::integration tests passed!"

.PHONY: test.all
test.all: validate-clang ## run all tests (unit + integration) with mock compilers
	@echo "alchemy::running all tests..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF
	@cmake --build build --target mock_iccarm
	@cmake --build build --target mock_cl
	@cmake --build build --target all
	@(cd build && ctest --output-on-failure)
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
	@cmake $(CMFLAGS)
	@cmake --build build
	@(cd build && ctest --output-on-failure --verbose --parallel $(NPROC))
	@echo "✅ alchemy::parallel tests passed!"

# ---------- Coverage ----------

.PHONY: coverage
coverage: validate-clang ## generate code coverage report (requires lcov)
	@command -v lcov >/dev/null 2>&1 || { echo "❌ alchemy::lcov not found - run 'make setup.coverage' first"; exit 1; }
	@echo "alchemy::building with coverage enabled..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
	@cmake --build build --target mock_iccarm
	@cmake --build build --target mock_cl
	@cmake --build build --target unit_tests
	@cmake --build build --target integration_tests
	@echo "alchemy::running tests to generate coverage data..."
	@(cd build && ctest --output-on-failure -L "unit_tests|integration_tests")
	@echo "alchemy::generating coverage report..."
	@lcov --gcov-tool $(PWD)/scripts/llvm-gcov.sh --ignore-errors inconsistent --capture --directory build --output-file build/coverage.info
	@lcov --gcov-tool $(PWD)/scripts/llvm-gcov.sh --ignore-errors inconsistent --remove build/coverage.info '/usr/*' '*/tests/*' '*/_deps/*' --output-file build/coverage_filtered.info
	@genhtml --ignore-errors inconsistent build/coverage_filtered.info --output-directory build/coverage_html
	@echo "✅ alchemy::coverage report generated!"
	@echo "   📊 Open: build/coverage_html/index.html"

.PHONY: coverage.unit
coverage.unit: validate-clang ## generate coverage for unit tests only
	@command -v lcov >/dev/null 2>&1 || { echo "❌ alchemy::lcov not found - run 'make setup.coverage' first"; exit 1; }
	@echo "alchemy::building with coverage enabled..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
	@cmake --build build --target mock_iccarm
	@cmake --build build --target mock_cl
	@cmake --build build --target unit_tests
	@echo "alchemy::running unit tests..."
	@(cd build && ctest --output-on-failure -L "unit_tests")
	@echo "alchemy::generating coverage report..."
	@lcov --gcov-tool $(PWD)/scripts/llvm-gcov.sh --ignore-errors inconsistent --capture --directory build --output-file build/coverage.info
	@lcov --gcov-tool $(PWD)/scripts/llvm-gcov.sh --ignore-errors inconsistent --remove build/coverage.info '/usr/*' '*/tests/*' '*/_deps/*' --output-file build/coverage_filtered.info
	@genhtml --ignore-errors inconsistent build/coverage_filtered.info --output-directory build/coverage_html
	@echo "✅ alchemy::unit test coverage report generated!"
	@echo "   📊 Open: build/coverage_html/index.html"

.PHONY: coverage.integration
coverage.integration: validate-clang ## generate coverage for integration tests only
	@command -v lcov >/dev/null 2>&1 || { echo "❌ alchemy::lcov not found - run 'make setup.coverage' first"; exit 1; }
	@echo "alchemy::building with coverage enabled..."
	@cmake $(CMFLAGS) -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
	@cmake --build build --target mock_iccarm
	@cmake --build build --target mock_cl
	@cmake --build build --target integration_tests
	@echo "alchemy::running integration tests..."
	@(cd build && ctest --output-on-failure -L "integration_tests")
	@echo "alchemy::generating coverage report..."
	@lcov --gcov-tool $(PWD)/scripts/llvm-gcov.sh --ignore-errors inconsistent --capture --directory build --output-file build/coverage.info
	@lcov --gcov-tool $(PWD)/scripts/llvm-gcov.sh --ignore-errors inconsistent --remove build/coverage.info '/usr/*' '*/tests/*' '*/_deps/*' --output-file build/coverage_filtered.info
	@genhtml --ignore-errors inconsistent build/coverage_filtered.info --output-directory build/coverage_html
	@echo "✅ alchemy::integration test coverage report generated!"
	@echo "   📊 Open: build/coverage_html/index.html"
