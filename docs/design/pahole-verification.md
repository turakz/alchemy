# Pahole Post-Rebuild Verification — STATUS: PROPOSAL, NOT YET IMPLEMENTED

**Document Version**: 1.0
**Created**: 2026-03-13
**Status**: Proposal — not yet implemented

---

## Summary

After alchemy reorders struct fields and the developer rebuilds, there is no automated way to confirm the compiled binary reflects the predicted layout improvements. This feature adds `--verify <binary>`, which uses [pahole](https://linux.die.net/man/1/pahole) to read DWARF debug info from a rebuilt ELF binary and cross-check struct sizes against alchemy's source-level predictions.

**Goal**: Provide a QA post-processing step that closes the loop between source-level refactoring and binary-level results.

---

## Workflow

```bash
# 1. alchemy refactors source files (existing)
alchemy --salign -b build/ "src/**/*.h"

# 2. developer rebuilds (with -g for DWARF debug info)
make -C build/

# 3. verify refactoring took effect in the binary
alchemy --verify build/my_binary "src/**/*.h"
```

`--verify` is mutually exclusive with `--salign` and `--dry-run`. It is a read-only operation — no source files are modified.

---

## What pahole provides

pahole (part of the `dwarves` package) reads DWARF/BTF/CTF debug info from ELF binaries.

| Feature | Detail |
|---------|--------|
| Input | ELF binary or `.o` file compiled with `-g` |
| Output | Text-based (no JSON); stable, parseable format |
| Key flags | `--sizes` (compact `name,size,holes`), `-C name` (filter to struct), `--separator=','` (CSV-like), `--packable` (still-optimizable structs) |
| Install | `apt install dwarves` / `dnf install dwarves` |

### Relevant output formats

**`--sizes` mode** (easiest to parse):
```
$ pahole --sizes --separator=',' binary.elf
SensorData,40,0
ConfigBlock,112,1
ChannelState,32,2
```

Each line: `struct_name,size_in_bytes,number_of_holes`

**Default annotated mode** (detailed, for future use):
```
struct SensorData {
        double                     temperature;          /*     0     8 */
        int                        id;                   /*     8     4 */
        char                       label[4];             /*    12     4 */

        /* size: 16, cachelines: 1, members: 3 */
        /* last cacheline: 16 bytes */
};
```

---

## Output examples

### Clean verification (all structs match predictions)

```
alchemy::verify::binary: build/firmware.elf
alchemy::verify::checked 42 structs across 15 files

  CONFIRMED  SensorData       48B -> 40B  (saved 8B, matches binary)
  CONFIRMED  ConfigBlock     128B -> 112B (saved 16B, matches binary)
  CONFIRMED  ChannelState     64B -> 48B  (saved 16B, matches binary)

alchemy::verify::summary::42 confirmed, 0 mismatches, 0 not found
```

### Mismatches detected

```
alchemy::verify::binary: build/firmware.elf
alchemy::verify::checked 42 structs across 15 files

  CONFIRMED  SensorData       48B -> 40B  (saved 8B, matches binary)
  MISMATCH   ConfigBlock      source: 112B, binary: 128B (stale build?)
  NOT_FOUND  InternalHelper   (not in binary — header-only or optimized out)

alchemy::verify::summary::39 confirmed, 2 mismatches, 1 not found
```

### pahole not installed

```
alchemy::verify::error: 'pahole' not found on PATH
  install with: apt install dwarves (Debian/Ubuntu)
                dnf install dwarves (Fedora/RHEL)
```

### Binary lacks DWARF info

```
alchemy::verify::error: no DWARF debug info in 'build/firmware.elf'
  rebuild with -g flag to enable struct layout verification
```

### Struct name collision (multiple definitions)

```
alchemy::verify::binary: build/firmware.elf

  CONFIRMED  ns1::SensorData   40B  (matches binary)
  AMBIGUOUS  Config             source: 24B, binary has 2 definitions (24B, 32B)
                                 (pahole found multiple 'Config' — use fully qualified name in source)

alchemy::verify::summary::40 confirmed, 0 mismatches, 1 ambiguous, 1 not found
```

---

## Architecture

```
CLI: --verify <binary_path> <source_patterns...>
  |
  +-- Parse source files (existing libclang parser)
  |     -> collect struct names + naturalTotalSize from StructDef
  |
  +-- Run pahole against binary
  |     -> pahole --sizes --separator=',' -C name1,name2,... <binary>
  |     -> parse CSV output: struct_name,size,num_holes
  |
  +-- Cross-reference: source structs vs binary structs
  |     -> match by struct name
  |     -> compare source naturalTotalSize vs binary size
  |     -> classify: Confirmed / Mismatch / NotFound
  |
  +-- Report results
        -> color-coded terminal output (existing color scheme)
        -> exit code: 0 if no mismatches, 1 if any mismatches
```

This is a **separate execution path** from the normal parse -> execute -> transmute pipeline. When `--verify` is active, alchemy skips salign/transmute entirely.

---

## Changes

### CLI layer

**`inc/cli/cli.hpp`**:
- Add `std::string verifyBinaryPath` to `CliInputs`
- Add `std::filesystem::path verifyBinaryPath` to `ParsedOptions`

**`src/cli/cli.cpp`**:
- Add `llvm::cl::opt<std::string> verifyOpt("verify", desc("verify struct layouts against a compiled binary via pahole"), value_desc("binary_path"))`
- Validation: mutually exclusive with `--salign`/`--dry-run`; binary must exist; binary must be a regular file
- Merge support in `mergeInputs`

**`src/cli/config_parser.cpp`**:
- Add `verify` key to toml load/dump

### New modules

| File | Purpose |
|------|---------|
| `inc/verify/pahole_runner.hpp` | Pahole CLI wrapper: find pahole, run queries, parse output |
| `src/verify/pahole_runner.cpp` | Implementation: `popen`-based execution, CSV parsing |
| `inc/verify/verify.hpp` | Verification types (`VerifyStatus`, `StructVerifyResult`, `VerifyReport`) and orchestrator |
| `src/verify/verify.cpp` | Cross-referencing logic: source sizes vs binary sizes |
| `inc/reporting/verify_reporter.hpp` | Verify report display interface |
| `src/reporting/verify_reporter.cpp` | Color-coded terminal output for verification results |

### App execution

**`src/app/app.cpp`** — early branch in `exec()`:
```cpp
if (!context().cliArgs.verifyBinaryPath.empty())
{
  // parse-only path (no operations, no transmute)
  auto parser = ClangParser::create(sourceFiles, buildDir);
  auto artifacts = parser->parse({.structDefs = true});
  auto report = alchemy::verify::verify(artifacts, verifyBinaryPath);
  alchemy::verify::reporter::report(report);
  return report.mismatches > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
```

### CMake

Add new source files to the alchemy target in `CMakeLists.txt`.

---

## Key design decisions

| Decision | Rationale |
|----------|-----------|
| Shell out to pahole CLI, don't link libdwarves | libdwarves API is undocumented and unstable; CLI output format is stable for 15+ years; avoids new build dependency |
| Use `--sizes` mode, not default annotated mode | Simpler parsing (3-field CSV vs multi-line regex); sufficient for size verification; annotated mode can be added later for per-field comparison |
| Match structs by name only | pahole reports unqualified names from DWARF; namespace-qualified matching would require `--class_name` with mangled names, which is fragile across compilers |
| Separate execution path, not pipeline stage | Verification runs after rebuild (different point in time than refactoring); coupling it into the pipeline would force an awkward "run twice" workflow |
| Exit code reflects mismatches | Enables CI integration: `alchemy --verify binary patterns && echo "verified"` |

---

## Limitations and future work

- **Name collisions**: C structs with identical names in different translation units may produce ambiguous results. Future: use `pahole -I` (show source file/line) for disambiguation
- **Templates**: pahole sees each template instantiation as a separate type (e.g., `vector<int>`, `vector<double>`). These won't match source-level template names. Future: skip template types or match by instantiation
- **Cross-compilation**: pahole reads the host architecture's DWARF. For cross-compiled binaries (e.g., ARM target built on x86 host), struct sizes may differ from host expectations. Future: `--word-size` pass-through
- **Per-field comparison**: Currently only compares total struct size. Future: parse annotated output to compare individual field offsets and detect field-level mismatches
- **Detailed annotated output**: The default pahole output (with `/* offset size */` comments) could power a rich diff view showing exactly where layouts diverge. Deferred to a future iteration

---

## Testing

**Unit tests** (`tests/unit/test_pahole_runner.cpp`):
- Parse well-formed CSV output
- Parse output with holes=0 and holes>0
- Handle empty output (no matching structs)
- Handle malformed lines gracefully

**Unit tests** (`tests/unit/test_verify.cpp`):
- Confirmed: source size == binary size
- Mismatch: source size != binary size
- NotFound: struct in source but not in pahole output
- Mixed results across multiple structs
- Empty source/empty binary results

**Integration test** (manual):
```bash
# build sandbox with debug info
cd sandbox/build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make
# run verify
./build/debug/alchemy --verify sandbox/build/test_binary "sandbox/*.h"
```

---

**Next Steps**: Review and approve this design, then implement.
