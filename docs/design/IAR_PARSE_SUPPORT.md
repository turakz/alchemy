# IAR Parse Support — Plan (Followups)

Status: proposed — pending review against EWARM Compiler Reference Guide.

Branch goal: translate an IAR-generated compilation database into a form
acceptable to libclang so that IAR types, std types, and app types resolve at
parse time.

---

## In-Scope for This Branch

### Item 1: Extend `getClangCompatibleDefines()` with missing intrinsic keywords

**Proposal**: add stubs for IAR ARM keywords that appear in real code but
currently break parses.

**Candidates** (verify against local EWARM Compiler Reference Guide before
locking in — Hard Requirement #8):

```cpp
// in getClangCompatibleDefines(), additions:
"-D__monitor=",          // critical-section / disable-IRQ functions
"-D__absolute=",         // absolute-address placement (linker control)
"-D__raw=",              // no prologue/epilogue functions (rare, but seen)
"-D__regvar=",           // register variable (older EWARM)
"-D__svc=",              // ARMv7-M name for software interrupt; coexists with __swi
"-D__exception=",        // exception handler variant of __interrupt
```

**TrustZone (ARMv8-M, M23/M33)** — defer until needed; these take parameters:
- `__cmse_nonsecure_call`, `__cmse_nonsecure_entry`

**Reasoning**: same failure mode as the `__interrupt` bug — one uncovered
keyword in a guarded header → whole TU silently skipped → empty parse results.
Each addition is ~$0 cost; risk is only false positives if a user happens to
use the identifier as a non-keyword (vanishingly unlikely with `__` prefix —
reserved per C standard §7.1.3).

**Test addition** (`test_compilation_database_adapter.cpp`):

```cpp
TEST_F(CompilationDatabaseFactoryTest,
       TestIarTranslatorCompatDefinesIncludeExtendedKeywords)
{
  // ... existing setup ...
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__monitor="));
  ASSERT_THAT(translated.CommandLine, ::testing::Contains("-D__absolute="));
  // ... etc
}
```

**Open question**: extend `iar_compat.h` fixture + integration test to
exercise one of these (e.g. `__monitor`-guarded struct), matching the
pattern set with `__interrupt`?

---

### Item 2: Translate `--preinclude <file>` → `-include <file>`

**Proposal**: add explicit translation in `translateCommand` before the
generic `--` skip clause.

**Before** (current behavior: `--preinclude foo.h` is dropped):

```cpp
if (arg.starts_with("--"))
{
  if (idx + 1 < iarCommand.CommandLine.size() &&
      !iarCommand.CommandLine[idx + 1].starts_with("-"))
  {
    ++idx;  // drops the file path with the flag
  }
  continue;
}
```

**After**:

```cpp
// IAR --preinclude <file> → clang -include <file>
if (arg == "--preinclude")
{
  if (idx + 1 < iarCommand.CommandLine.size())
  {
    translatedArgs.emplace_back("-include");
    translatedArgs.emplace_back(iarCommand.CommandLine[++idx]);
  }
  continue;
}

if (arg.starts_with("--"))
{
  // ... existing skip logic
}
```

**Reasoning**:
- IAR doc form: `--preinclude <file>` (space-separated, per EWARM Compiler
  Reference Guide — verify whether equals form `--preinclude=<file>` also
  exists; if it does, both must be handled)
- Clang equivalent `-include` is documented and stable
- Common in projects with a forced compat shim header (e.g.
  `project_compat.h`)
- Without this, types defined only in the preinclude vanish → parse-time
  symbol errors → TU skip

**Test additions**:
1. Unit (`test_compilation_database_adapter.cpp`): cmd with
   `--preinclude /path/to/compat.h` → translated cmd contains
   `-include /path/to/compat.h` in correct order.
2. Integration (`test_clang_parser.cpp`): fixture project where `main.c`
   references a type defined only in a header that's not `#include`d —
   provided via `--preinclude` in the DB. Assert struct is found.

**Estimated diff size**: ~10 lines source, ~30 lines tests.

---

## Follow-up Tickets (Out of Scope for This Branch)

### Restore `--include_path` translation in IAR translator
- **Why**: IAR project-file exports (.ewp → compile_commands.json) emit
  `--include_path=<dir>` rather than `-I<dir>`. Currently dropped by
  allowlist (was added in commit `9321a08`, removed in `74a4843` "until a
  need arises").
- **Trigger to prioritize**: first time someone feeds a `.ewp`-generated DB
  into alchemy and gets "header not found" errors.
- **Acceptance**: integration test with a DB using only `--include_path=`
  form resolves project types.

### Honor `#pragma data_alignment=N` in salign analysis (correctness)
- **Why**: clang ignores IAR's `#pragma data_alignment` → salign computes
  wrong layouts → misleading recipes.
- **Severity**: silent correctness issue, not a parse failure.
- **Approach**: either (a) translate the pragma to
  `__attribute__((aligned(N)))` via a libtooling rewriter pass before parse,
  or (b) detect the pragma in `clang_struct_extractor` and adjust
  `StructDef`/`FieldDef` alignment manually.
- **Note**: `#pragma pack` is fine — clang honors it natively.

### Non-ARM IAR variant support (iccavr / iccrx / iccrl78)
- **Why**: `translateCommand` hardcodes `-target arm-none-eabi`. Comment in
  source already flags this as future work (`iar_database_translator.cpp`
  lines 96–100).
- **Approach**: detect target from compiler binary basename (`iccavr`,
  `iccrx`, etc.) or predef macros (`__ICCAVR__`, `__ICCRX__`).
- **Scope hint**: needs target-triple table + per-variant arch-define
  derivation. Largest of the followups.

### ARMv8.1-M architecture defines (M55, M85)
- **Why**: `deriveArchitectureDefines` doesn't cover newest Cortex-M cores.
- **Acceptance**: unit test with `--cpu Cortex-M55` produces correct
  `__CORE__` / `__ARM_ARCH_*__` defines.
- **Priority**: low until a real M55/M85 project shows up.

### IAR C++ mode (`iccarm --c++`)
- **Why**: translator emits no `-x c++`, doesn't account for C++-only IAR
  keywords or extern-C concerns.
- **Approach**: detect `--c++` / `--ec++` / `--eec++` in source DB → emit
  `-x c++` and adjust language standard flag.

---

## Workflow Checkpoints (per CLAUDE_ALCHEMY_README 6-step)

Before drafting any code, verify:

1. **Keyword list against EWARM docs** — confirm or pare the
   `__monitor` / `__absolute` / `__raw` / `__regvar` / `__svc` /
   `__exception` set.
2. **`--preinclude` syntax forms** — space-only, or also
   `--preinclude=<file>`?
3. **Fixture extension decision** — extend existing `iar_compat.h` or add
   a second fixture file?

Order of work after answers (no batching):
- Item 1 first (pure define additions, lowest risk)
- Item 2 second (control-flow change in `translateCommand`)
