# SPDX-License-Identifier: GPL-3.0-only
# Cross-compiling for the CardputerZero (Raspberry Pi CM0, 64-bit Debian).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_STRIP aarch64-linux-gnu-strip)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Tuned for the Cortex-A53 in the CM0; the binaries still run on any ARMv8-A.
set(CMAKE_C_FLAGS_INIT "-mtune=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "-mtune=cortex-a53")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s")
