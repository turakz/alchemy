# Refactor Plan: `ClangParser::run()` — Requirements-Driven Multi-Artifact Support

## Goal

Change the private `run()` methods in `ClangParser` so they accept `ParsingRequirements` and
return `Result<ParseResults>` instead of `Result<std::vector<StructDef>>`. This makes a single
AST pass naturally support multiple artifact types, and aligns the implementation with the
interface contract that already exists at the `parse()` level.

## Motivation

- Private `run()` currently returns `std::vector<StructDef>` — an artifact-specific type locked
  to struct parsing
- When a second artifact type arrives (e.g. `FuncDef`), the alternative is two separate AST
  passes (wasteful) or a larger structural change under pressure
- The public interface (`ParsingRuleAdapter::parse()` returning `ParseResults`) is already
  correctly abstracted; this brings the implementation in line with it
- `ParseResults` and `ParsingRequirements` are already designed as extensible aggregates — this
  change lets the private layer use them too

## Scope: Files Changed

| File | Change |
|---|---|
| `inc/parsing/libclang/clang_parser.hpp` | Private `run()` signatures only |
| `src/parsing/libclang/clang_parser.cpp` | Both `run()` bodies + `parse()` call sites |

No header other than `clang_parser.hpp` changes. No public interface changes. No test changes
required (see below).

---

## Steps

### Step 1 — Update `run()` signatures in `clang_parser.hpp`

Two private declarations change.

Before:
```cpp
Result<std::vector<alchemy::parser::artifacts::StructDef>>
run(const ParseCommand& cmd) const;

Result<std::vector<alchemy::parser::artifacts::StructDef>>
run(const std::string& header) const;
```

After:
```cpp
Result<alchemy::parser::artifacts::ParseResults>
run(const ParseCommand& cmd,
    const alchemy::parser::ParsingRequirements& requirements) const;

Result<alchemy::parser::artifacts::ParseResults>
run(const std::string& header,
    const alchemy::parser::ParsingRequirements& requirements) const;
```

---

### Step 2 — Update `run(const ParseCommand&)` body in `clang_parser.cpp`

The tool setup and `tool.run()` call are unchanged. Only the rule setup block and return change.

Before:
```cpp
auto structParser = std::make_unique<ClangStructParsingRule>(m_targetHeaders);
clang::ast_matchers::MatchFinder finder;
structParser->registerMatchers(finder);

// ... tool.run() ...

if (structParser->hasParseErrors()) { /* return failure */ }

auto structs = structParser->getParsedStructs();
for (auto& s : structs) { s.targetName = spec.targetName; }

return Result<std::vector<StructDef>>::success(std::move(structs));
```

After:
```cpp
clang::ast_matchers::MatchFinder finder;
std::unique_ptr<ClangStructParsingRule> structParser;

if (requirements.needsStructParsing) {
  structParser = std::make_unique<ClangStructParsingRule>(m_targetHeaders);
  structParser->registerMatchers(finder);
}
// future: if (requirements.needsFunctionParsing) { ... }

// ... tool.run() unchanged ...

if (structParser && structParser->hasParseErrors()) { /* return failure — same message */ }

alchemy::parser::artifacts::ParseResults out;
if (structParser) {
  out.structs = structParser->getParsedStructs();
  for (auto& s : out.structs) { s.targetName = spec.targetName; }
}
// future: if (funcParser) { out.functions = funcParser->getParsedFunctions(); }

return Result<ParseResults>::success(std::move(out));
```

The same pattern applies to `run(const std::string& header)` — identical structural change,
different tool setup preamble.

---

### Step 3 — Update `parse()` call sites and merge loop in `clang_parser.cpp`

Two things change: the early-return guard becomes a multi-flag check, and the merge loop uses
`ParseResults` directly.

Before:
```cpp
if (!requirements.needsStructParsing) {
  return Result<ParseResults>::success(std::move(results));
}
// ...
auto parseResult = run(cmd);
// ...
auto structs = parseResult.value();
results.structs.insert(end(results.structs),
                       make_move_iterator(begin(structs)),
                       make_move_iterator(end(structs)));
```

After:
```cpp
// guard expands naturally as more flags are added
if (!requirements.needsStructParsing /* && !requirements.needsFunctionParsing */) {
  return Result<ParseResults>::success(std::move(results));
}
// ...
auto parseResult = run(cmd, requirements);
// ...
auto cmdResults = parseResult.value();
results.structs.insert(end(results.structs),
                       make_move_iterator(begin(cmdResults.structs)),
                       make_move_iterator(end(cmdResults.structs)));
// future: results.functions.insert(...);
```

The same change applies to the direct-header fallback loop (`run(header, requirements)`). The
`deduplicateStructs(results.structs)` call at the end of `parse()` is unchanged.

---

## Test Impact

### Unit tests (`tests/unit/test_clang_parser.cpp`) — no changes required

All 9 unit tests call `parse()` through the public interface, which does not change signature.
Two tests worth verifying after the change:

- `ParseRespectsStructParsingFlag` — tests `needsStructParsing=false` yields empty
  `results.structs`. Behavior preserved: the early-return guard in `parse()` still fires before
  any `run()` call.
- `ParseCanBeCalledMultipleTimes` — tests two consecutive `parse()` calls return consistent
  results. Unaffected — nothing about `run()` call state changes.

### Integration tests — no changes required

All integration tests go through `parse()`. The internal signature change is invisible to them.

---

## What Stays the Same

- `ParsingRuleAdapter::parse()` — public interface signature unchanged
- `ParseResults`, `ParsingRequirements` — no changes to these types
- `ClangStructParsingRule` — no changes; remains a self-contained rule
- `deduplicateStructs()` — called on `results.structs` as before
- All error messages in `run()` — same text, same failure conditions, just different `Result`
  wrapper type
- `ClangParser::create()` factory — completely unchanged
