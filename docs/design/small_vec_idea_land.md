# SmallVector Design Document — STATUS: NOT IMPLEMENTED

## Problem Statement

During the refactoring from polymorphic strategy pattern to `std::variant`, we removed direct LLVM dependencies from abstraction layers to achieve better separation of concerns and loose coupling. This involved replacing `llvm::SmallVector` with `std::vector` in public interfaces.

**Trade-off discovered:**
- ✅ Better abstraction boundaries and loose coupling
- ✅ Abstraction layers no longer expose LLVM types to clients
- ❌ Lost small buffer optimization (SBO) - went from potentially zero heap allocations to many
- ❌ ~2.5% performance regression in profiling

**Root cause:** `std::vector` always heap-allocates, while `llvm::SmallVector<T, N>` keeps small arrays (≤N elements) on the stack, avoiding allocations for the common case.

## Architecture Goals

1. **Loose coupling**: Abstraction layers (artifacts, operations) should not expose implementation-specific types in their public interfaces
2. **Performance**: Maintain small buffer optimization to avoid heap allocations for common case
3. **Swappability**: Ability to change underlying implementation (LLVM → Boost → custom) without changing client code
4. **Simplicity**: Thin wrapper, not re-inventing vector - just adapting existing implementations

## Solution: Type Alias Wrapper

Create `inc/core/small_vector.hpp` that provides `alchemy::SmallVector` as a type alias to the chosen implementation.

### Implementation (Option 1: Type Alias)

```cpp
// inc/core/small_vector.hpp
#ifndef ALCHEMY_SMALL_VECTOR_HPP
#define ALCHEMY_SMALL_VECTOR_HPP

// Implementation choice: swap this out if needed
#include <llvm/ADT/SmallVector.h>

namespace alchemy {

// Type alias for small buffer optimized vector
// Currently delegates to llvm::SmallVector for SBO
//
// Template parameters:
//   T - element type
//   N - number of elements in small buffer (stack-allocated)
//       If size <= N, no heap allocation occurs
//       If size > N, falls back to heap allocation
//
// To swap implementations (e.g., boost::container::small_vector),
// just change this file. Client code uses alchemy::SmallVector
// and remains unchanged (just recompile).
template<typename T, size_t N = 8>
using SmallVector = llvm::SmallVector<T, N>;

} // namespace alchemy

#endif // ALCHEMY_SMALL_VECTOR_HPP
```

### Usage Pattern

**Before (exposed LLVM dependency):**
```cpp
// inc/parsing/artifacts/artifacts.hpp
#include <llvm/ADT/SmallVector.h>  // ❌ Tight coupling to LLVM

struct StructDef {
  llvm::SmallVector<FieldDef, 16> fields;  // ❌ LLVM type in interface
};
```

**After (hidden implementation):**
```cpp
// inc/parsing/artifacts/artifacts.hpp
#include "core/small_vector.hpp"  // ✅ Alchemy abstraction

struct StructDef {
  alchemy::SmallVector<FieldDef, 16> fields;  // ✅ Alchemy type
};
```

## Buffer Size Guidelines

### Field Vectors (`FieldDef`)

Use existing constant:
```cpp
namespace alchemy::parser::detail {
  constexpr std::size_t FIELD_BUFFER_SZ{16};
}

// Usage:
alchemy::SmallVector<FieldDef, detail::FIELD_BUFFER_SZ> fields;
```

**Rationale**: 16 fields is a reasonable upper bound for most structs without being wasteful. Stack cost: 16 * sizeof(FieldDef) ≈ 16 * 64 bytes = 1KB.

### Struct Vectors (`ParseResults::structs`)

Consider larger initial size since file-level parsing typically yields multiple structs:
```cpp
// For 5 files with 100 structs each = 500 structs total
alchemy::SmallVector<StructDef, 32> structs;
```

**Heuristics**:
- Embedded projects: 20-50 structs → use N=32 or N=64
- Small/medium projects: 100-500 structs → use N=64 or N=128
- Large projects: 500+ structs → use std::vector (heap allocation unavoidable)

Can profile and adjust based on realistic workloads.

### Recipe Vectors

Operations that return recipes (e.g., `analyze()` returns `std::vector<RefactorRecipe>`):
```cpp
// 3 recipes per struct (reorder fields, update alignment, padding hint)
alchemy::SmallVector<RefactorRecipe, 16> analyze(...);
```

## Benefits

### Zero Overhead
- Type alias has zero runtime cost
- Compiler sees through the alias completely
- All inlining and optimizations apply as if using `llvm::SmallVector` directly

### Loose Coupling
- Client code includes `core/small_vector.hpp`, not LLVM headers
- Abstraction layers use `alchemy::SmallVector`, not `llvm::SmallVector`
- LLVM dependency hidden behind alchemy's abstraction

### Swappable Implementation

**To switch to Boost:**
```cpp
// Just change small_vector.hpp:
#include <boost/container/small_vector.hpp>

template<typename T, size_t N = 8>
using SmallVector = boost::container::small_vector<T, N>;
```

**To switch to custom implementation:**
```cpp
// Implement directly in small_vector.hpp:
template<typename T, size_t N = 8>
class SmallVector {
  // Custom implementation with SBO
  alignas(T) std::byte buffer_[sizeof(T) * N];
  // ... implementation ...
};
```

**Client code unchanged** - just recompile.

### Upgradeable to Full Wrapper

If custom behavior is needed later, change `using` to `class`:

```cpp
// From this:
template<typename T, size_t N>
using SmallVector = llvm::SmallVector<T, N>;

// To this (client code still compiles):
template<typename T, size_t N>
class SmallVector {
  llvm::SmallVector<T, N> impl_;
public:
  // Delegate all operations + add custom methods
};
```

## Alternatives Considered

### ❌ Keep `std::vector` with `.reserve()`
- Still heap-allocates unconditionally
- Doesn't solve the performance regression

### ❌ Use Boost dependency
- Introduces entirely new dependency just to avoid using existing LLVM dependency
- Doesn't make sense when already linking against LLVM

### ❌ Implement custom SmallVector from scratch
- Significant effort (~500+ lines for production-quality implementation)
- Need to handle: alignment, exception safety, move semantics, iterator invalidation
- Reinventing the wheel when battle-tested implementations exist

### ❌ Full wrapper class (Option 2 from alternatives)
- More boilerplate to maintain
- Slight compilation overhead
- No clear benefit over type alias for current needs
- Can upgrade to this later if needed

## Implementation Plan

1. **Create `inc/core/small_vector.hpp`** with type alias to `llvm::SmallVector`
2. **Update `artifacts.hpp`**:
   - Replace `std::vector<FieldDef>` → `alchemy::SmallVector<FieldDef, FIELD_BUFFER_SZ>`
   - Replace `std::vector<StructDef>` → `alchemy::SmallVector<StructDef, 64>` (tunable)
3. **Update operation return types** where beneficial:
   - `sortFieldsForOptimalAlignment()` returns `SmallVector<FieldDef, 16>`
   - `analyze()` returns `SmallVector<RefactorRecipe, 16>`
4. **Profile and measure**: Verify performance regression is recovered
5. **Document**: Update this file with actual performance measurements

## Expected Performance Impact

Based on callgrind profiling showing +47,957 instructions (+0.025%) and +50ms (+2.5%) wall-clock after variant refactoring, we expect:

- **Instruction count**: Should return to v2.6 baseline (~194.4M instructions)
- **Wall-clock**: Should reduce by 2-3% (~50ms faster)
- **Memory allocations**: Significant reduction in malloc/free calls (currently 6.7% of execution time)

**Key insight from profiling**: Memory allocation overhead is 13M instructions (~6.7%). SmallVector SBO should eliminate most `FieldDef` vector allocations for structs with ≤16 fields (majority case).

## Notes

- This is **not about re-inventing vector** - we're adapting existing high-quality implementations
- LLVM is already a binary dependency (we link `libLLVM-14.so.1`)
- The issue was **header-level coupling**, not binary-level dependency
- Type alias achieves both loose coupling AND performance
- If LLVM ever becomes truly undesirable, swap to Boost or custom with one-line change
