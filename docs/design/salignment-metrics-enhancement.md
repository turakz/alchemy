# Struct Alignment Metrics Enhancement

**Document Version**: 1.0
**Created**: 2025-01-23
**Status**: Design Proposal

---

## Executive Summary

Current salign metrics focus on **space savings** (bytes saved, percentage reduction). This document proposes **cache-aware metrics** that provide deeper insights into memory layout performance characteristics, helping users understand the impact of struct optimization on cache utilization, memory bandwidth, and access patterns.

**Goal**: Transform salign from a "struct optimizer" into a **cache-aware struct analyzer** that teaches users about memory layout performance.

---

## Background: Memory Hierarchy and Alignment

### The Memory Hierarchy

Modern computers have a multi-tiered memory system:

```
┌─────────────────────────────────────────────────────────────┐
│                     CPU Registers                            │
│                  ~1 cycle latency                            │
│                  64-128 bytes total                          │
└─────────────────────────────────────────────────────────────┘
                           ▲
                           │
┌─────────────────────────────────────────────────────────────┐
│                     L1 Cache (per core)                      │
│                  ~4 cycles latency                           │
│                  32-64 KB (split: 32KB data, 32KB instr)    │
└─────────────────────────────────────────────────────────────┘
                           ▲
                           │
┌─────────────────────────────────────────────────────────────┐
│                     L2 Cache (per core)                      │
│                  ~12 cycles latency                          │
│                  256 KB - 512 KB                             │
└─────────────────────────────────────────────────────────────┘
                           ▲
                           │
┌─────────────────────────────────────────────────────────────┐
│                     L3 Cache (shared)                        │
│                  ~40 cycles latency                          │
│                  8 MB - 64 MB                                │
└─────────────────────────────────────────────────────────────┘
                           ▲
                           │
┌─────────────────────────────────────────────────────────────┐
│                     Main Memory (RAM)                        │
│                  ~200 cycles latency                         │
│                  8 GB - 128 GB                               │
└─────────────────────────────────────────────────────────────┘
```

**Key insight**: Each level is ~10x slower but ~100x larger than the previous.

### Cache Lines - The Fundamental Transfer Unit

Memory is **not** transferred byte-by-byte. Instead, it moves in fixed-size chunks called **cache lines**.

**Typical cache line size: 64 bytes** (on x86-64, ARM64)

#### Example: Reading a Single Byte

```
RAM (conceptual view):
Address:  0x1000  0x1008  0x1010  0x1018  0x1020  0x1028  0x1030  0x1038
         ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
         │   A   │   B   │   C   │   D   │   E   │   F   │   G   │   H   │
         └───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
                           ▲
                           │
                    You want to read byte at 0x1010
```

**What actually happens:**
```
CPU requests: Read 1 byte at 0x1010

Cache checks: Is cache line containing 0x1010 present?
  → NO (cache miss)

Cache fetches ENTIRE cache line (64 bytes):
  → Fetches addresses 0x1000 - 0x103F (aligned to 64-byte boundary)

L1 Cache now contains:
         ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
0x1000:  │   A   │   B   │   C   │   D   │   E   │   F   │   G   │   H   │
         └───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
         └────────────────── 64 bytes ──────────────────────────────────┘

CPU receives: The 1 byte at 0x1010 (but 64 bytes were transferred)
```

**Cost**: ~200 cycles for the miss, but subsequent accesses to nearby addresses are ~4 cycles (L1 hit).

### Natural Alignment Rule

A data type of size N bytes should be aligned to an N-byte boundary (or nearest power of 2).

```
Type         Size    Natural Alignment    Rule
────────────────────────────────────────────────
char         1       1                    Any address
short        2       2                    Address % 2 == 0
int          4       4                    Address % 4 == 0
long         8       8                    Address % 8 == 0
double       8       8                    Address % 8 == 0
pointer      8       8                    Address % 8 == 0 (on 64-bit)
```

### Why Alignment Matters: The Unaligned Read Problem

**Example: Reading a misaligned 8-byte `long`**

```
Scenario 1: ALIGNED READ (address 0x1000)
────────────────────────────────────────────────
Cache Line Boundary:  0x1000                    0x1040
                      ▼                          ▼
         ┌────────────┬──────────────────────────┬─────────────
         │ long value │                          │
         └────────────┴──────────────────────────┴─────────────
         0x1000       0x1008

Result: 1 cache line read, 1 memory access
Cost: ~4 cycles (L1 hit) or ~200 cycles (L1 miss)


Scenario 2: MISALIGNED READ (address 0x103C)
────────────────────────────────────────────────
Cache Line Boundary:  0x1000        0x1040        0x1080
                      ▼             ▼             ▼
         ┌────────────┬──────┬──────────┬────────┬─────────────
         │            │ lo   │   hi     │        │
         └────────────┴──────┴──────────┴────────┴─────────────
                      0x103C │          0x1044
                             ▼ (straddles boundary!)
                      long value (8 bytes)

Result: 2 cache line reads, 2 memory accesses, CPU must stitch together
Cost: 2x the memory bandwidth, 2x the cache pollution
      On some architectures: HARDWARE EXCEPTION (crash)
```

**Takeaway**: Misaligned accesses can be 2x slower or cause crashes.

---

## Struct Padding and Layout Impact

### Example: Poorly Ordered Struct

```c
struct BadExample {
    char   a;     // 1 byte
    long   b;     // 8 bytes (needs 8-byte alignment)
    char   c;     // 1 byte
    int    d;     // 4 bytes (needs 4-byte alignment)
};
```

**Memory layout (with padding shown as `·`):**

```
Offset:   0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
        ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
Field:  │ a  │ ·  │ ·  │ ·  │ ·  │ ·  │ ·  │ ·  │      b (long, 8 bytes)      │ c  │ ·  │
        └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
           ▲                                               ▲                   ▲
           │                                               │                   │
       char a (1 byte)                                 long b (aligned to 8)  char c

Offset:  16   17   18   19   20   21   22   23
        ┌─────────────┬────┬────┬────┬────┐
Field:  │  d (int, 4) │ ·  │ ·  │ ·  │ ·  │  ← Padding to align struct size to 8
        └─────────────┴────┴────┴────┴────┘
           ▲
           │
       int d (aligned to 4)

Total size: 24 bytes
Wasted padding: 11 bytes (46% waste!)
```

**Why the padding?**
- After `a` (offset 0), `b` needs 8-byte alignment → 7 bytes of padding
- After `c` (offset 16), `d` needs 4-byte alignment → 1 byte padding
- After `d` (offset 20), struct must be 8-byte aligned for arrays → 4 bytes padding

### Optimized Struct (Largest Alignment First)

```c
struct GoodExample {
    long   b;     // 8 bytes
    int    d;     // 4 bytes
    char   a;     // 1 byte
    char   c;     // 1 byte
};
```

**Memory layout:**

```
Offset:   0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
        ┌───────────────────────────────────────┬─────────────┬────┬────┬────┬────┐
Field:  │      b (long, 8 bytes)                │ d (int, 4)  │ a  │ c  │ ·  │ ·  │
        └───────────────────────────────────────┴─────────────┴────┴────┴────┴────┘
           ▲                                        ▲            ▲    ▲
           │                                        │            │    │
       long b (aligned to 8)                    int d (aligned) chars pack together

Total size: 16 bytes
Wasted padding: 2 bytes (12.5% waste)
Space savings: 33% reduction (24 → 16 bytes)
```

---

## Arrays of Structs and Cache Line Effects

### Scenario: Array of Structs

```c
struct Point {
    double x;  // 8 bytes
    double y;  // 8 bytes
};  // Total: 16 bytes, aligned to 8

Point points[100];  // 1600 bytes total
```

**Memory layout in cache:**

```
Cache Line 0 (64 bytes):
┌──────────┬──────────┬──────────┬──────────┐
│ points[0]│ points[1]│ points[2]│ points[3]│
│  (16 B)  │  (16 B)  │  (16 B)  │  (16 B)  │
└──────────┴──────────┴──────────┴──────────┘
           4 structs per cache line

Cache Line 1 (64 bytes):
┌──────────┬──────────┬──────────┬──────────┐
│ points[4]│ points[5]│ points[6]│ points[7]│
└──────────┴──────────┴──────────┴──────────┘

Efficiency: 64/64 = 100% (no waste, perfect packing)
```

### Bad Example: Poorly Packed Struct

```c
struct BadPoint {
    double x;   // 8 bytes
    double y;   // 8 bytes
    char flag;  // 1 byte
    // Padding: 7 bytes (to align to 8 for next struct in array)
};  // Total: 24 bytes

BadPoint points[100];  // 2400 bytes total
```

**Memory layout in cache:**

```
Cache Line 0 (64 bytes):
┌────────────────┬────────────────┬────────────────┐  (partial)
│  points[0]     │  points[1]     │  points[2]     │ ← 8 bytes overflow
│   (24 B)       │   (24 B)       │   (24 B)       │   to next line!
└────────────────┴────────────────┴────────────────┘
     16 + 8       16 + 8 + 8       16
     ▲
     │ x (8) + y (8) + flag (1) + padding (7)

Efficiency: 56/64 = 87.5% (8 bytes wasted per cache line)
Structs per line: 2.67 (fractional = BAD!)
```

**Problem**: The 3rd struct straddles cache line boundaries → accessing `points[2]` may require 2 cache line fetches!

---

## Proposed Metrics

### Current Metrics (v2.5)

```cpp
struct SAlignMetrics {
    std::filesystem::path sourceFile;
    std::string structName;
    std::size_t originalSize;
    std::size_t optimizedSize;
    std::size_t bytesSaved;
    double percentageReduction;
};
```

**Limitations**:
- Focuses only on space savings
- No insight into cache behavior
- No guidance on array allocation
- Doesn't explain *why* the optimization helps performance

---

## Tier 1: Essential Cache Metrics (High Value, Easy to Compute)

### 1. Cache Line Utilization

**What it measures**: How efficiently structs pack into cache lines.

**Calculation**:
```cpp
struct CacheLineMetrics {
    size_t structSize;
    size_t cacheLineSize = 64;  // Configurable per architecture

    // Computed values
    double structsPerCacheLine;      // floor(64 / struct_size)
    double cacheLineUtilization;     // Percentage of cache line used
    size_t wastedBytesPerLine;       // 64 - (structs_per_line * struct_size)
};

// Example calculation
CacheLineMetrics calc(size_t structSize) {
    CacheLineMetrics m;
    m.structSize = structSize;
    m.structsPerCacheLine = std::floor(64.0 / structSize);
    size_t bytesUsed = m.structsPerCacheLine * structSize;
    m.wastedBytesPerLine = 64 - bytesUsed;
    m.cacheLineUtilization = (bytesUsed / 64.0) * 100.0;
    return m;
}
```

**Example output**:
```
Before optimization:
  Struct size: 24 bytes
  Structs per cache line: 2 (2.67 rounded down)
  Cache line utilization: 75.0% (48 bytes used, 16 bytes wasted)

After optimization:
  Struct size: 16 bytes
  Structs per cache line: 4
  Cache line utilization: 100.0% (64 bytes used, 0 bytes wasted)

Improvement: +25% cache line utilization
```

**Interpretation guide**:
- 100% = Excellent (perfect packing, power-of-2 struct size that divides 64)
- 87-99% = Good (minor waste)
- 75-86% = Fair (significant waste, consider further optimization)
- <75% = Poor (struct size doesn't pack well, major cache line waste)

---

### 2. Memory Density

**What it measures**: Ratio of actual data to total struct size (including padding).

**Calculation**:
```cpp
struct MemoryDensityMetrics {
    size_t totalFieldBytes;    // Sum of actual field sizes
    size_t totalStructBytes;   // Including padding
    size_t paddingBytes;       // Total padding
    double densityPercentage;  // (totalFieldBytes / totalStructBytes) * 100
};

// Example
MemoryDensityMetrics calc(const std::vector<FieldDef>& fields, size_t structSize) {
    MemoryDensityMetrics m;
    m.totalFieldBytes = std::accumulate(fields.begin(), fields.end(), 0,
        [](size_t sum, const FieldDef& f) { return sum + f.size; });
    m.totalStructBytes = structSize;
    m.paddingBytes = structSize - m.totalFieldBytes;
    m.densityPercentage = (m.totalFieldBytes / (double)structSize) * 100.0;
    return m;
}
```

**Example output**:
```
Before optimization:
  Field bytes: 14 bytes (char + long + char + int)
  Struct size: 24 bytes
  Padding: 10 bytes
  Memory density: 58.3%

After optimization:
  Field bytes: 14 bytes (unchanged)
  Struct size: 16 bytes
  Padding: 2 bytes
  Memory density: 87.5%

Improvement: +29.2% density (80% less padding)
```

---

### 3. Optimal Array Multiple

**What it measures**: Recommended array size for perfect cache line alignment.

**Calculation**:
```cpp
struct ArrayAllocationMetrics {
    size_t optimalArrayMultiple;   // LCM(struct_size, cache_line) / struct_size
    size_t recommendedMinSize;      // Minimum array size for good cache alignment
};

// Using std::lcm from C++17
size_t calcOptimalMultiple(size_t structSize, size_t cacheLineSize = 64) {
    return std::lcm(structSize, cacheLineSize) / structSize;
}
```

**Example output**:
```
Struct size: 24 bytes

Optimal array allocation:
  Allocate arrays in multiples of: 8 structs
  Reasoning: 8 structs * 24 bytes = 192 bytes = 3 cache lines (perfect alignment)

For best cache performance:
  Good: array[8], array[16], array[24], ...
  Suboptimal: array[7], array[10], array[15], ... (straddling boundaries)
```

**Use case**: When users allocate dynamic arrays (`new Point[N]`), this tells them the optimal N for cache alignment.

---

## Tier 2: Advanced Metrics (High Value, Moderate Complexity)

### 4. Estimated Bandwidth Savings (Array-Based)

**What it measures**: Reduction in memory traffic for array operations.

**Calculation**:
```cpp
struct BandwidthMetrics {
    // Input
    size_t arraySize;           // Number of elements (user-provided or estimated)
    size_t structSizeBefore;
    size_t structSizeAfter;

    // Computed
    size_t totalBytesBefore;
    size_t totalBytesAfter;
    size_t bytesSaved;
    size_t cacheLinesBefore;
    size_t cacheLinesAfter;
    size_t cacheLinesSaved;
    double bandwidthReduction;  // Percentage
};

BandwidthMetrics calc(size_t arraySize, size_t sizeBefore, size_t sizeAfter) {
    BandwidthMetrics m;
    m.arraySize = arraySize;
    m.structSizeBefore = sizeBefore;
    m.structSizeAfter = sizeAfter;

    m.totalBytesBefore = arraySize * sizeBefore;
    m.totalBytesAfter = arraySize * sizeAfter;
    m.bytesSaved = m.totalBytesBefore - m.totalBytesAfter;

    // Round up to nearest cache line
    m.cacheLinesBefore = (m.totalBytesBefore + 63) / 64;
    m.cacheLinesAfter = (m.totalBytesAfter + 63) / 64;
    m.cacheLinesSaved = m.cacheLinesBefore - m.cacheLinesAfter;

    m.bandwidthReduction = (m.bytesSaved / (double)m.totalBytesBefore) * 100.0;

    return m;
}
```

**Example output**:
```
For array of 10,000 structs:

Memory footprint:
  Before: 10,000 * 24 = 240,000 bytes
  After:  10,000 * 16 = 160,000 bytes
  Savings: 80,000 bytes (33.3% reduction)

Cache line impact:
  Before: 3,750 cache lines
  After:  2,500 cache lines
  Savings: 1,250 cache lines (33.3% less memory traffic)

Estimated performance impact:
  Sequential scan cost reduction: 33.3%
  (Assuming cache-miss-dominated workload)
```

**Configuration**: Allow users to specify expected array size via CLI flag:
```bash
alchemy "src/**/*.h" -salign --array-size=10000
```

---

### 5. Alignment Boundary Report

**What it measures**: Strictest alignment requirement and verification.

**Calculation**:
```cpp
struct AlignmentMetrics {
    size_t strictestAlignment;        // Largest field alignment (e.g., 8 for double)
    bool allFieldsProperlyAligned;    // All fields meet their natural alignment

    struct FieldAlignment {
        std::string fieldName;
        size_t fieldSize;
        size_t requiredAlignment;
        size_t actualOffset;
        bool isAligned;               // actualOffset % requiredAlignment == 0
    };
    std::vector<FieldAlignment> fieldAlignments;
};

AlignmentMetrics calc(const std::vector<FieldDef>& fields) {
    AlignmentMetrics m;
    m.strictestAlignment = 0;
    m.allFieldsProperlyAligned = true;

    size_t currentOffset = 0;
    for (const auto& field : fields) {
        FieldAlignment fa;
        fa.fieldName = field.name;
        fa.fieldSize = field.size;
        fa.requiredAlignment = field.alignment;
        fa.actualOffset = currentOffset;
        fa.isAligned = (currentOffset % field.alignment) == 0;

        if (!fa.isAligned) {
            m.allFieldsProperlyAligned = false;
        }

        m.strictestAlignment = std::max(m.strictestAlignment, field.alignment);
        m.fieldAlignments.push_back(fa);

        currentOffset += field.size;
    }

    return m;
}
```

**Example output**:
```
Alignment analysis:

Strictest alignment requirement: 8 bytes (due to field 'x' of type double)
Struct alignment: 8 bytes

Field alignment status:
  ✓ b (long, 8 bytes)    @ offset 0  (8-byte aligned)
  ✓ d (int, 4 bytes)     @ offset 8  (4-byte aligned)
  ✓ a (char, 1 byte)     @ offset 12 (1-byte aligned)
  ✓ c (char, 1 byte)     @ offset 13 (1-byte aligned)

All fields properly aligned: YES
No misaligned access penalties
```

---

### 6. Padding Breakdown

**What it measures**: WHERE padding occurs and WHY.

**Calculation**:
```cpp
struct PaddingMetrics {
    struct PaddingLocation {
        std::string afterField;       // Field name (or "struct tail")
        size_t byteOffset;            // Where padding starts
        size_t paddingBytes;          // How many bytes
        std::string reason;           // "alignment for 'nextField'" or "struct tail"
    };

    std::vector<PaddingLocation> paddingLocations;
    size_t totalPaddingBytes;
    double paddingPercentage;  // (totalPadding / structSize) * 100
};

PaddingMetrics calc(const std::vector<FieldDef>& fields, size_t structSize) {
    PaddingMetrics m;
    m.totalPaddingBytes = 0;

    size_t currentOffset = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];

        // Padding before this field
        size_t alignedOffset = alignUp(currentOffset, field.alignment);
        size_t padding = alignedOffset - currentOffset;

        if (padding > 0) {
            PaddingLocation loc;
            loc.afterField = (i > 0) ? fields[i-1].name : "(start)";
            loc.byteOffset = currentOffset;
            loc.paddingBytes = padding;
            loc.reason = fmt::format("alignment for '{}'", field.name);
            m.paddingLocations.push_back(loc);
            m.totalPaddingBytes += padding;
        }

        currentOffset = alignedOffset + field.size;
    }

    // Tail padding
    size_t finalAlignment = /* strictest field alignment */;
    size_t alignedStructSize = alignUp(currentOffset, finalAlignment);
    size_t tailPadding = alignedStructSize - currentOffset;

    if (tailPadding > 0) {
        PaddingLocation loc;
        loc.afterField = fields.back().name;
        loc.byteOffset = currentOffset;
        loc.paddingBytes = tailPadding;
        loc.reason = "struct tail alignment (for arrays)";
        m.paddingLocations.push_back(loc);
        m.totalPaddingBytes += tailPadding;
    }

    m.paddingPercentage = (m.totalPaddingBytes / (double)structSize) * 100.0;

    return m;
}
```

**Example output**:
```
Padding breakdown (BEFORE optimization):

Padding locations:
  1. After field 'a' @ offset 1
     - 7 bytes of padding
     - Reason: alignment for field 'b' (requires 8-byte alignment)

  2. After field 'c' @ offset 17
     - 3 bytes of padding
     - Reason: alignment for field 'd' (requires 4-byte alignment)

  3. After field 'd' @ offset 20
     - 4 bytes of padding
     - Reason: struct tail alignment (for arrays)

Total padding: 14 bytes (58.3% of struct size)


Padding breakdown (AFTER optimization):

Padding locations:
  1. After field 'c' @ offset 14
     - 2 bytes of padding
     - Reason: struct tail alignment (for arrays)

Total padding: 2 bytes (12.5% of struct size)

Padding eliminated: 12 bytes (85.7% reduction)
```

---

## Tier 3: Expert Metrics (Lower Priority, High Complexity)

### 7. False Sharing Risk Assessment

**What it measures**: Risk of false sharing in multithreaded scenarios.

**Background**: When two threads access different fields of structs that share the same cache line, each write invalidates the other thread's cache → severe performance degradation.

**Calculation**:
```cpp
enum class FalseSharingRisk {
    LOW,      // Struct >= 128 bytes (spans multiple cache lines)
    MEDIUM,   // 64 <= struct < 128 bytes
    HIGH      // Struct < 64 bytes (multiple structs per cache line)
};

struct FalseSharingMetrics {
    FalseSharingRisk risk;
    std::string explanation;
    std::string recommendation;
};

FalseSharingMetrics calc(size_t structSize) {
    FalseSharingMetrics m;

    if (structSize >= 128) {
        m.risk = FalseSharingRisk::LOW;
        m.explanation = "Struct spans multiple cache lines - false sharing unlikely within single struct";
        m.recommendation = "No action needed for single-struct access patterns";
    } else if (structSize >= 64) {
        m.risk = FalseSharingRisk::MEDIUM;
        m.explanation = "Struct fits in 1-2 cache lines - moderate false sharing risk";
        m.recommendation = "Consider padding to 128 bytes if heavily used in multithreaded contexts";
    } else {
        m.risk = FalseSharingRisk::HIGH;
        m.explanation = fmt::format("Multiple structs per cache line ({}) - high false sharing risk", 64 / structSize);
        m.recommendation = "For per-thread data structures, consider padding to 64 bytes with alignas(64)";
    }

    return m;
}
```

**Example output**:
```
False sharing analysis:

Struct size: 24 bytes
Risk level: HIGH

Explanation:
  - Multiple structs (2-3) fit in single 64-byte cache line
  - If different threads access adjacent array elements, cache line ping-pong may occur

Recommendation:
  - For per-thread data: Add alignas(64) and pad to 64 bytes
  - For shared read-only data: Current layout is fine
  - For single-threaded code: No action needed

Example mitigation:
  struct __attribute__((aligned(64))) ThreadSafePoint {
      double x;
      double y;
      char flag;
      char padding[64 - 17];  // Pad to cache line size
  };
```

---

### 8. Prefetch Efficiency Analysis

**What it measures**: How well the struct works with hardware prefetching.

**Background**: CPUs prefetch future cache lines when detecting sequential access patterns. Struct size affects prefetch efficiency.

**Calculation**:
```cpp
struct PrefetchMetrics {
    size_t cacheLinesPerStruct;    // How many cache lines one struct spans
    std::string prefetchEfficiency;
    std::string accessPattern;
};

PrefetchMetrics calc(size_t structSize) {
    PrefetchMetrics m;
    m.cacheLinesPerStruct = (structSize + 63) / 64;

    if (m.cacheLinesPerStruct == 1) {
        m.prefetchEfficiency = "Excellent";
        m.accessPattern = "Sequential array access will prefetch efficiently";
    } else if (m.cacheLinesPerStruct <= 4) {
        m.prefetchEfficiency = "Good";
        m.accessPattern = fmt::format("Each struct spans {} cache lines - prefetcher should adapt",
                                       m.cacheLinesPerStruct);
    } else {
        m.prefetchEfficiency = "Suboptimal";
        m.accessPattern = fmt::format("Large struct ({} cache lines) - consider Struct-of-Arrays layout",
                                       m.cacheLinesPerStruct);
    }

    return m;
}
```

**Example output**:
```
Prefetch efficiency:

Struct size: 24 bytes
Cache lines per struct: 1
Prefetch efficiency: Excellent

Analysis:
  ✓ Entire struct fits in single cache line
  ✓ Sequential array access triggers efficient prefetching
  ✓ Spatial locality: accessing one field prefetches all fields

Performance characteristics:
  - Sequential iteration: ~4 cycles per struct (L1 hit after initial miss)
  - Random access: ~200 cycles per struct (L1 miss, no prefetch benefit)
```

**For large structs**:
```
Struct size: 1004 bytes
Cache lines per struct: 16
Prefetch efficiency: Suboptimal

Analysis:
  ⚠ Large struct spans 16 cache lines (1024 bytes)
  ⚠ Sequential access fetches 1024 bytes per element
  ⚠ If only accessing few fields, 95%+ of bandwidth is wasted

Recommendation:
  Consider Struct-of-Arrays (SoA) layout for hot loops:

  Instead of:
    struct Particle { double x, y; char data[1000]; };
    Particle particles[10000];
    for (auto& p : particles) process(p.x, p.y);  // Fetches data[1000] unnecessarily

  Use:
    struct Particles {
        double x[10000];
        double y[10000];
        char data[10000][1000];
    };
    for (int i = 0; i < 10000; i++) process(x[i], y[i]);  // Only fetches x, y arrays
```

---

## Implementation Design

### File Structure

```
inc/metrics/
  ├── salign_metrics.hpp          (existing - basic metrics)
  ├── cache_metrics.hpp           (NEW - cache-aware metrics)
  ├── memory_efficiency.hpp       (NEW - density, bandwidth, padding)
  └── alignment_metrics.hpp       (NEW - alignment analysis)

src/metrics/
  ├── cache_analyzer.cpp          (NEW - compute cache-related metrics)
  ├── efficiency_analyzer.cpp     (NEW - density, bandwidth calculations)
  └── alignment_analyzer.cpp      (NEW - alignment and padding analysis)

src/reporting/
  ├── metrics_reporter.cpp        (existing - needs enhancement for new metrics)
  └── cache_metrics_reporter.cpp  (NEW - formatted output for cache metrics)
```

### Data Structures

```cpp
namespace alchemy::metrics {

// Tier 1: Essential metrics
struct CacheLineMetrics {
    size_t structSize;
    size_t cacheLineSize = 64;

    double structsPerCacheLine;
    double cacheLineUtilization;
    size_t wastedBytesPerLine;
};

struct MemoryDensityMetrics {
    size_t totalFieldBytes;
    size_t totalStructBytes;
    size_t paddingBytes;
    double densityPercentage;
};

struct ArrayAllocationMetrics {
    size_t optimalArrayMultiple;
    size_t recommendedMinSize;
};

// Tier 2: Advanced metrics
struct BandwidthMetrics {
    size_t arraySize;
    size_t totalBytesBefore;
    size_t totalBytesAfter;
    size_t bytesSaved;
    size_t cacheLinesBefore;
    size_t cacheLinesAfter;
    size_t cacheLinesSaved;
    double bandwidthReduction;
};

struct AlignmentMetrics {
    size_t strictestAlignment;
    bool allFieldsProperlyAligned;

    struct FieldAlignment {
        std::string fieldName;
        size_t fieldSize;
        size_t requiredAlignment;
        size_t actualOffset;
        bool isAligned;
    };
    std::vector<FieldAlignment> fieldAlignments;
};

struct PaddingMetrics {
    struct PaddingLocation {
        std::string afterField;
        size_t byteOffset;
        size_t paddingBytes;
        std::string reason;
    };

    std::vector<PaddingLocation> paddingLocations;
    size_t totalPaddingBytes;
    double paddingPercentage;
};

// Tier 3: Expert metrics
enum class FalseSharingRisk { LOW, MEDIUM, HIGH };

struct FalseSharingMetrics {
    FalseSharingRisk risk;
    std::string explanation;
    std::string recommendation;
};

struct PrefetchMetrics {
    size_t cacheLinesPerStruct;
    std::string prefetchEfficiency;
    std::string accessPattern;
};

// Aggregate metrics container
struct EnhancedSAlignMetrics {
    // Existing metrics
    std::filesystem::path sourceFile;
    std::string structName;
    size_t originalSize;
    size_t optimizedSize;
    size_t bytesSaved;
    double percentageReduction;

    // Tier 1 (always computed)
    CacheLineMetrics cacheLineBefore;
    CacheLineMetrics cacheLineAfter;
    MemoryDensityMetrics densityBefore;
    MemoryDensityMetrics densityAfter;
    ArrayAllocationMetrics arrayMetrics;

    // Tier 2 (optional, based on flags)
    std::optional<BandwidthMetrics> bandwidth;
    std::optional<AlignmentMetrics> alignment;
    std::optional<PaddingMetrics> paddingBefore;
    std::optional<PaddingMetrics> paddingAfter;

    // Tier 3 (opt-in via flag)
    std::optional<FalseSharingMetrics> falseSharingBefore;
    std::optional<FalseSharingMetrics> falseSharingAfter;
    std::optional<PrefetchMetrics> prefetchBefore;
    std::optional<PrefetchMetrics> prefetchAfter;
};

}  // namespace alchemy::metrics
```

### CLI Flags for Metric Control

```bash
# Tier 1 metrics (always shown)
alchemy "src/**/*.h" -salign

# Tier 2 metrics (with array size for bandwidth calculation)
alchemy "src/**/*.h" -salign --array-size=10000

# Tier 2 metrics (with detailed padding breakdown)
alchemy "src/**/*.h" -salign --show-padding

# Tier 3 metrics (expert mode)
alchemy "src/**/*.h" -salign --expert-metrics

# Custom cache line size (for non-x86 architectures)
alchemy "src/**/*.h" -salign --cache-line-size=128  # ARM Neoverse, some RISC-V

# Verbose output (all tiers)
alchemy "src/**/*.h" -salign --verbose
```

---

## Example Output

### Tier 1 Output (Default)

```
alchemy::salign::struct 'Particle'

Space optimization:
  Before: 24 bytes
  After:  16 bytes
  Savings: 8 bytes (33.3% reduction)

Cache line efficiency:
  Before: 2 structs/line (75.0% utilization, 16 bytes wasted)
  After:  4 structs/line (100.0% utilization, 0 bytes wasted)
  Improvement: +25% cache line utilization

Memory density:
  Before: 58.3% (14 bytes data, 10 bytes padding)
  After:  87.5% (14 bytes data, 2 bytes padding)
  Improvement: +29.2% density

Array allocation tip:
  For optimal cache alignment, allocate arrays in multiples of 4 structs (64 bytes)
```

### Tier 2 Output (With --array-size=10000)

```
alchemy::salign::struct 'Particle'

[... Tier 1 output ...]

Estimated bandwidth savings (array of 10,000):
  Memory footprint:
    Before: 240,000 bytes
    After:  160,000 bytes
    Savings: 80,000 bytes (33.3% reduction)

  Cache line impact:
    Before: 3,750 cache lines
    After:  2,500 cache lines
    Savings: 1,250 cache lines (33.3% less memory traffic)

  Performance impact:
    Sequential scan cost: -33.3% (cache-miss-dominated workloads)

Alignment analysis:
  Strictest requirement: 8 bytes (field 'y' of type double)
  All fields properly aligned: YES
  No misaligned access penalties
```

### Tier 2 Output (With --show-padding)

```
alchemy::salign::struct 'Particle'

[... Tier 1 output ...]

Padding breakdown (BEFORE):
  1. After 'active' @ offset 1:  7 bytes (alignment for 'x')
  2. After 'type' @ offset 17:   3 bytes (alignment for 'id')
  3. After 'id' @ offset 20:     4 bytes (struct tail alignment)
  Total: 14 bytes (58.3% of struct)

Padding breakdown (AFTER):
  1. After 'type' @ offset 22:   2 bytes (struct tail alignment)
  Total: 2 bytes (12.5% of struct)

Padding eliminated: 12 bytes (85.7% reduction)
```

### Tier 3 Output (With --expert-metrics)

```
alchemy::salign::struct 'Particle'

[... Tier 1 & 2 output ...]

False sharing risk:
  Before: HIGH
    - 2-3 structs per cache line
    - If threads access adjacent array elements, cache line contention may occur
    - Recommendation: For per-thread data, consider alignas(64) and padding

  After: HIGH (improved but still small)
    - 4 structs per cache line
    - Risk remains for multithreaded array access
    - Recommendation: Same as before

Prefetch efficiency:
  Before: Excellent (1 cache line per struct)
  After:  Excellent (1 cache line per struct)

  Analysis:
    ✓ Sequential array access will prefetch efficiently
    ✓ Entire struct fits in single cache line
    ✓ Spatial locality benefits all field accesses
```

---

## Testing Strategy

### Unit Tests

```cpp
// tests/unit/test_cache_metrics.cpp
TEST(CacheMetrics, CalculatesStructsPerCacheLine) {
    // 16-byte struct: 64 / 16 = 4 structs per line
    auto metrics = calcCacheLineMetrics(16);
    ASSERT_EQ(metrics.structsPerCacheLine, 4);
    ASSERT_EQ(metrics.cacheLineUtilization, 100.0);
    ASSERT_EQ(metrics.wastedBytesPerLine, 0);
}

TEST(CacheMetrics, HandlesNonPowerOfTwo) {
    // 24-byte struct: 64 / 24 = 2.67 → 2 structs per line
    auto metrics = calcCacheLineMetrics(24);
    ASSERT_EQ(metrics.structsPerCacheLine, 2);
    ASSERT_EQ(metrics.cacheLineUtilization, 75.0);  // 48 / 64
    ASSERT_EQ(metrics.wastedBytesPerLine, 16);
}

TEST(MemoryDensity, CalculatesPaddingCorrectly) {
    std::vector<FieldDef> fields = {
        {"a", 1, 1},  // char
        {"b", 8, 8},  // long
    };
    auto metrics = calcMemoryDensity(fields, 16);
    ASSERT_EQ(metrics.totalFieldBytes, 9);
    ASSERT_EQ(metrics.paddingBytes, 7);
    ASSERT_DOUBLE_EQ(metrics.densityPercentage, 56.25);
}

TEST(ArrayMetrics, CalculatesOptimalMultiple) {
    // 24 bytes: LCM(24, 64) = 192 → 192/24 = 8
    ASSERT_EQ(calcOptimalMultiple(24), 8);

    // 16 bytes: LCM(16, 64) = 64 → 64/16 = 4
    ASSERT_EQ(calcOptimalMultiple(16), 4);

    // 64 bytes: LCM(64, 64) = 64 → 64/64 = 1
    ASSERT_EQ(calcOptimalMultiple(64), 1);
}

TEST(BandwidthMetrics, CalculatesSavingsForArray) {
    auto metrics = calcBandwidthMetrics(10000, 24, 16);
    ASSERT_EQ(metrics.totalBytesBefore, 240000);
    ASSERT_EQ(metrics.totalBytesAfter, 160000);
    ASSERT_EQ(metrics.bytesSaved, 80000);
    ASSERT_EQ(metrics.cacheLinesBefore, 3750);
    ASSERT_EQ(metrics.cacheLinesAfter, 2500);
    ASSERT_DOUBLE_EQ(metrics.bandwidthReduction, 33.333, 0.001);
}
```

### Integration Tests

```cpp
// tests/integration/test_enhanced_salign_metrics.cpp
TEST_F(SAlignIntegrationTest, ReportsEnhancedMetricsForOptimization) {
    // Given: struct with suboptimal layout
    writeFile("test.h", R"(
        struct Test {
            char a;
            long b;
            char c;
            int d;
        };
    )");

    // When: run salign with enhanced metrics
    auto result = runAlchemy({"test.h", "-salign", "--array-size=1000"});

    // Then: metrics include cache and bandwidth analysis
    ASSERT_TRUE(result.metrics.cacheLineBefore.has_value());
    ASSERT_TRUE(result.metrics.bandwidth.has_value());
    ASSERT_EQ(result.metrics.cacheLineBefore->structsPerCacheLine, 2);
    ASSERT_EQ(result.metrics.cacheLineAfter->structsPerCacheLine, 4);
}
```

---

## Performance Considerations

### Computation Cost

All proposed metrics can be computed in O(n) time where n = number of fields:

- **Cache line metrics**: O(1) - simple division
- **Memory density**: O(n) - sum field sizes
- **Optimal array multiple**: O(1) - LCM computation
- **Bandwidth metrics**: O(1) - arithmetic
- **Alignment metrics**: O(n) - iterate fields
- **Padding breakdown**: O(n) - iterate fields
- **False sharing**: O(1) - threshold check
- **Prefetch**: O(1) - simple calculation

**Total overhead**: Negligible (microseconds per struct)

### Memory Overhead

- **Tier 1 metrics**: ~200 bytes per struct
- **Tier 2 metrics**: ~500 bytes per struct (includes vectors)
- **Tier 3 metrics**: ~300 bytes per struct

For 1000 structs: ~1 MB total (acceptable)

---

## Phased Implementation Plan

### Phase 1: Foundation (Week 1)
- [ ] Define data structures (`cache_metrics.hpp`, `memory_efficiency.hpp`)
- [ ] Implement Tier 1 calculators (cache line, density, array multiple)
- [ ] Add unit tests for calculations
- [ ] Update `SAlignMetrics` to include Tier 1 fields

### Phase 2: Reporting (Week 2)
- [ ] Create `CacheMetricsReporter` for formatted output
- [ ] Integrate Tier 1 metrics into existing reporter
- [ ] Add CLI flag: `--array-size=N`
- [ ] Integration tests for end-to-end flow

### Phase 3: Advanced Metrics (Week 3)
- [ ] Implement Tier 2 calculators (bandwidth, alignment, padding)
- [ ] Add CLI flag: `--show-padding`
- [ ] Enhanced reporter output for Tier 2
- [ ] Documentation updates

### Phase 4: Expert Features (Week 4)
- [ ] Implement Tier 3 calculators (false sharing, prefetch)
- [ ] Add CLI flag: `--expert-metrics`
- [ ] Comprehensive testing (edge cases, large structs)
- [ ] User documentation with examples

### Phase 5: Polish (Week 5)
- [ ] Performance profiling (ensure negligible overhead)
- [ ] Documentation: README examples, usage guide
- [ ] Integration with existing v2.5 pipeline
- [ ] Update design docs

---

## Success Criteria

1. **Correctness**: All metric calculations validated against manual analysis
2. **Performance**: <1ms overhead per struct (imperceptible to users)
3. **Usability**: Output is clear, actionable, educational
4. **Extensibility**: Easy to add new metrics in the future
5. **Testing**: >90% coverage on metric calculation logic

---

## Future Enhancements (Post-v2.0)

### Architecture-Specific Metrics
- Support for ARM cache line sizes (128 bytes on Neoverse)
- RISC-V cache line detection
- Platform-specific alignment rules

### Interactive Mode
- `--interactive` flag: ask user for array size, use case
- Tailored recommendations based on use case (single-threaded vs multi-threaded)

### JSON Output
- `--format=json` for machine-readable metrics
- Integration with visualization tools

### Historical Comparison
- Track metrics over time (git history integration)
- Show metric trends: "Cache utilization improved 15% over last 3 commits"

### Struct-of-Arrays (SoA) Suggestions
- Detect "hot field" access patterns (requires profiling integration)
- Suggest SoA transformation for large structs with selective field access

---

## References

- [Intel 64 and IA-32 Architectures Optimization Reference Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [What Every Programmer Should Know About Memory (Ulrich Drepper)](https://people.freebsd.org/~lstewart/articles/cpumemory.pdf)
- [CPU Caches and Why You Care (Scott Meyers)](https://www.aristeia.com/TalkNotes/codedive-CPUCachesHandouts.pdf)
- [Understanding the Impact of Cache Line Size](https://stackoverflow.com/questions/3928995/how-do-cache-lines-work)

---

**Next Steps**: Review and approve this design, then proceed with Phase 1 implementation.
