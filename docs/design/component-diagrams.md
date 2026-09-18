# Alchemy Component Architecture (v0.1.0-alpha)

**current state (v0.1.0-alpha)**:
- single feature: struct alignment refactoring (`--salign`)
- cross-compiler support: GCC, Clang, MSVC, IAR

## High-Level Module Boundaries

architecture boundaries, dependency direction, and layer separation

```mermaid
flowchart TD
    App["App<br/>────<br/>entry point + orchestrator"]

    ConfigParser["ConfigParser<br/>────<br/>config loading + merging"]
    CLI["CLI<br/>────<br/>arg parsing"]
    Discovery["Discovery<br/>────<br/>file discovery + pattern matching"]
    Reporter["Reporter<br/>────<br/>metrics output"]

    Pipeline["Pipeline<br/>────<br/>parse → execute → transmute"]

    ClangParser["ClangParser<br/>────<br/>source files → parse results"]
    Operations["RecipeOperation<br/>────<br/>parse results → recipes"]
    Transmute["Transmute<br/>────<br/>recipes → file mutations"]

    subgraph External["External"]
        direction LR
        Tomlpp["toml++<br/>────<br/>TOML parsing library"]
        AlchemyToml[".alchemy/alchemy.toml<br/>────<br/>project config"]
        LLVM["LLVM/Clang<br/>────<br/>AST + arg parsing"]
        CompDB["compile_commands.json<br/>────<br/>vendor-specific"]
    end

    App ==> ConfigParser
    App ==> CLI
    App ==> Discovery
    App ==> Reporter
    App ==> Pipeline

    Pipeline ==> ClangParser
    Pipeline ==> Operations
    Pipeline ==> Transmute

    Operations ==>|recipes| Transmute

    ConfigParser ==>|loads| AlchemyToml
    ConfigParser -.->|wraps| Tomlpp
    ClangParser -.->|wraps| LLVM
    CLI -.->|wraps| LLVM
    ClangParser ==>|loads| CompDB

    classDef app fill:#268bd2,stroke:#1e6ba8,stroke-width:2px,color:#fdf6e3
    classDef infra fill:#b58900,stroke:#8f6d00,stroke-width:2px,color:#fdf6e3
    classDef pipeline fill:#2aa198,stroke:#208c84,stroke-width:2px,color:#fdf6e3
    classDef domain fill:#859900,stroke:#6c7a00,stroke-width:2px,color:#fdf6e3
    classDef external fill:#d33682,stroke:#a92866,stroke-width:2px,color:#fdf6e3

    class App app
    class ConfigParser,CLI,Discovery,Reporter infra
    class Pipeline pipeline
    class ClangParser,Operations,Transmute domain
    class Tomlpp,AlchemyToml,LLVM,CompDB external
```

## Compiler Adapter Detail

zoomed-in view of the compiler detection and translation layer

```mermaid
graph TB
    CompDB["compile_commands.json<br/>────<br/>vendor-specific flags"]

    subgraph Factory["CompilationDatabaseFactory"]
        Detect["Detect compiler type<br/>────<br/>inspect flags in DB"]
    end

    subgraph Translators["Translators"]
        direction LR
        IAR["IarDbTranslator<br/>────<br/>query iccarm for includes<br/>derive arch defines<br/>filter IAR flags"]
        MSVC["MsvcDbTranslator<br/>────<br/>translate /I→-I, /D→-D<br/>add -fms-extensions<br/>filter MSVC flags"]
        GCC["GccDbTranslator<br/>────<br/>query gcc for includes + triple<br/>inject -fshort-enums (ARM EABI)<br/>filter/translate GCC flags"]
    end

    Adapter["ClangCompilationDatabaseAdapter<br/>────<br/>stores translated commands<br/>implements CompilationDatabase"]

    ClangParserCreate["ClangParser::create()<br/>────<br/>exclude filter → dep graph<br/>→ parse command resolution<br/>(see Dep Graph diagram)"]

    CompDB ==>|loaded by| Factory
    Detect ==> IAR
    Detect ==> MSVC
    Detect ==> GCC

    IAR ==> Adapter
    MSVC ==> Adapter
    GCC ==> Adapter

    Adapter ==>|translated commands| ClangParserCreate

    classDef external fill:#d33682,stroke:#a92866,stroke-width:2px,color:#fdf6e3
    classDef domain fill:#859900,stroke:#6c7a00,stroke-width:2px,color:#fdf6e3

    class CompDB external
    class Detect,IAR,MSVC,GCC,Adapter,ClangParserCreate domain
```

---

## ClangParser - Dep Graph & Parse Command Resolution

data flow inside `ClangParser::create()`: from translated commands to the parse commands stored on the constructed parser

```mermaid
graph TB
    subgraph Inputs["Inputs to create()"]
        SourceFiles["sourceFiles<br/>user-provided paths"]
        ExcludePatterns["excludePatterns<br/>glob patterns"]
        Adapter["Adapter<br/>translated compile commands"]
    end

    FilteredCommands["filteredCommands<br/>────<br/>allCommands − TUs matching excludePatterns<br/>(discovery::matchesPattern)"]

    ReverseDepMap["ReverseDependencyMap<br/>────<br/>header → [{tuPath, cmd}]<br/>built via clang -MM per TU"]

    subgraph SpecResolution["resolveParseCommands()"]
        ParseSpecs["ParseCommand[]<br/>{parseFile, targetName, cmd}<br/>one per (TU, build target)"]
        DirectHeaders["directHeaders[]<br/>no TU includes this header<br/>→ direct-parse fallback"]
    end

    subgraph FallbackSetup["Direct-Parse Fallback Setup"]
        FallbackDb["fallbackDb<br/>inferMissingCompileCommands<br/>(filteredCommands only)"]
        GlobalIncludes["globalIncludes<br/>union of all -I paths"]
    end

    ClangParser["ClangParser (constructed)<br/>────<br/>m_parseCmds · m_directHeaders<br/>m_fallbackDb · m_globalIncludes · m_targetHeaders"]

    ExcludePatterns ==> FilteredCommands
    Adapter ==>|getAllCompileCommands| FilteredCommands

    FilteredCommands ==> ReverseDepMap
    FilteredCommands ==> SpecResolution
    FilteredCommands ==> FallbackSetup

    SourceFiles ==> SpecResolution
    ReverseDepMap ==> SpecResolution

    ParseSpecs ==> ClangParser
    DirectHeaders ==> ClangParser
    SpecResolution ==>|m_targetHeaders| ClangParser
    FallbackDb ==> ClangParser
    GlobalIncludes ==> ClangParser

    classDef input fill:#d33682,stroke:#a92866,stroke-width:2px,color:#fdf6e3
    classDef filtered fill:#2aa198,stroke:#208c84,stroke-width:2px,color:#fdf6e3
    classDef specs fill:#859900,stroke:#6c7a00,stroke-width:2px,color:#fdf6e3
    classDef fallback fill:#6c71c4,stroke:#5450a0,stroke-width:2px,color:#fdf6e3
    classDef output fill:#268bd2,stroke:#1e6ba8,stroke-width:2px,color:#fdf6e3

    class SourceFiles,ExcludePatterns,Adapter input
    class FilteredCommands,ReverseDepMap filtered
    class ParseSpecs,DirectHeaders specs
    class FallbackDb,GlobalIncludes fallback
    class ClangParser output
```

**Data Flow**:
1. **exclude filter** —> strips TUs whose source path matches any exclude pattern, prevents cross-target SDK include contamination from reaching dep graph or spec resolution
2. **dep graph** —> `clang -MM` per filtered TU builds `ReverseDepMap`: maps each header to the TUs that include it
3. **parse command resolution** —> for each user-provided file: source files map directly to their DB entry, headers map to every dep-graph TU that includes them (files absent from the dep graph go to `directHeaders`)
4. **fallback db** —> `inferMissingCompileCommands` wraps `filteredCommands` only (excluded TUs cannot contaminate inferred commands for direct-parse headers)

**why `m_targetHeaders`?** `ClangTool` runs on a full TU (e.g. `utils.c`) which pulls in many headers. `m_targetHeaders` contains only the user-requested absolute paths (`ClangStructParsingRule` uses it to discard struct definitions from non-target headers during AST traversal).

---

**Relationship Types:**
- **thick arrows (`==>`)** -> Runtime dependencies, data flow
- **dotted arrows (`-.->`)** —> Compile-time dependencies (interface implementation, wrapping)
- **regular arrows (`-->`)** —> Structural relationships

---

see [sequence-diagrams.md](sequence-diagrams.md) for runtime execution flows
