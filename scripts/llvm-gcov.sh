#!/bin/bash
# wrapper script for llvm-cov to provide gcov-compatible interface
# used by lcov for coverage analysis when building with clang
exec llvm-cov gcov "$@"
