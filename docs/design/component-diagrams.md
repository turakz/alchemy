# Alchemy Component Architecture (v0.1.0-alpha)

Architecture showing module boundaries, dependency direction, and layer separation.

**Current state (v0.1.0-alpha)**:
- Single feature: struct alignment refactoring (`--salign`)
- Cross-compiler support: GCC, Clang, MSVC, IAR (query-driver approach)
- Test coverage: 276 unit + 39 integration = 315 tests (all passing)

## High-Level Module Boundaries

Shows architectural layers, module groupings, and dependency direction (top → bottom).

```mermaid
graph TB
    subgraph AppLayer["App Layer"]
        main["main.cpp"]
        App["App<br/>────<br/>orchestrator"]
        main --> App
    end

    subgraph Infrastructure["Infrastructure"]
        direction LR
        CLI["CLI<br/>────<br/>parse arguments"]
        Discovery["Discovery<br/>────<br/>find files"]
        Reporter["Reporter<br/>────<br/>print metrics"]
    end

    subgraph PipelineLayer["Pipeline"]
        Pipeline["Pipeline<br/>────<br/>stateless orchestration<br/>parse → execute → transmute"]
    end

    subgraph Parsing["Parsing Domain"]
        ParserIface["ParsingRuleAdapter<br/>────<br/>abstract interface"]
        ClangParser["ClangParser<br/>────<br/>implements ParsingRuleAdapter"]
        CompDBFactory["CompilationDatabaseFactory"]
        ClangParser -.->|implements| ParserIface
        ClangParser ==>|uses| CompDBFactory
    end

    subgraph CompilerAdapters["Compiler Adapters"]
        direction LR
        GCCTranslator["GccDbTranslator"]
        IARTranslator["IarDbTranslator"]
        MSVCTranslator["MsvcDbTranslator"]
    end

    subgraph Operations["Operation Domain"]
        OpVariant["RecipeOperation<br/>────<br/>std::variant"]
        SAlignOp["StructAlignmentOp"]
        OpVariant ==> SAlignOp
    end

    subgraph TransmuteDomain["Transmute Domain"]
        Recipe["Recipe<br/>────<br/>std::variant"]
        Transmute["Transmute<br/>────<br/>apply recipes to files"]
    end

    subgraph External["External Dependencies"]
        direction LR
        LLVM["LLVM/Clang<br/>────<br/>AST parsing"]
        CompDB["compile_commands.json<br/>────<br/>vendor-specific"]
    end

    %% App layer → Infrastructure
    App --> CLI
    App --> Discovery
    App --> Reporter

    %% App layer → Pipeline
    App ==> Pipeline

    %% Pipeline → Domain modules
    Pipeline ==>|uses| ParserIface
    Pipeline ==>|uses| OpVariant
    Pipeline ==>|uses| Transmute

    %% Operations produce recipes for Transmute
    SAlignOp ==>|produces| Recipe
    Recipe ==>|consumed by| Transmute

    %% Compiler adapters translate for the factory
    CompDBFactory ==> GCCTranslator
    CompDBFactory ==> IARTranslator
    CompDBFactory ==> MSVCTranslator

    %% External dependencies
    CompDBFactory ==>|loads| CompDB
    ClangParser -.->|wraps| LLVM
    CLI -.->|wraps| LLVM

    %% Styling
    classDef appLayer fill:#268bd2,stroke:#1e6ba8,stroke-width:2px,color:#fdf6e3
    classDef infra fill:#b58900,stroke:#8f6d00,stroke-width:2px,color:#fdf6e3
    classDef pipelineStyle fill:#2aa198,stroke:#208c84,stroke-width:2px,color:#fdf6e3
    classDef domain fill:#859900,stroke:#6c7a00,stroke-width:2px,color:#fdf6e3
    classDef external fill:#d33682,stroke:#a92866,stroke-width:2px,color:#fdf6e3

    class main,App appLayer
    class CLI,Discovery,Reporter infra
    class Pipeline pipelineStyle
    class ParserIface,ClangParser,CompDBFactory,GCCTranslator,IARTranslator,MSVCTranslator,OpVariant,SAlignOp,Recipe,Transmute domain
    class LLVM,CompDB external
```

## Compiler Adapter Detail

Zoomed-in view of the compiler adapter and translation

```mermaid
graph TB
    CompDB["compile_commands.json<br/>────<br/>vendor-specific flags"]

    subgraph Factory["CompilationDatabaseFactory"]
        Detect["Detect compiler type<br/>────<br/>inspect flags in DB"]
    end

    subgraph Translators["Stateless Translators (query-driver)"]
        direction LR
        IAR["IarDbTranslator<br/>────<br/>query iccarm for includes<br/>derive arch defines<br/>filter IAR flags"]
        MSVC["MsvcDbTranslator<br/>────<br/>translate /I→-I, /D→-D<br/>add -fms-extensions<br/>filter MSVC flags"]
        GCC["GccDbTranslator<br/>────<br/>query gcc for includes + triple<br/>inject -fshort-enums (ARM EABI)<br/>filter/translate GCC flags"]
    end

    Adapter["ClangCompilationDatabaseAdapter<br/>────<br/>stores translated commands<br/>implements CompilationDatabase"]

    Inference["inferMissingCompileCommands<br/>────<br/>LLVM wrapper for header-only files"]

    ClangParser["ClangParser::create()<br/>────<br/>TU pairing + argument adjusters"]

    CompDB ==>|loaded by| Factory
    Detect ==> IAR
    Detect ==> MSVC
    Detect ==> GCC

    IAR ==> Adapter
    MSVC ==> Adapter
    GCC ==> Adapter

    Adapter ==> Inference
    Inference ==> ClangParser

    classDef input fill:#d33682,stroke:#a92866,stroke-width:2px,color:#fdf6e3
    classDef factory fill:#b58900,stroke:#8f6d00,stroke-width:2px,color:#fdf6e3
    classDef translator fill:#859900,stroke:#6c7a00,stroke-width:2px,color:#fdf6e3
    classDef output fill:#268bd2,stroke:#1e6ba8,stroke-width:2px,color:#fdf6e3

    class CompDB input
    class Detect factory
    class IAR,MSVC,GCC translator
    class Adapter,Inference,ClangParser output
```

**Relationship Types:**
- **Thick arrows (`==>`)** — Runtime dependencies, data flow
- **Dotted arrows (`-.->`)** — Compile-time dependencies (interface implementation, wrapping)
- **Regular arrows (`-->`)** — Structural relationships

---

See [sequence-diagrams.md](sequence-diagrams.md) for runtime execution flows.
