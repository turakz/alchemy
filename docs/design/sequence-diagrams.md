# Alchemy Sequence Diagrams (v0.1.0-alpha)

**Current state (v0.1.0-alpha)**:

high-level execution flows showing component interactions

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
    Note over main,Discovery: Phase 1: Initialization
        main->>CLI: parseCli(argc, argv)
        CLI-->>main: Result<ParsedOptions>

        main->>App: create(ParsedOptions)
        App->>Discovery: discoverFiles(patterns)
        Discovery-->>App: Result<DiscoveryResult>

        App-->>main: Result<App>

    %% Phase 2: Execution
    Note over main,Reporter: Phase 2: Pipeline Execution
        main->>App: exec()

        App->>Parser: ClangParser::create(sourceFiles, buildDir, excludePatterns)
        Parser-->>App: Result<unique_ptr<ClangParser>>
        Note over Parser: loadCompilationDatabase → CompilationDatabaseFactory detects compiler<br/>filters compile commands by excludePatterns<br/>buildReverseDependencyMap (clang -MM) → resolveParseCommands<br/>direct-header fallback for unmatched headers

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

    %% Phase 3: Reporting
    Note over App,Reporter: Phase 3: Report Metrics

        alt has metrics
            App->>Reporter: reportMetrics(allMetrics)
            Note over Reporter: Dispatch by variant type (std::visit)
            Reporter->>Reporter: SAlignReporter::report(salignMetrics)
        end

        App->>App: Print summary (recipesApplied, filesProcessed)

        alt recipes applied && !dryRun
            App->>App: Print warning (inline comments may have shifted)
        end

        App-->>main: exitCode
```

**Key Phases**:
1. **initialization** -> cli parsing, file discovery, AppConfig creation
2. **pipeline execution** -> parser creation, pipeline stages (parse → execute → transmute)
3. **report metrics** -> App reports metrics (I/O responsibility)

**Architecture Properties**:
- parser abstraction isolates LLVM from pipeline (language-agnostic interface)
- pipeline is stateless data transformation (no I/O)
- app handles I/O and reporting
- semantic layers: `parsing/` extracts data, `operation/` creates recipes, `transmute/` applies recipes

---

## Parser Creation & Lifecycle

**focus**: factory translates db, `create()` builds dep graph via `clang -MM`, resolves per-file parse commands

```mermaid
sequenceDiagram
    participant App
    participant ClangParser
    participant CompDBFactory
    participant Translator
    participant Adapter
    participant LLVM
    participant Discovery

    App->>ClangParser: ClangParser::create(sourceFiles, buildDir, excludePatterns)

    Note over ClangParser,Adapter: Step 1: Compiler Detection & Translation
        ClangParser->>CompDBFactory: fromBuildDir(buildDir)
        CompDBFactory->>CompDBFactory: loadFromDirectory(compile_commands.json)

        alt IAR detected
            CompDBFactory->>Translator: IarDbTranslator::translateAll(db)
            Note over Translator: Query-driver: queries iccarm for system includes
            Translator-->>CompDBFactory: vector<CompileCommand>
            CompDBFactory->>Adapter: ClangCompilationDatabaseAdapter(translated)
            CompDBFactory-->>ClangParser: Result<CompilationDatabaseInfo{db, "IAR"}>
        else MSVC detected
            CompDBFactory->>Translator: MsvcDbTranslator::translateAll(db)
            Translator-->>CompDBFactory: vector<CompileCommand>
            CompDBFactory->>Adapter: ClangCompilationDatabaseAdapter(translated)
            CompDBFactory-->>ClangParser: Result<CompilationDatabaseInfo{db, "MSVC"}>
        else GCC detected
            CompDBFactory->>Translator: GccDbTranslator::translateAll(db)
            Note over Translator: Query-driver: queries gcc for system includes + target triple
            Translator-->>CompDBFactory: vector<CompileCommand>
            CompDBFactory->>Adapter: ClangCompilationDatabaseAdapter(translated)
            CompDBFactory-->>ClangParser: Result<CompilationDatabaseInfo{db, "GCC"}>
        else Clang
            CompDBFactory->>Adapter: ClangCompilationDatabaseAdapter(db->getAllCommands())
            CompDBFactory-->>ClangParser: Result<CompilationDatabaseInfo{db, "Clang"}>
        end

        ClangParser->>ClangParser: allCommands = db->getAllCompileCommands()

    Note over ClangParser,Discovery: Step 2: Exclude Pattern Filtering
        alt excludePatterns not empty
            ClangParser->>Discovery: compilePatterns(excludePatterns)
            Discovery-->>ClangParser: vector<CompiledPattern>
            loop for each compile command
                ClangParser->>Discovery: matchesPattern(cmd.Filename, pattern)
            end
            Note over ClangParser: filteredCommands = allCommands − matched
        else
            Note over ClangParser: filteredCommands = allCommands
        end

    Note over ClangParser,LLVM: Step 3: Dep Graph Construction (clang -MM)
        ClangParser->>ClangParser: findClangBinary()
        ClangParser->>ClangParser: buildReverseDependencyMap(filteredCommands, clangBinary)
        loop for each TU in filteredCommands
            ClangParser->>LLVM: clang -MM <translated-flags> <source.c>
            LLVM-->>ClangParser: Makefile dep output (target.o: source.c header1.h ...)
            Note over ClangParser: reverseDepMap[header.h] → [{tuPath, command}]
        end

    Note over ClangParser: Step 4: Parse Command Resolution
        ClangParser->>ClangParser: resolveParseCommands(sourceFiles, filteredCommands, reverseDepMap)
        loop for each sourceFile
            alt source file (.c/.cpp) found in DB
                ClangParser->>ClangParser: parseCommands += {absPath, targetName, cmd}
            else source file not in DB
                ClangParser->>ClangParser: directHeaders += absPath
            else header found in reverseDepMap
                loop for each dep-graph entry (TU that includes this header)
                    ClangParser->>ClangParser: parseCommands += {tuPath, targetName, cmd}
                end
            else header not in reverseDepMap
                ClangParser->>ClangParser: directHeaders += header
            end
        end
        Note over ClangParser: dedup parseCommands by (parseFile, targetName)

    Note over ClangParser,LLVM: Step 5: Fallback DB (for direct-parse headers only)
        ClangParser->>LLVM: inferMissingCompileCommands(filteredCommandsAdapter)
        LLVM-->>ClangParser: fallbackDb
        ClangParser->>ClangParser: extractIncludePaths(filteredCommands) → globalIncludes

    Note over ClangParser: Construct ClangParser(parseCmds, directHeaders, fallbackDb, globalIncludes, compilerType, targetHeaders)
    ClangParser-->>App: Result<unique_ptr<ClangParser>>
```

**Key Points**:
- `CompilationDatabaseFactory` detects compiler from flags in `compile_commands.json`
- uses stateless translators (GCC/IAR/MSVC) to convert commands to Clang-compatible format
- `create()` accepts `excludePatterns` — compile commands matching any pattern are stripped before dep graph work, preventing cross-target SDK include contamination
- dep graph built via `clang -MM` per filtered TU (produces `reverseDepMap: header → [{tuPath, command}]`)
- `resolveParseCommands()` maps each user-provided file to the TU(s) that include it, unmatched files go to `directHeaders` for direct-parse fallback
- `inferMissingCompileCommands` wraps `filteredCommands` only —> excluded TUs cannot contaminate the fallback DB
- `m_targetHeaders` stores user-requested absolute paths, `ClangStructParsingRule` uses it to filter AST results to only the requested files

**ParseCommand vs Direct Header**:
- **ParseCommand** (`m_parseCmds`): file is parsed via the TU that includes it (full include chain context) -> one command per `(TU, build target)` pair
- **direct header** (`m_directHeaders`): no TU in dep graph includes this file, parsed directly via `fallbackDb` with injected include paths and std-type preamble

---

## IAR Translator: Compiler Query Driven

**focus**: IAR translator queries actual compiler for system includes, derives architecture defines

```mermaid
sequenceDiagram
    participant Factory as CompilationDatabaseFactory
    participant Translator as IarDbTranslator
    participant JSONDb as JSONCompilationDatabase
    participant Compiler as iccarm (IAR Compiler)

    Factory->>Translator: translateAll(db)

    Note over Translator: Step 1: Extract query config
    Translator->>Translator: extractQueryConfig(db)
    Translator->>JSONDb: getAllCompileCommands()
    JSONDb-->>Translator: vector<CompileCommand>
    Translator->>Translator: Extract compiler path (first cmd.CommandLine[0])
    Translator->>Translator: Extract arch flags (--cpu=Cortex-M4, --fpu=...)
    Translator-->>Translator: IarQueryConfig{compilerPath, archFlags}

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
- **compiler query**: executes actual IAR compiler to get system include paths (no hardcoded assumptions)
- **include path parsing**: parses compiler output for `#include <...>` search paths
- **architecture detection**: exact string matching on `--cpu` flag (M33 must not match M3)
  - supports: M0/M0+/M1 (ARMv6-M), M3 (ARMv7-M), M4/M4F/M7/M7F (ARMv7E-M), M23/M33/M33F (ARMv8-M)
- **compatibility defines**: `__intrinsic=`, `__packed=__attribute__((packed))`, etc.
- IAR uses GCC-compatible syntax for `-I`, `-D`, `-U` (no translation needed)
- returns `vector<CompileCommand>` with **real** include paths from actual compiler

---

## GCC Translator

**focus**: GCC translator queries compiler for system includes and target triple, filters GCC-specific flags, injects ABI-compatibility flags

```mermaid
sequenceDiagram
    participant Factory as CompilationDatabaseFactory
    participant Translator as GccDbTranslator
    participant JSONDb as JSONCompilationDatabase
    participant Compiler as gcc (GCC Compiler)

    Factory->>Translator: extractQueryConfig(db)
    Note over Translator: Step 1: Extract query config
    Translator->>JSONDb: getAllCompileCommands()
    JSONDb-->>Translator: vector<CompileCommand>
    Translator->>Translator: Extract compiler path (CommandLine[0])
    Translator->>Translator: Infer language from binary name (c / c++)
    Translator-->>Factory: Result<GccQueryConfig{compilerPath, language}>

    Note over Translator: Step 2: Query system includes
    Factory->>Translator: querySystemIncludes(query)
    Translator->>Compiler: popen("gcc -E -Wp,-v -x c /dev/null 2>&1")
    Compiler-->>Translator: stdout/stderr with include paths
    Translator->>Translator: parseSystemIncludes(output)
    Note over Translator: Parse: #include <...> search starts here:<br/>/usr/arm-none-eabi/include<br/>/usr/lib/gcc/arm-none-eabi/12/include<br/>End of search list.
    Translator-->>Factory: Result<vector<string>> sysIncludes

    Note over Translator: Step 3: Query target triple
    Factory->>Translator: queryTargetTriple(query)
    Translator->>Compiler: popen("gcc -dumpmachine")
    Compiler-->>Translator: "arm-none-eabi"
    Translator->>Translator: validateTargetTriple(triple)
    Note over Translator: Extract arch (before first '-')<br/>Check against KnownArchitectures set<br/>Warn if unrecognized (non-fatal)
    Translator-->>Factory: Result<string> targetTriple

    Factory->>Factory: GccDbTranslator(sysIncludes, targetTriple)

    Note over Translator: Step 4: Translate commands
    loop for each GCC command
        Factory->>Translator: translateCommand(gccCmd)

        Note over Translator: Build Clang command
        Translator->>Translator: Start with "clang"
        Translator->>Translator: Add -target <triple>

        alt ARM EABI target (arm/thumb-*-none-eabi*)
            Translator->>Translator: Add -fshort-enums (ABI compat)
        end

        loop for each queried include path
            Translator->>Translator: Add -isystem <path>
        end

        loop for each flag in GCC command
            alt --driver-mode= flag
                Note over Translator: Strip (alchemy handles translation)
            else GCC-only flag OR resource dir suppressor
                Note over Translator: Strip (-nostdinc, -Werror, --specs=, etc.)
            else GCC warning with clang equivalent
                Translator->>Translator: Translate (-Wmaybe-uninitialized -> -Wconditional-uninitialized)
            else Standard flag (-I, -D, -O, -march, source file)
                Translator->>Translator: Keep flag unchanged
            end
        end

        Translator->>Translator: Add -Wno-unknown-warning-option (safety net)
        Translator->>Translator: Add -fsyntax-only

        Translator-->>Factory: CompileCommand (Clang-compatible)
    end
```

**Key Points**:
- **compiler query**: Executes actual GCC compiler for system includes (`-E -Wp,-v`) and target triple (`-dumpmachine`)
- **non-fatal failures**: System include and target triple queries warn on failure but don't block translation
- **architecture validation**: Checks extracted arch against known set (arm, thumb, aarch64, x86_64, riscv32/64, avr, msp430), warns if unrecognized
- **ARM EABI `-fshort-enums`**: GCC implicitly enables packed enums for `arm-*-none-eabi*` / `thumb-*-none-eabi*` targets -> clang doesn't, so it's injected explicitly for `sizeof()` correctness
- **resource dir suppressors**: `-nostdinc`, `-nostdinc++`, `-nobuiltininc`, `-nostdlibinc` are stripped to prevent suppressing clang's resource directory (needed for target-correct `stdint.h`)
- **flag translation**: 6 GCC warning flags have clang equivalents (e.g., `-Wmaybe-uninitialized` → `-Wconditional-uninitialized`)
- **blocklist approach**: GCC-specific flags filtered by exact match set + prefix matching (`-fdump-*`, `-fipa-*`, `-Werror=*`)

---

## Pipeline: Parse → Execute → Transmute

**focus**: Template-based stateless pipeline stages with requirement-driven parsing

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
            Note over Pipeline: validateTransmute() -> pre-flight checks
            Note over Pipeline: executeTransmute() -> apply recipes
        end
    end
    Note over Pipeline: Accumulate summary

    Pipeline-->>App: Result<PipelineResult{allMetrics, summary}>
```

**Key Points**:
- **template-based**: `execute<RecipeOperationVariantT>()` -> generic over operation variant type
- production: `execute<RecipeOperation>(...)` -> uses real operations
- testing: `execute<TestRecipeOperation>(...)` -> uses mock operations
- single parse pass fulfills all operation requirements
- `parser::artifacts::ParseResults` passed by const reference to operations
- duck-typed interface: `std::visit` calls `getRequirements()`, `execute()` directly

---

## ClangParser - parse() Execution & Error Handling

**focus**: per-command ClangTool runs with soft-skip on failure (hard-fail only when nothing was analyzed)

```mermaid
sequenceDiagram
    participant Pipeline
    participant ClangParser
    participant ClangTool
    participant ParsingRule

    Pipeline->>ClangParser: parse(requirements)

    Note over ClangParser,ClangTool: Command loop — one ClangTool per ParseCommand (soft-skip on error)
    loop for each ParseCommand in m_parseCmds
        ClangParser->>ClangParser: run(cmd)
        Note over ClangParser: thin single-command DB for this TU
        ClangParser->>ClangTool: run(FrontendActionFactory)
        loop for each struct in AST
            ClangTool->>ParsingRule: run(MatchResult)
            alt struct file in m_targetHeaders
                ParsingRule->>ParsingRule: m_parsedStructs.push_back()
            else not in target headers
                Note over ParsingRule: filtered (TU pulls in many headers)
            end
        end
        ClangTool-->>ClangParser: clangResult (int)
        alt clangResult != 0 OR hasParseErrors()
            Note over ClangParser: SOFT-SKIP: log warning, continue to next spec
        else
            ClangParser->>ClangParser: results.structs += structs
        end
    end

    Note over ClangParser,ClangTool: Direct-header fallback — soft-skip on error
    loop for each header in m_directHeaders
        ClangParser->>ClangParser: run(header)
        Note over ClangParser: fallbackDb + injectIncludePaths + injectStdPreamble
        ClangParser->>ClangTool: run(FrontendActionFactory)
        ClangTool-->>ClangParser: clangResult
        alt clangResult != 0
            Note over ClangParser: SOFT-SKIP: log warning, continue
        else
            ClangParser->>ClangParser: results.structs += structs
        end
    end

    alt ALL parse commands failed AND m_directHeaders empty
        ClangParser-->>Pipeline: Result::failure("all parse commands failed")
    else
        ClangParser->>ClangParser: deduplicateStructs(results.structs)
        ClangParser-->>Pipeline: Result::success(ParseResults)
    end
```

**Key Points**:
- each `ParseCommand` gets its own `ClangTool` with a thin single-command DB (exactly the translated command for that TU)
- Clang AST callbacks have void return — errors are accumulated in `m_parseErrors` during traversal, checked after `ClangTool::run()` returns
- `m_targetHeaders` filters AST output to only the user-requested files -> TU-included-only headers are discarded
- **soft-skip**: a failing parse command (non-zero exit OR parse errors) emits a warning and continues — caused by cross-target SDK include contamination in the compile command
- **direct-header fallback** (`run(header)`): no TU in dep graph -> uses `fallbackDb` (inferred) + global include injection + std-type preamble injection
- **hard-fail** only in the degenerate case where every parse command failed AND there are no direct headers — nothing was analyzed at all
- `deduplicateStructs` removes duplicate `(sourceFile, structName)` pairs emitted when multiple parse commands parse the same shared header

---

## Recipe Generation

**focus**: Operations analyze artifacts, produce recipes wrapped in variants

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
            Note over Analyzer: Sort fields by alignment (desc)
            Note over Analyzer: Compare original vs optimized order
            alt Swap involves non-reorderable field
                Analyzer-->>Operation: empty (bail out — bitfield/anonymous union)
            else All swapped fields reorderable
                Analyzer-->>Operation: RefactorRecipe{file, offset, length, text}
                Note over Operation: text = sourceTypeName + fieldName + arraySuffix + trailingComment
            end
        else Already optimal
            Analyzer-->>Operation: empty
        end
    end

    Note over Operation: Compute metrics, wrap in Metrics variant
    Operation->>Operation: RecipeOperationResult{recipes, metrics}

    Operation-->>Pipeline: Result<RecipeOperationResult>
```

**Key Points**:
- pipeline uses `std::visit` to invoke `execute()` on the variant operation type
- recipes auto-wrapped in `std::variant<RefactorRecipe>`
- metrics auto-wrapped in `std::variant<SAlignMetrics>`
- grouped by file for batch processing

---

## Transmute - Variant Dispatcher

**focus**: separate recipes by type, apply with type-specific logic

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
- pre-flight validation prevents partial modifications on error
- `std::visit` dispatches Recipe variant to type-specific handler
- refactorRecipes: validate byte ranges, calculate final size, single-pass construction, atomic write (temp + rename)
- single-pass construction (17% reduction vs string::replace)

---

## Metrics Reporter - Variant Dispatcher

**focus**: separate metrics by type, dispatch to type-specific reporters

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
- each metric type dispatched to appropriate reporter
- reporting in App layer (I/O responsibility), not Pipeline
- app also reports transmutation summary from PipelineResult

---

## Error Propagation

**example**: error during parsing propagates to main with context

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
- result<T> propagates errors up call stack
- each layer adds contextual prefix
- user sees full error chain
- error strings moved with `std::move(result).error()` (no copies)

---
