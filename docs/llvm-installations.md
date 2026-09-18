# LLVM Version Management

This document provides instructions for managing multiple LLVM/Clang installations on your system, including how to cleanly install, switch between, and uninstall specific versions.

## Current State

Check what LLVM versions are installed:

```bash
# List all LLVM installations
ls -la /usr/lib | grep llvm

# Check active clang version
clang --version

# Check all installed llvm/clang packages
dpkg -l | grep -E "llvm|clang" | grep "^ii"

# Find LLVM CMake configs
find /usr/lib -name "LLVMConfig.cmake" 2>/dev/null
find /usr/lib -name "ClangConfig.cmake" 2>/dev/null
```

## Installing LLVM 17 Alongside LLVM 14

### Step 1: Check if LLVM 17 is available in your Ubuntu repos

```bash
apt-cache search llvm-17 | grep -E "^llvm-17|^clang-17"
```

### Step 2: Install LLVM 17 packages (if available)

```bash
sudo apt update
sudo apt install -y \
  llvm-17 \
  llvm-17-dev \
  llvm-17-runtime \
  clang-17 \
  libclang-17-dev \
  clang-tools-17
```

### Step 3: Verify installation

```bash
# Check both versions exist
ls -la /usr/lib | grep llvm

# Should show both:
# drwxr-xr-x  8 root root  4096 ... llvm-14
# drwxr-xr-x  8 root root  4096 ... llvm-17
```

## Clean Uninstall of LLVM 17

### Step 1: List all LLVM 17 packages

```bash
dpkg -l | grep -E "(llvm-17|clang-17)" | awk '{print $2}'
```

### Step 2: Remove LLVM 17 packages

```bash
# Remove main packages
sudo apt remove --purge -y \
  llvm-17 \
  llvm-17-dev \
  llvm-17-runtime \
  llvm-17-tools \
  clang-17 \
  libclang-17-dev \
  clang-tools-17 \
  libclang-common-17-dev \
  libllvm17

# Remove any remaining dependencies
sudo apt autoremove -y
```

### Step 3: Verify complete removal

```bash
# Should NOT show llvm-17 or clang-17
dpkg -l | grep -E "(llvm-17|clang-17)"

# Should NOT show /usr/lib/llvm-17
ls -la /usr/lib | grep llvm-17

# Should NOT find any LLVM 17 CMake configs
find /usr/lib -name "*17*" -path "*/cmake/*" 2>/dev/null
```

### Step 4: Clean up any orphaned files (if needed)

```bash
# Check for orphaned config files
ls -la /usr/lib/cmake/ | grep -E "llvm-17|clang-17"

# Remove if found
sudo rm -rf /usr/lib/cmake/llvm-17
sudo rm -rf /usr/lib/cmake/clang-17

# Check for broken symlinks
find /usr/lib -xtype l -ls 2>/dev/null | grep llvm

# Remove broken symlinks (adjust paths as needed)
# sudo rm /path/to/broken/symlink
```

### Step 5: Fix package database (if necessary)

```bash
# Fix any broken dependencies
sudo apt --fix-broken install

# Update package cache
sudo apt update
```

## Reverting to LLVM 14 Only

If LLVM 17 installation causes issues, follow the uninstall steps above, then verify LLVM 14 still works:

```bash
# Should show LLVM 14
clang --version

# Should show llvm-14 directory
ls -la /usr/lib/llvm-14

# Should find LLVM 14 CMake configs
find /usr/lib/llvm-14 -name "LLVMConfig.cmake"
find /usr/lib/llvm-14 -name "ClangConfig.cmake"
```

### Test Alchemy Build with LLVM 14

```bash
cd /home/fractals/dev/sandbox/cpp/alchemy
make clean
make alchemy.debug
```

If the build fails with LLVM 14, you may need to reinstall it:

```bash
sudo apt install --reinstall -y \
  llvm-14 \
  llvm-14-dev \
  clang-14 \
  libclang-14-dev
```

## Switching Default Clang Version

Ubuntu uses the `update-alternatives` system to manage multiple versions:

### Check current alternatives

```bash
update-alternatives --query clang
update-alternatives --query clang++
```

### Set LLVM 17 as default (after installation)

```bash
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-17 100
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-17 100

# Verify
clang --version  # Should show 17.x.x
```

### Revert to LLVM 14 as default

```bash
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-14 50
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-14 50

# Verify
clang --version  # Should show 14.x.x
```

### Interactive selection

```bash
# Choose interactively
sudo update-alternatives --config clang
sudo update-alternatives --config clang++
```

## Alchemy-Specific: Force a Specific LLVM Version

Even with multiple LLVM installations, you can force Alchemy to use a specific version:

### Option 1: Environment variable

```bash
export LLVM_DIR=/usr/lib/llvm-14/lib/cmake/llvm
export Clang_DIR=/usr/lib/llvm-14/lib/cmake/clang
make alchemy.debug
```

### Option 2: Direct CMake argument (via presets)

```bash
cmake --preset debug \
  -DLLVM_DIR=/usr/lib/llvm-14/lib/cmake/llvm \
  -DClang_DIR=/usr/lib/llvm-14/lib/cmake/clang
cmake --build build/debug
```

### Option 3: Makefile passthrough

```bash
LLVM_DIR=/usr/lib/llvm-14/lib/cmake/llvm \
Clang_DIR=/usr/lib/llvm-14/lib/cmake/clang \
make alchemy.debug
```

## Expected Behavior with Multiple Versions

Alchemy's CMake toolchain (`cmake/toolchains/clang.cmake`) auto-detects LLVM installations and **selects the latest version** by default.

**Example:** If you have both LLVM 14 and LLVM 17 installed:
- Toolchain searches: `/usr/lib/llvm-*`, `/usr/local/lib/llvm-*`, `/opt/llvm-*`
- Finds: `llvm-14`, `llvm-17`
- Sorts naturally (descending): `llvm-17` comes first
- Selects: **LLVM 17**

You should see during CMake configure:
```
-- alchemy::auto-detected LLVM in: /usr/lib/llvm-17
-- alchemy::found LLVM 17.x.x
```

If you want to override this, use one of the options above to force LLVM 14.

## Troubleshooting: Version Mismatch Errors

### Error: "Could not find a configuration file for package LLVM that exactly matches requested version X"

**Cause:** Orphaned Clang config files from a previous installation

**Fix:**
1. Find orphaned configs:
   ```bash
   find /usr/lib -name "ClangConfig.cmake" 2>/dev/null
   ```

2. Remove configs for uninstalled versions:
   ```bash
   sudo rm -rf /usr/lib/cmake/clang-XX  # Replace XX with orphaned version
   ```

3. Clean build and retry:
   ```bash
   make clean
   make alchemy.debug
   ```

### Error: Linker errors or missing symbols

**Cause:** Mixing LLVM/Clang from different versions

**Fix:** Explicitly set matching LLVM and Clang dirs:
```bash
export LLVM_DIR=/usr/lib/llvm-14/lib/cmake/llvm
export Clang_DIR=/usr/lib/llvm-14/lib/cmake/clang
make clean
make alchemy.debug
```

### Error: "libclang.so: undefined symbol"

**Cause:** stdlib ABI mismatch (libc++ vs libstdc++)

**Fix:** The toolchain auto-detects this, but if issues persist:
1. Check what stdlib LLVM uses:
   ```bash
   llvm-config-14 --cxxflags | grep stdlib
   ```

2. Force clean rebuild:
   ```bash
   make clean
   rm -rf build .cache
   make alchemy.debug
   ```

## Summary: Safe Testing Workflow

When testing with multiple LLVM versions:

1. **Before installation:** Document current state
   ```bash
   dpkg -l | grep -E "(llvm|clang)" > ~/llvm-before.txt
   ```

2. **Install new version:** Use `apt install` (not `apt upgrade`)

3. **Test build:** Try building Alchemy

4. **If issues:** Cleanly uninstall following steps above

5. **Verify restoration:** Compare packages
   ```bash
   dpkg -l | grep -E "(llvm|clang)" > ~/llvm-after.txt
   diff ~/llvm-before.txt ~/llvm-after.txt
   ```

---

**Last Updated:** 2025-10-23
**Tested on:** Ubuntu 22.04 LTS with LLVM 14
