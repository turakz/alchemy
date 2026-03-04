# cmake/toolchains/clang.cmake
# Simplified Clang/LLVM toolchain for Alchemy

# --------------------------------------------------------------------------
# 1) User-configurable LLVM version
# --------------------------------------------------------------------------
# Set ALCHEMY_LLVM_VERSION to override auto-detection (e.g., -DALCHEMY_LLVM_VERSION=18)
if(NOT DEFINED ALCHEMY_LLVM_VERSION AND DEFINED ENV{ALCHEMY_LLVM_VERSION})
    set(ALCHEMY_LLVM_VERSION $ENV{ALCHEMY_LLVM_VERSION})
endif()

# --------------------------------------------------------------------------
# 2) Set C/C++ compilers
# --------------------------------------------------------------------------
if(DEFINED ALCHEMY_LLVM_VERSION)
    set(CMAKE_C_COMPILER clang-${ALCHEMY_LLVM_VERSION})
    set(CMAKE_CXX_COMPILER clang++-${ALCHEMY_LLVM_VERSION})
    message(STATUS "alchemy::using user-specified LLVM version ${ALCHEMY_LLVM_VERSION}")
else()
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
endif()

# --------------------------------------------------------------------------
# 3) Resolve LLVM_DIR and Clang_DIR
# --------------------------------------------------------------------------
# Priority: user-provided env or CMake variable > llvm-config > fallback directories
if(NOT DEFINED LLVM_DIR AND DEFINED ENV{LLVM_DIR})
    set(LLVM_DIR $ENV{LLVM_DIR})
endif()
if(NOT DEFINED Clang_DIR AND DEFINED ENV{Clang_DIR})
    set(Clang_DIR $ENV{Clang_DIR})
endif()

# Attempt to use llvm-config if LLVM_DIR not set
if(NOT DEFINED LLVM_DIR)
    # Build list of llvm-config names to search
    if(DEFINED ALCHEMY_LLVM_VERSION)
        # User specified version - only look for that one
        set(_llvm_config_names llvm-config-${ALCHEMY_LLVM_VERSION})
    else()
        # Auto-detect: prefer highest version (search from highest to lowest)
        set(_llvm_config_names llvm-config-18 llvm-config-17 llvm-config-16 llvm-config-15 llvm-config)
    endif()

    find_program(_LLVM_CONFIG
        NAMES ${_llvm_config_names}
        DOC "LLVM config utility"
    )
    if(_LLVM_CONFIG)
        execute_process(
            COMMAND "${_LLVM_CONFIG}" --cmakedir
            OUTPUT_VARIABLE _llvm_cmake_dir
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(EXISTS "${_llvm_cmake_dir}")
            set(LLVM_DIR "${_llvm_cmake_dir}" CACHE PATH "LLVM_DIR from llvm-config")
            message(STATUS "alchemy::found LLVM via llvm-config: ${LLVM_DIR}")
            # Clang_DIR is a sibling of LLVM_DIR under lib/cmake/
            get_filename_component(_cmake_parent "${_llvm_cmake_dir}" DIRECTORY)
            if(EXISTS "${_cmake_parent}/clang")
                set(Clang_DIR "${_cmake_parent}/clang" CACHE PATH "Clang_DIR from llvm-config")
            endif()
            set(ALCHEMY_LLVM_AUTODETECTED ON CACHE BOOL "LLVM auto-detected")
        endif()
    endif()
endif()

# Optional fallback: scan standard directories (only if still undefined)
if(NOT DEFINED LLVM_DIR)
    if(DEFINED ALCHEMY_LLVM_VERSION)
        # User specified version - only look for that one
        set(_fallback_paths
            "/usr/lib/llvm-${ALCHEMY_LLVM_VERSION}"
            "/usr/local/lib/llvm-${ALCHEMY_LLVM_VERSION}"
            "/opt/llvm-${ALCHEMY_LLVM_VERSION}"
        )
    else()
        # Auto-detect: scan for any version
        set(_fallback_paths
            "/usr/lib/llvm-*"
            "/usr/local/lib/llvm-*"
            "/opt/llvm-*"
        )
    endif()

    foreach(p ${_fallback_paths})
        file(GLOB _candidates "${p}")
        if(_candidates)
            list(SORT _candidates COMPARE NATURAL ORDER DESCENDING)
            list(GET _candidates 0 _candidate)
            if(EXISTS "${_candidate}/lib/cmake/llvm")
                set(LLVM_DIR "${_candidate}/lib/cmake/llvm" CACHE PATH "LLVM_DIR fallback")
                set(Clang_DIR "${_candidate}/lib/cmake/clang" CACHE PATH "Clang_DIR fallback")
                set(ALCHEMY_LLVM_AUTODETECTED ON CACHE BOOL "LLVM auto-detected fallback")
                message(STATUS "alchemy::fallback detected LLVM in: ${_candidate}")
                break()
            endif()
        endif()
    endforeach()
endif()

# --------------------------------------------------------------------------
# 4) Find llvm-config binary matching LLVM_DIR
# --------------------------------------------------------------------------
set(_LLVM_CONFIG "")
if(DEFINED LLVM_DIR)
    get_filename_component(_tmp "${LLVM_DIR}" DIRECTORY)
    get_filename_component(_llvm_root "${_tmp}" DIRECTORY)
    set(_try_config "${_llvm_root}/bin/llvm-config")
    if(EXISTS "${_try_config}")
        set(_LLVM_CONFIG "${_try_config}")
    endif()
    set(_ALCHEMY_LLVM_ROOT "${_llvm_root}" CACHE PATH "Resolved LLVM root")
endif()
if(NOT _LLVM_CONFIG)
    find_program(_LLVM_CONFIG llvm-config)
endif()

# --------------------------------------------------------------------------
# 5) Detect stdlib and append LLVM compile flags
# --------------------------------------------------------------------------
set(_prefer_libcxx FALSE)
if(_LLVM_CONFIG)
    message(STATUS "alchemy::using llvm-config: ${_LLVM_CONFIG}")
    execute_process(
        COMMAND "${_LLVM_CONFIG}" --cxxflags
        OUTPUT_VARIABLE _llvm_cxxflags
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(_llvm_cxxflags MATCHES "-stdlib=libc\\+\\+")
        set(_prefer_libcxx TRUE)
        message(STATUS "alchemy::llvm-config suggests libc++")
    endif()

    # Append LLVM compile flags
    if(_llvm_cxxflags)
        string(REPLACE "\"" "" _llvm_cxxflags_clean "${_llvm_cxxflags}")
        separate_arguments(_llvm_cxxflags_list UNIX_COMMAND "${_llvm_cxxflags_clean}")
        add_compile_options(${_llvm_cxxflags_list})
    endif()
endif()

# --------------------------------------------------------------------------
# 6) Apply stdlib flags if needed
# --------------------------------------------------------------------------
if(_prefer_libcxx)
    message(STATUS "alchemy::forcing -stdlib=libc++ for compile and link")
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
    add_link_options(-lc++abi)
else()
    message(STATUS "alchemy::using platform default C++ stdlib")
endif()

# --------------------------------------------------------------------------
# 7) Mark toolchain loaded
# --------------------------------------------------------------------------
set(ALCHEMY_TOOLCHAIN_LOADED TRUE CACHE INTERNAL "Alchemy toolchain loaded")
