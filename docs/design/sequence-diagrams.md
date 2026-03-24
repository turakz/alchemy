# Alchemy Sequence Diagrams (v1.0.0-alpha)

High-level execution flows showing component interactions.

**Current state (v1.0.0-alpha)**:
- Operations are plain classes with duck-typed interface: `getRequirements()`, `getName()`, `execute()`
- `std::visit` calls methods directly on variant alternatives (compile-time dispatch)
- IAR translator uses query-driver approach for system include paths
- Single feature: struct alignment refactoring (`--salign`)

---

## Complete Execution Flow

```mermaid
sequenceDiagram
    participant main
    participant App
    participant CLI
    participant Discovery
    participant Pipeline
    participant Parser
    participant Operation
    participant Transmute
    participant Reporter

    %% Phase 1: Setup
    rect rgb(40, 40, 60)
        Note over main,Discovery: Phase 1: Initialization
        main->>App: createFromCli(argc, argv)
        App->>CLI: parseCli(argc, argv)
        CLI-->>App: Result<ParsedOptions>

        App->>Discovery: discoverFiles(patterns)
        Discovery-->>App: Result<DiscoveryResult>

        App-->>main: Result<App>
    end

    %% Phase 2: Execution
    rect rgb(60, 40, 40)
        Note over main,Reporter: Phase 2: Pipeline Execution
        main->>App: exec()

        App->>Parser: ClangParser::create(sourceFiles, buildDir)
        Parser-->>App: Result<unique_ptr<ClangParser>>
        Note over Parser: buildDir used to load compile_commands.json<br/>CompilationDatabaseFactory detects compiler (IAR/MSVC/GCC)<br/>Returns CompilationDatabaseInfo{database, compilerType}

        Note over App: Create operations based on config

        App->>Pipeline: execute<RecipeOperation>(parser, operations, buildDir, dryRun)
        Note over Pipeline: Template parameter = RecipeOperation variant

        %% Stage 1: Parse
        Note over Pipeline,Parser: Stage 1: Parse
        loop for each operation
            Pipeline->>Pipeline: std::visit(operation)
            Pipeline->>Operation: getRequirements()
            Operation-->>Pipeline: ParsingRequirements
        end
        Note over Pipeline: Aggregate requirements
        Pipeline->>Parser: parse(requirements)
        Parser-->>Pipeline: Result<parser::artifacts::ParseResults>

        %% Stage 2: Execute operations
        Note over Pipeline,Operation: Stage 2: Execute Operations
        loop for each operation (variant)
            Pipeline->>Pipeline: std::visit(operation)
            Note over Pipeline: Compile-time dispatch
            Pipeline->>Operation: execute(artifacts)
            Operation-->>Pipeline: Result<RecipeOperationResult>
        end

        %% Stage 3: Transmute
        Note over Pipeline,Transmute: Stage 3: Transmute Recipes
        loop for each operation result
            alt has recipes
                Note over Pipeline: Pre-flight validation
                loop for each file with recipes
                    Pipeline->>Transmute: applyRecipes(file, recipes, buildDir, dryRun)
                    Note over Transmute: Dispatch via std::visit
                    Transmute->>Transmute: applyRefactor()
                    Transmute-->>Pipeline: Result<TransmutationResult>
                end
            end
            Note over Pipeline: Accumulate metrics and summary
        end

        Pipeline-->>App: Result<PipelineResult{allMetrics, summary}>
    end

    %% Phase 3: Reporting
    rect rgb(40, 60, 40)
        Note over App,Reporter: Phase 3: Report Metrics

        alt has metrics
            App->>Reporter: reportMetrics(allMetrics)
            Note over Reporter: Dispatch by variant type (std::visit)
            Reporter->>Reporter: SAlignReporter::report(salignMetrics)
        end

        App->>App: Print summary (recipesApplied, filesProcessed)
        App-->>main: exitCode
    end
```

**Key Phases**:
1. **Initialization** (blue) - CLI parsing, file discovery, AppConfig creation
2. **Pipeline Execution** (red) - Parser creation, pipeline stages (parse → execute → transmute)
3. **Report Metrics** (green) - App reports metrics (I/O responsibility)

**Architecture Properties**:
- Parser abstraction isolates LLVM from pipeline (language-agnostic interface)
- Pipeline is stateless data transformation (no I/O)
- App handles I/O and reporting
- Semantic layers: `parsing/` extracts data, `operation/` creates recipes, `transmute/` applies recipes

---

## Parser Creation & Lifecycle

**Focus**: Factory pattern creates parser with compiler translator support

```mermaid
sequenceDiagram
    participant App
    participant ClangParser
    participant CompDBFactory
    participant Translator
    participant Adapter
    participant LLVM

    App->>ClangParser: ClangParser::create(sourceFiles, buildDir)

    Note over ClangParser: If buildDir provided, use for compile_commands.json

    rect rgb(60, 40, 60)
        Note over ClangParser,Adapter: Compiler Detection & Translation
        ClangParser->>CompDBFactory: fromBuildDir(buildDir)
        CompDBFactory->>CompDBFactory: loadFromDirectory(compile_commands.json)

        alt IAR detected
            CompDBFactory->>Translator: IARDbTranslator::translateAll(db)
            Note over Translator: Query-driver: queries compiler for system includes
            Translator-->>CompDBFactory: vector<CompileCommand>
            CompDBFactory->>Adapter: ClangCompilationDatabaseAdapter(translatedCommands)
            CompDBFactory->>LLVM: inferMissingCompileCommands(adapter)
            CompDBFactory-->>ClangParser: Result<CompilationDatabaseInfo{db, "IAR"}>
        else MSVC detected
            CompDBFactory->>Translator: MSVCDbTranslator::translateAll(db)
            Translator-->>CompDBFactory: vector<CompileCommand>
            CompDBFactory->>Adapter: ClangCompilationDatabaseAdapter(translatedCommands)
            CompDBFactory->>LLVM: inferMissingCompileCommands(adapter)
            CompDBFactory-->>ClangParser: Result<CompilationDatabaseInfo{db, "MSVC"}>
        else GCC/Clang
            CompDBFactory->>Adapter: ClangCompilationDatabaseAdapter(db->getAllCommands())
            CompDBFactory->>LLVM: inferMissingCompileCommands(adapter)
            CompDBFactory-->>ClangParser: Result<CompilationDatabaseInfo{db, "GCC/Clang"}>
        end
    end

    ClangParser->>LLVM: create ClangTool(dbInfo.database, sourceFiles)
    alt Tool creation fails
        LLVM-->>ClangParser: Error
        ClangParser-->>App: Result::failure()
    else Success
        LLVM-->>ClangParser: unique_ptr<ClangTool>
    end

    Note over ClangParser: CRITICAL: Store as members
    ClangParser->>ClangParser: m_tool, m_compilationDb, m_compilerType

    ClangParser-->>App: Result<unique_ptr<ClangParser>>
```

**Key Points**:
- `CompilationDatabaseFactory` detects compiler from flags in `compile_commands.json`
- Uses stateless translators (IAR/MSVC) to convert commands to Clang-compatible format
- Stores translated commands in `ClangCompilationDatabaseAdapter`
- Wraps adapter with `inferMissingCompileCommands()` for header file support (applies to all compilers)
- IAR translator uses query-driver approach (queries actual compiler for system include paths)
- MSVC translator converts flag syntax (`/I` → `-I`, `/D` → `-D`)
- Returns `CompilationDatabaseInfo{database, compilerType}`
- ClangParser stores `ClangTool`, `CompilationDatabase`, and `compilerType` as members
- Parser exposes compiler type via `getName()` for debugging/testing

---

## IAR Translator - Query-Driver Flow

**Focus**: IAR translator queries actual compiler for system includes, derives architecture defines

```mermaid
sequenceDiagram
    participant Factory as CompilationDatabaseFactory
    participant Translator as IARDbTranslator
    participant JSONDb as JSONCompilationDatabase
    participant Compiler as iccarm (IAR Compiler)

    Factory->>Translator: translateAll(db)

    Note over Translator: Step 1: Extract query config
    Translator->>Translator: extractQueryConfig(db)
    Translator->>JSONDb: getAllCompileCommands()
    JSONDb-->>Translator: vector<CompileCommand>
    Translator->>Translator: Extract compiler path (first cmd.CommandLine[0])
    Translator->>Translator: Extract arch flags (--cpu=Cortex-M4, --fpu=...)
    Translator-->>Translator: IARQueryConfig{compilerPath, archFlags}

    Note over Translator: Step 2: Query compiler for system includes
    Translator->>Translator: querySystemIncludes(query)
    Translator->>Compiler: popen("iccarm -E -xc -v --cpu=Cortex-M4 /dev/null 2>&1")
    Compiler-->>Translator: stdout/stderr with include paths

    Translator->>Translator: parseIncludePaths(output)
    Note over Translator: Parse: #include <...> search starts here:<br/>/opt/iar/arm/inc/c<br/>/opt/iar/arm/inc/c/aarch32<br/>End of search list.
    Translator-->>Translator: vector<string> includePaths

    Note over Translator: Step 3: Derive architecture defines
    Translator->>Translator: deriveArchitectureDefines(query)
    Note over Translator: Exact match: --cpu=Cortex-M4 → ARMv7E-M<br/>Defines: -D__ARM7EM__=1, -D__CORE__=__ARM7EM__
    Translator-->>Translator: vector<string> archDefines

    Note over Translator: Step 4: Translate commands
    loop for each IAR command
        Translator->>Translator: translateCommand(iarCmd)

        Note over Translator: Build Clang command
        Translator->>Translator: Start with "clang"

        loop for each queried include path
            Translator->>Translator: Add -isystem <path>
        end

        Translator->>Translator: Add compatibility defines (-D__IAR_SYSTEMS_ICC__=1, etc.)
        Translator->>Translator: Add architecture defines (from deriveArchitectureDefines)

        loop for each flag in IAR command
            alt IAR-specific flag (--cpu, --fpu, --diag_*)
                Note over Translator: Filter out (skip)
            else Standard flag (-I, -D, -U, source file)
                Translator->>Translator: Keep flag unchanged
            end
        end

        Translator->>Translator: Add -target arm-none-eabi
        Translator->>Translator: Add -Wno-everything -fsyntax-only

        Translator->>Translator: translatedCommands.push_back(clangCmd)
    end

    Translator-->>Factory: vector<CompileCommand> (Clang-compatible with real includes)
```

**Key Points**:
- **Query-driver**: Executes actual IAR compiler to get system include paths (no hardcoded assumptions)
- **Subprocess execution**: Uses `popen()` to run `iccarm -E -xc -v /dev/null`, captures verbose output
- **Include path parsing**: Parses compiler output for `#include <...>` search paths
- **Architecture detection**: Exact string matching on `--cpu` flag (M33 must not match M3)
  - Supports: M0/M0+/M1 (ARMv6-M), M3 (ARMv7-M), M4/M4F/M7/M7F (ARMv7E-M), M23/M33/M33F (ARMv8-M)
- **Compatibility defines**: `__intrinsic=`, `__packed=__attribute__((packed))`, etc.
- IAR uses GCC-compatible syntax for `-I`, `-D`, `-U` (no translation needed)
- Returns `vector<CompileCommand>` with **real** include paths from actual compiler

---

## Pipeline - Parse → Execute → Transmute (Template-Based)

**Focus**: Template-based stateless pipeline stages with requirement-driven parsing

```mermaid
sequenceDiagram
    participant App
    participant Pipeline
    participant Operations
    participant Parser

    App->>Pipeline: execute<RecipeOperation>(parser, operations, buildDir, dryRun)
    Note over Pipeline: Template param = RecipeOperation variant

    Note over Pipeline: Stage 1: Parse
    loop for each operation
        Pipeline->>Pipeline: std::visit(operation)
        Pipeline->>Operations: getRequirements()
        Operations-->>Pipeline: ParsingRequirements
    end
    Note over Pipeline: Aggregate requirements
    Pipeline->>Parser: parse(requirements)
    Parser-->>Pipeline: Result<parser::artifacts::ParseResults>

    Note over Pipeline: Stage 2: Execute Operations
    loop for each operation (variant)
        Pipeline->>Pipeline: std::visit(operation)
        Note over Pipeline: Compile-time dispatch
        Pipeline->>Operations: execute(artifacts)
        Operations-->>Pipeline: Result<RecipeOperationResult>
    end

    Note over Pipeline: Stage 3: Transmute
    loop for each operation result
        alt has recipes
            Pipeline->>Pipeline: transmute(recipes)
            Note over Pipeline: validateTransmute() - pre-flight checks
            Note over Pipeline: executeTransmute() - apply recipes
        end
    end
    Note over Pipeline: Accumulate summary

    Pipeline-->>App: Result<PipelineResult{allMetrics, summary}>
```

**Key Points**:
- **Template-based**: `execute<RecipeOperationVariantT>()` - generic over operation variant type
- Production: `execute<RecipeOperation>(...)` - uses real operations
- Testing: `execute<TestRecipeOperation>(...)` - uses mock operations
- Single parse pass fulfills all operation requirements
- `parser::artifacts::ParseResults` passed by const reference to operations
- Duck-typed interface: `std::visit` calls `getRequirements()`, `execute()` directly

---

## ClangParser - Error Accumulation

**Focus**: Accumulate errors during AST callbacks (void return type)

```mermaid
sequenceDiagram
    participant Pipeline
    participant ClangParser
    participant ClangTool
    participant ParsingRule

    Pipeline->>ClangParser: parse(requirements)
    ClangParser->>ClangTool: run(FrontendActionFactory)

    Note over ClangTool,ParsingRule: Clang callbacks (void return)
    loop for each struct in AST
        ClangTool->>ParsingRule: run(MatchResult)
        alt Extraction succeeds
            ParsingRule->>ParsingRule: m_parsedStructs.push_back()
        else Extraction fails
            ParsingRule->>ParsingRule: m_parseErrors.push_back()
        end
    end

    ClangTool-->>ClangParser: void

    alt Errors accumulated
        ClangParser-->>Pipeline: Result::failure(joined errors)
    else No errors
        ClangParser-->>Pipeline: Result::success(ParseResults)
    end
```

**Key Points**:
- Clang callbacks have void return (cannot return errors)
- Errors stored in `m_parseErrors` vector during callbacks
- Checked after AST processing, converted to Result::failure()

---

## Recipe Generation

**Focus**: Operations analyze artifacts, produce recipes wrapped in variants

```mermaid
sequenceDiagram
    participant Pipeline
    participant Operation
    participant Analyzer

    Pipeline->>Pipeline: std::visit(operation)
    Note over Pipeline: Dispatch to execute()
    Pipeline->>Operation: execute(artifacts)

    loop for each struct in artifacts
        Operation->>Analyzer: analyze(structDef)

        alt Optimization possible
            Analyzer-->>Operation: RefactorRecipe{file, offset, length, text}
            Note over Operation: Auto-wrapped in Recipe variant
        else Already optimal
            Analyzer-->>Operation: empty
        end
    end

    Note over Operation: Compute metrics, wrap in Metrics variant
    Operation->>Operation: RecipeOperationResult{recipes, metrics}

    Operation-->>Pipeline: Result<RecipeOperationResult>
```

**Key Points**:
- Pipeline uses `std::visit` to invoke `execute()` on the variant operation type
- Recipes auto-wrapped in `std::variant<RefactorRecipe>`
- Metrics auto-wrapped in `std::variant<SAlignMetrics>`
- Grouped by file for batch processing

---

## Transmute - Variant Dispatcher

**Focus**: Separate recipes by type, apply with type-specific logic

```mermaid
sequenceDiagram
    participant Pipeline
    participant Transmute
    participant FileSystem

    Note over Pipeline: Pre-flight validation
    Pipeline->>FileSystem: Check file exists, writable, parent writable

    Pipeline->>Transmute: applyRecipes(file, recipes, buildDir, dryRun)

    Note over Transmute: Dispatch via std::visit
    Transmute->>FileSystem: read file
    Note over Transmute: Validate byte ranges<br/>Calculate final size<br/>Single-pass construction
    Transmute->>FileSystem: write modified content (atomic: temp + rename)

    Transmute-->>Pipeline: Result<TransmutationResult{recipesApplied}>
```

**Key Points**:
- Pre-flight validation prevents partial modifications on error
- `std::visit` dispatches Recipe variant to type-specific handler
- RefactorRecipes: validate byte ranges, calculate final size, single-pass construction, atomic write (temp + rename)
- Single-pass construction (17% reduction vs string::replace)

---

## Metrics Reporter - Variant Dispatcher

**Focus**: Separate metrics by type, dispatch to type-specific reporters

```mermaid
sequenceDiagram
    participant App
    participant Reporter
    participant SAlignReporter

    App->>Reporter: reportMetrics(metrics)

    Note over Reporter: Partition by variant type (std::visit)
    Reporter->>Reporter: salignMetrics.push_back()

    alt !salignMetrics.empty()
        Reporter->>SAlignReporter: report(salignMetrics)
        Note over SAlignReporter: Three-level reporting:<br/>File → Struct → Global
        SAlignReporter-->>Reporter: void (displayed)
    end

    Reporter-->>App: void
```

**Key Points**:
- `std::visit` separates Metrics variant into concrete types
- Each metric type dispatched to appropriate reporter
- Reporting in App layer (I/O responsibility), not Pipeline
- App also reports transmutation summary from PipelineResult

---

## Error Propagation

**Example**: Error during parsing propagates to main with context

```mermaid
sequenceDiagram
    participant main
    participant App
    participant Pipeline
    participant ClangParser
    participant ParsingRule

    main->>App: exec()
    App->>Pipeline: execute(parser, operations, buildDir, dryRun)
    Pipeline->>ClangParser: parse(requirements)

    Note over ParsingRule: Error during AST callback
    ParsingRule->>ParsingRule: m_parseErrors.push_back()

    ClangParser->>ParsingRule: hasParseErrors()
    ParsingRule-->>ClangParser: true

    ClangParser-->>Pipeline: Result::failure("parsing failed: ...")
    Pipeline-->>App: Result<PipelineResult>::failure("pipeline::execute: ...")
    App-->>main: EXIT_FAILURE
```

**Key Points**:
- Result<T> propagates errors up call stack
- Each layer adds contextual prefix
- User sees full error chain
- Error strings moved with `std::move(result).error()` (no copies)

---

## Performance Notes

- Pre-compiled glob patterns (once per pattern, not per file)
- Single parse pass with requirement aggregation
- Byte-offset precision (no line parsing overhead)
- Single-pass transmutation with pre-allocation
- Move semantics for `Result<T>` (eliminates error string copies)
- Primary bottleneck: ClangTool initialization and AST traversal

---
