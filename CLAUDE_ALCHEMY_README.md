# Alchemy Project Context for Claude Code

## 🚀 REQUIRED READING ON SESSION START
**Read these files in order when starting any Alchemy session:**
1. This file (CLAUDE_ALCHEMY_README.md)
2. `PITM.md` - Pedagogical principles to reinforce

---

## 🎯 Project Mission
**Alchemy** - C++ refactoring and code generation tool built on libclang/LLVM
- Analyze C source files and perform struct alignment optimizations
- Cross-compiler support: GCC, Clang, MSVC, IAR (embedded ARM)
- Current version: v0.1.0-alpha (struct alignment refactoring)

---

## 🏗️ Architecture: The Mental Model

### Core Pattern: Layered Data Transformation Pipeline
```
User Input (CLI)
    ↓
App Orchestration (creates parser + operations, reports results)
    ↓
Pipeline (stateless: parse → execute → transmute)
    ↓
├─ Parsing (source files → structured data)
├─ Operations (structured data → transformation recipes)
└─ Transmute (recipes → file mutations)
```

### Key Design Patterns In Use
- **CRTP-based operations** - Static polymorphism (zero-cost abstraction)
- **Variant-based polymorphism** - `std::variant` + `std::visit` for type-safe dispatch
- **Template-based pipeline** - Enables dependency injection for testing
- **Result<T> monad** - Consistent error handling across all layers
- **Atomic writes** - Temp file + rename (prevents corruption)
- **Pre-flight validation** - Fail-fast before any file modifications

### PITM Principles Manifested
- **Testability First**: 308 unit + 63 integration = 371 total (all passing)
- **Loose Coupling**: Clear layer boundaries, dependency injection via templates
- **Data-Centric**: `ParseResults`, `Recipe` variants, `Metrics` flow through system
- **Design for Testability**: Can test logic without running full pipeline
- **Test Coverage**: run `make coverage` to generate (requires lcov)

---

## 📁 Current File Structure (v0.1.0-alpha)

<details>
<summary>inc/ - Public Headers (click to expand)</summary>

```
inc/
├── app/
│   ├── app.hpp                  # App orchestrator (parser creation, pipeline execution, reporting)
│   ├── color.hpp                # ANSI color codes for console output
│   ├── version.hpp.in           # CMake configure_file template -> generated/app/version.hpp at build time
│   └── core/
│       ├── core.hpp             # Result<T> monad, Error::format()
│       └── utils/
│           └── utils.hpp        # extractVariantFrom<T>() template
├── cli/
│   ├── cli.hpp                  # CLI parsing + Validator + mergeInputs
│   └── config_parser.hpp        # Config file loading (loadConfig) + dumping (dumpConfig)
├── config/
│   └── config.hpp               # AppConfig, SourceInventory
├── files/
│   └── discovery.hpp            # File discovery and glob expansion
├── metrics/
│   ├── metrics.hpp              # Metrics variant type
│   └── salign_metrics.hpp       # SAlignMetrics (cache waste, cache utilization)
├── operation/
│   ├── operation_base.hpp       # RefactorRecipe, Recipe variant (single type), RecipeOperationResult
│   ├── operation.hpp            # RecipeOperation variant (single type: StructAlignmentOperation)
│   └── refactoring/
│       └── salign_operation.hpp # StructAlignmentOperation (struct alignment optimization)
├── parsing/
│   ├── parser.hpp               # ParsingRuleAdapter interface
│   ├── parsing_requirements.hpp # ParsingRequirements (operation parsing needs)
│   ├── artifacts/
│   │   └── artifacts.hpp        # ParseResults, StructDef, FieldDef (canonicalTypeName, sourceTypeName, trailingComment)
│   └── libclang/
│       ├── clang_parser.hpp                 # ClangParser implementation (ClangTool runner)
│       ├── clang_parsing_rules.hpp          # ClangParsingMatcher base class
│       ├── clang_struct_extractor.hpp       # StructExtractor (static utility, pure extraction logic)
│       ├── clang_struct_parsing_rule.hpp    # ClangStructParsingRule (AST matching)
│       └── compiler_adapters/
│           ├── clang_compilation_database_adapter.hpp   # Adapter wrapping libclang CompilationDatabase
│           ├── clang_compilation_database_factory.hpp   # Factory: compiler detection → translator → adapter
│           ├── compiler_utils.hpp                       # Shared compiler adapter utilities
│           │                                            # - translateDb(): template for translating all commands
│           │                                            # - executeCompilerCommand(): shared compiler query execution
│           │                                            # - parseIncludeFlags(): extracts -I/-isystem values from args
│           │                                            # - extractIncludePaths(): union of include paths across commands
│           │                                            # - inferMissingIncludeFlags(): per-file -I flag dedup
│           ├── gcc_database_translator.hpp              # GccDbTranslator (GCC → Clang translation)
│           │                                            # - Flag translation map (GCC warnings → Clang equivalents)
│           │                                            # - GCC-only flag stripping and prefix matching
│           │                                            # - validateTargetTriple(): validates target triples for clang
│           ├── iar_database_translator.hpp              # IarDbTranslator (IAR → Clang translation)
│           │                                            # - Query-driver approach: queries actual compiler for system includes
│           │                                            # - Architecture detection: M0/M1/M3/M4/M7/M23/M33 + FPU variants
│           │                                            # - Allowlist approach: only keeps -D/-I/-U and source files
│           │                                            # - Intrinsic keyword stubs, compatibility defines
│           └── msvc_database_translator.hpp             # MsvcDbTranslator (MSVC → Clang translation)
│                                                        # - MSVC flag translation (/I → -I, /D → -D, etc.)
├── pipeline/
│   ├── pipeline.hpp             # Template-based pipeline functions
│   └── preflight_validator.hpp  # Pre-flight validation (file existence, write permissions)
├── reporting/
│   ├── metrics_reporter.hpp     # Metrics variant dispatcher
│   └── salign_reporter.hpp      # SAlignReporter (three-level display: summary, struct, field)
├── logger/
│   └── logger.hpp               # Logger singleton (configurable log levels, elapsed time tracking)
└── transmute/
    └── transmute.hpp            # applyRecipes, applyRefactor (atomic file writes)
```
</details>

<details>
<summary>src/ - Implementations (click to expand)</summary>

```
src/
├── main.cpp                     # Entry point
├── app/
│   └── app.cpp                  # Parser creation, pipeline execution, metrics reporting, post-mutation warnings
├── cli/
│   ├── cli.cpp                  # LLVM adapter + Validator + mergeInputs implementation
│   └── config_parser.cpp        # Config file loading + TOML parsing + dumpConfig
├── files/
│   └── discovery.cpp            # File discovery implementation
├── operation/
│   └── refactoring/
│       └── salign_operation.cpp     # StructAlignmentOperation implementation
│                                    # - computeCacheMetrics: cache waste, cache utilization
│                                    # - sortFieldsByAlignment: reorders fields by alignment (descending)
│                                    # - analyzeStruct: returns StructOptimization (metrics + recipes)
│                                    # - recipe generation: source-faithful type names, array syntax, trailing comments
│                                    # - non-reorderable field bailout (bitfields, anonymous unions)
├── parsing/
│   ├── artifacts/
│   │   └── artifacts.cpp            # ParseResults implementation
│   └── libclang/
│       ├── clang_parser.cpp         # ClangParser implementation
│       │                            # - create(): compiler detection, exclude filter, dep graph, parse command resolution
│       │                            # - filterExcludedCommands(): strips TUs matching exclude patterns
│       │                            # - buildReverseDependencyMap(): clang -MM to build header→TU dep graph (parallel)
│       │                            # - buildParseGraph(): per-TU dep graph worker (called from threads)
│       │                            # - resolveParseCommands(): maps user files to ParseCommands via dep graph
│       │                            # - run(ParseCommand): ClangTool execution with soft-skip on clang parse errors
│       │                            # - run(header): direct-parse fallback via fallbackDb + include injection
│       ├── clang_struct_extractor.cpp # StructExtractor (field/struct extraction from AST)
│       │                              # - extractField(): byte range, trailing comment capture, anonymous type detection
│       │                              # - extractStruct(): macro-expanded type location fix (getExpansionLoc), source type name extraction
│       ├── clang_struct_parsing_rule.cpp # ClangStructParsingRule (field extraction)
│       └── compiler_adapters/
│           ├── clang_compilation_database_adapter.cpp  # Adapter delegates to underlying CompilationDatabase
│           ├── clang_compilation_database_factory.cpp  # Factory: compiler detection → translator → adapter
│           │                                           # - fromBuildDir(): detect → translate → adapt
│           ├── compiler_utils.cpp                      # Shared compiler adapter utilities
│           │                                           # - executeCompilerCommand(), translateDb()
│           │                                           # - parseIncludeFlags(): core -I/-isystem parser (used by both below)
│           │                                           # - extractIncludePaths(), inferMissingIncludeFlags()
│           ├── gcc_database_translator.cpp             # GCC → Clang translation
│           │                                           # - gccToClangTranslation(): warning flag mapping
│           │                                           # - gccFlags(): known GCC-only flags to strip
│           │                                           # - isGccFlagPrefix(): GCC-only flag prefix matching
│           │                                           # - validateTargetTriple(): validates GCC -dumpmachine triples for clang
│           │                                           # - strips resource-dir suppressors in translateCommand()
│           ├── iar_database_translator.cpp             # IAR → Clang translation (query-driver approach)
│           │                                           # - Allowlist: only keeps -D/-I/-U and source files
│           │                                           # - querySystemIncludes(): exec compiler -E -xc -v
│           │                                           # - deriveArchitectureDefines(): M0/M1/M3/M4/M7/M23/M33
│           │                                           # - getClangCompatibileDefines(): keyword stubs (13 IAR intrinsics)
│           └── msvc_database_translator.cpp            # MSVC → Clang translation
│                                                       # - translateCommand(): /I → -I, /D → -D, /TC → -x c
├── pipeline/
│   ├── pipeline.cpp             # Non-template helpers only
│   │                            # - executeTransmute: apply recipes to files
│   │                            # - validateTransmute: validate before transmutation
│   │                            # - transmute: orchestrates validation + execution
│   └── preflight_validator.cpp  # Pre-flight validation implementation
├── reporting/
│   ├── metrics_reporter.cpp         # Metrics variant dispatcher (separates by type)
│   └── salign_reporter.cpp          # SAlignReporter implementation (three-level display)
├── logger/
│   └── logger.cpp                   # Logger implementation (singleton, elapsed time, scoped indent)
└── transmute/
    └── transmute.cpp                # Recipe variant dispatcher + type-specific functions
                                     # - applyRefactor(): apply RefactorRecipe with atomic writes
                                     # - applyRecipes(): variant dispatcher
```
</details>

<details>
<summary>tests/ - Test Structure (click to expand)</summary>

```
tests/
├── unit/                                        # 308 unit tests (mock all dependencies)
│   ├── test_app.cpp                             # App orchestrator tests - 10 tests
│   ├── test_cli.cpp                             # CLI validation tests - 19 tests
│   │                                            # - Validator: feature flags, jobs, dry-run, source patterns
│   │                                            # - mergeInputs(): CLI-over-config precedence rules for all fields
│   ├── test_config_parser.cpp                   # Config loading + mergeInputs + dumpConfig tests - 17 tests
│   ├── test_clang_parser.cpp                    # ClangParser tests - 9 tests
│   ├── test_clang_struct_extractor.cpp          # StructExtractor tests - 10 tests
│   ├── test_clang_struct_parsing_rule.cpp       # ClangStructParsingRule tests - 5 tests
│   ├── test_compilation_database_adapter.cpp    # Compiler adapter tests - 72 tests
│   │                                            # - GCC/IAR/MSVC compiler detection
│   │                                            # - GCC flag translation (warning flags → clang equivalents)
│   │                                            # - GCC flag stripping and prefix matching
│   │                                            # - GCC validateTargetTriple (known/unknown architectures)
│   │                                            # - resource-dir suppressor stripping (-nostdinc, -nostdinc++, etc.)
│   │                                            # - GCC architecture flag normalization + pass-through (-mcpu, -mthumb, etc.)
│   │                                            # - IAR allowlist (keeps -D/-I/-U, drops everything else)
│   │                                            # - IAR architecture-specific defines (M0/M3/M33/fallback)
│   │                                            # - IAR compatibility defines (13 intrinsic keyword stubs)
│   │                                            # - MSVC flag translation (/I → -I, /D → -D)
│   │                                            # - Error paths (empty DB, invalid compiler, non-zero exit)
│   │                                            # - compiler_utils edge cases (inferMissingIncludeFlags, findArgInsertionPoint)
│   ├── test_core_types.cpp                      # Result<T> tests - 11 tests (includes rvalue tests)
│   ├── test_discovery.cpp                       # File discovery tests - 15 tests
│   │                                            # - Glob pattern matching (wildcard, recursive, zero-depth)
│   │                                            # - globToRegex edge cases (bare **, ?)
│   │                                            # - findCommonAncestor edge cases (empty, disjoint paths)
│   │                                            # - buildDiscoveryConfig extension cleanup
│   ├── test_operation.cpp                       # StructAlignmentOperation tests - 34 tests
│   │                                            # - Tests public API (getRequirements, getName, operator())
│   │                                            # - Tests computeCacheMetrics, computeSize helpers
│   │                                            # - Non-reorderable field bailout in swap zone
│   ├── test_pipeline.cpp                        # Pipeline tests - 15 tests
│   │                                            # - Uses TestRecipeOperation variant (CRTP-based mocks)
│   │                                            # - Tests: executeOperations, runParser, execute
│   ├── test_pipeline_transmute.cpp              # Pipeline transmute integration - 14 tests
│   ├── test_preflight_validator.cpp             # Pre-flight validation tests - 16 tests
│   ├── test_salign_invariants.cpp               # SAlign invariant tests - 11 tests
│   ├── test_salign_reporter.cpp                 # SAlignReporter tests - 30 tests
│   │                                            # - Array Hint rendering when optimizedCacheSize > optimizedSize
│   └── test_transmute.cpp                       # Transmutation tests - 12 tests
├── integration/                                 # 63 integration tests (end-to-end)
│   ├── test_clang_parser.cpp                    # Parser integration tests - 11 tests
│   │                                            # - Realistic compilation databases (GCC/Clang/IAR/MSVC)
│   │                                            # - Mock compiler execution
│   │                                            # - Full parser pipeline with translated commands
│   │                                            # - Exclude pattern filtering (non-matching / partial / total)
│   ├── test_logger.cpp                          # Logger integration tests - 14 tests
│   │                                            # - Log level filtering, elapsed time output
│   │                                            # - Scoped indent, debug/verbose modes
│   └── salign/
│       ├── test_salign_errors.cpp               # Error handling and pre-flight validation - 7 tests
│       ├── test_salign_pipeline.cpp             # End-to-end pipeline tests - 10 tests
│       └── test_salign_recipes.cpp              # Recipe generation behavior - 21 tests
│       │                                        # - Array syntax, include guard preservation
│       │                                        # - Bool source spelling, trailing comment preservation
├── performance/                                 # Performance benchmarks
│   ├── baseline.md                              # Performance baseline documentation
│   ├── test_salign.cpp                          # Basic performance tests
│   ├── test_salign_complexity.cpp               # Complexity scaling tests
│   ├── test_salign_parsing.cpp                  # Parsing performance tests
│   ├── test_salign_realistic.cpp                # Realistic workload tests
│   └── test_salign_stress.cpp                   # Stress tests
├── data/
│   ├── integration/
│   │   └── salign/
│   │       └── salign_test_fixture.hpp          # Shared test fixtures for salign integration tests
│   ├── mock_compilers/                          # Mock compiler binaries for testing
│   │   ├── mock_cl.cpp                          # Mock MSVC compiler (outputs include paths)
│   │   ├── mock_iccarm.cpp                      # Mock IAR compiler (outputs include paths)
│   │   └── CMakeLists.txt                       # Build configuration for mock compilers
│   └── shared_project/                          # Shared test project for parser integration tests
│       ├── main.c
│       ├── inc/                                 # Headers: config.h, utils.h, header_only.h,
│       │                                        #          std_types.h (size_t/uint8_t/bool without includes),
│       │                                        #          task_private.h (no matching TU in dep graph)
│       └── src/                                 # Sources: config.c, utils.c, std_types.c, task.c
├── utils.hpp                                    # Test utilities (createCliInputs, mock helpers, createRecipe)
├── utils.cpp                                    # Test utilities implementation (createGccDatabase, createIarDatabase, createMsvcDatabase)
└── CMakeLists.txt                               # Test build configuration
```
</details>

<details>
<summary>docs/design/ - Architecture Documentation (click to expand)</summary>

```
docs/design/
├── README.md                                # Design documentation index
├── component-diagrams.md                    # Component architecture diagrams
├── sequence-diagrams.md                     # Interaction flow diagrams
├── pahole-verification.md                   # Pahole post-rebuild verification proposal (NOT YET IMPLEMENTED)
├── salignment-metrics-enhancement.md        # SAlign metrics enhancement notes
└── small_vec_idea_land.md                   # SmallVec exploration notes
```
</details>

---

## ⚙️ Hard Requirements (Non-Negotiable)

1. **Header/Implementation Parity**: Every function must have both `.hpp` declaration AND `.cpp` definition
   - Exception: Template functions (always in headers)

2. **No Anonymous Namespaces**: Zero usage, anywhere

3. **Fully Qualified Names**: No namespace aliases in implementation files
   - Use `alchemy::core::Result`, not `using namespace` or aliases

4. **Template Functions in Headers**: Never in `.cpp` files

5. **Include Order**: Always `self-contained header` -> std → 3rd party → local`
   - Implementation files include their header first, then dependencies alphabetically

6. **Unit Tests Mock Dependencies When Reasonable**: Prefer component isolation, test API contracts only
   - Unit tests verify inputs → outputs (contract testing), edge cases, error paths, exceptions thrown
   - Integration tests verify component behavior (functionality testing) -> this can be a component in isolation or end-to-end

7. **Explicit Newlines**: `fmt::print` doesn't add them - include `\n` explicitly

8. **Documentation-Driven Decision Making**: Base all technical claims on verified facts, not probabilistic guessing
   - **Consult official documentation** before making claims about compiler flags, language features, or library behavior
   - **State uncertainty explicitly** when you don't know something ("I don't know which flags overlap - let me check the documentation")
   - **Verify assumptions** with searches or empirical tests rather than guessing based on probability
   - **Push back only with evidence** - disagree with concrete facts/documentation, not just confidence
   - **No speculation** - "GCC might support this" should be "Let me check the GCC documentation to verify"
   - **Transparency over correctness** - admitting uncertainty is better than misleading with confident guesses

---

## 🤝 How We Work Together

### The 6-Step Interactive Workflow
When proposing ANY code change (refactoring, feature, bug fix):

1. **Propose** - Detailed explanation of what and why
2. **Show Examples** - Before/after code with real examples from codebase
3. **Explain Reasoning** - Pros/cons, trade-offs, alternatives
4. **User Reviews** - User decides what to implement
5. **User Implements** - User writes code, runs tests
6. **Review Together** - Discuss implementation, ensure tests pass

### Key Working Principles
- **You're my mentor, not a sycophant, not a people-pleaser** - Disagree when I'm wrong, prioritize correctness over validation
- **No black-box implementations or Agentic workflows** - Interactive, walkthrough-based design
- **Test failures = broken code first** - Examine application code before blaming tests
- **One change at a time** - No batching multiple refactorings
- **Abstractions emerge** - Don't over-engineer upfront, let patterns emerge from actual need
- **It's ok to be methodical** - Don't rush, or take short-cuts and get ahead of yourself

### What Works Best
- **Show, don't tell** - Code examples with reasoning, especially if they can be mapped to industry standards
- **Think out loud** - Share design considerations openly
- **Ask targeted questions** - Specific decisions, not vague requests
- **Connect to principles** - Explain how changes improve testability/coupling/maintainability
- **Reinforce PITM practices** - Emphasize testability, loose coupling, data-centric design

---

## 🔧 Build & Test Info

- **Build System**: CMake Presets + Makefile targets
  - Uses `CMakePresets.json` (version 3, CMake 3.21+) for configuration management
  - Per-configuration build directories: `build/debug/`, `build/release/`, `build/coverage/`, `build/sanitizers/`
  - Switching between Debug/Release does not invalidate the other's build cache
  - `make alchemy.debug` - Debug build (output: `build/debug/alchemy`)
  - `make alchemy.release` - Release build (output: `build/release/alchemy`)
  - `make test.unit` - Run unit tests
  - `make test.integration` - Run integration tests
  - `make test.all` - Run all tests
  - `make test.parallel` - Run all tests in parallel
  - `ASAN=1 make test.unit` - Run with AddressSanitizer + UBSan (uses `build/sanitizers/`)
  - `IN_DOCKER=1 make <target>` - Run any target inside the alchemy-ci Docker container (requires `make docker.build` first)
  - `compile_commands.json` symlink at project root points to `build/debug/`
- **Test Coverage**: 308 unit + 63 integration = 371 tests (all passing)
  - Line coverage: 89.9% (2868/3190 lines), function coverage: 92.2% (553/600)
  - Run `make coverage` to generate line/function coverage reports (requires lcov, or use `IN_DOCKER=1`)
- **Current Features**:
  - `--version` - Print alchemy version (`alchemy v0.1.0-alpha`) and exit
  - `--salign` - Struct alignment optimization
  - `--dry-run` - Preview without writing files
  - `--build-dir` / `-b` - Specify build directory
  - `--output-dir` / `-o` - Specify output directory
  - `--jobs` / `-j` - Thread count (0 = auto-detect)
  - `--exclude` - Exclude file patterns
  - `--dump-config` - Write resolved CLI args to `.alchemy/alchemy.toml`
  - `--debug` - Enable debug logging; creates `.alchemy/alchemy.log` (no log file without this flag)
- **Configuration File** (`.alchemy/alchemy.toml`):
  - Automatically loaded if present in project root (cwd)
  - CLI args take precedence over config values
  - `--dump-config` generates a config from the current CLI invocation
  - Long CLI flag names match TOML keys exactly for zero cognitive overhead
  - Example:
    ```toml
    [options]
    build-dir = "./build"
    sources = ["src/*.c", "lib/*.c"]
    exclude = ["test/*"]
    salign = true
    jobs = 4
    dry-run = false
    ```
- **Code Style**:
  - Indentation: 2 spaces, no tabs
  - Comments: Lowercase except proper nouns

---


## 📚 Additional Context Files

- **PITM.md** - MUST READ: Pedagogical principles (testability, loose coupling, incremental development)

---

## 💡 Key Architectural Insights

### Separation of Concerns (Layers)
- `app/` - I/O and orchestration (file writing, console output, metrics reporting)
- `cli/` - Command-line parsing (separate from app orchestration)
- `pipeline/` - Pure data transformation (no I/O, testable logic)
- `parsing/` - Source → structured data (no recipe generation)
- `operation/` - Structured data → transformation recipes (no parsing)
- `transmute/` - Recipes → file mutations (writing only)
- `reporting/` - Metrics output formatting (no business logic)

### Dependency Flow
- High-level depends on low-level: `app → pipeline → {parsing, operations, transmute}`
- Core types (`app/core.hpp`) used across all layers
- Parser artifacts passed by const reference (parse once, no copying)

### Testability Strategy
- **CRTP-based operations**: Structural polymorphism enables mock operations with same shape as production
- **Template-based pipeline**: Generic over operation variant type, enables dependency injection
- **Test-specific variants**: `TestRecipeOperation` contains only mocks, no production types
- **Mock operations**: CRTP-based mocks with structural parity to production operations
- **True isolation**: Unit tests mock all dependencies, test API contracts
- **Integration tests**: End-to-end tests with real components verify behavior

---

## 🎓 Learning Context (PITM Integration)

This project is a learning vehicle for applying professional embedded/systems software engineering practices:

### Core Practices Being Reinforced
1. **Design for Testability** - Structure code so logic can be tested without hardware/full system
2. **Loose Coupling** - Separate concerns with clear boundaries (layers, abstractions)
3. **Data-Centric Thinking** - Represent state in central, testable models (variants, Result<T>)
4. **Incremental Development** - Build one piece at a time, test continuously
5. **Pattern Application** - Use consistent patterns (CRTP, variant dispatch, monads)

### Cognitive Load Management Strategies
- **Externalize memory** - Write down structure before coding
- **Build incrementally** - Test each piece before moving to next
- **Mental templates** - Develop templates through repetition (3-5 implementations to internalize)
- **Copy-paste-modify** - Reuse working code structure, modify for new context

### Core Question for Every Design Decision
"Can I test this logic without running the whole system?"
