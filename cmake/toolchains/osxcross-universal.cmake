# Cross-compiling a macOS universal (x86_64 + arm64) binary from Linux with
# osxcross. One pass: CMake passes "-arch x86_64 -arch arm64" itself from
# CMAKE_OSX_ARCHITECTURES, the wrapper clang accepts both.
#
# Deployment target 10.15 for x86_64 (X-Plane 12 official floor); the arm64
# slice is implicitly 11.0 since that is where arm64 macOS starts.

set(OSXCROSS /usr/local/osxcross)
set(OSXCROSS_HOST x86_64-apple-darwin20.4)

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   ${OSXCROSS}/bin/o64-clang)
set(CMAKE_CXX_COMPILER ${OSXCROSS}/bin/o64-clang++)
set(CMAKE_AR                ${OSXCROSS}/bin/${OSXCROSS_HOST}-ar                CACHE FILEPATH "ar")
set(CMAKE_RANLIB            ${OSXCROSS}/bin/${OSXCROSS_HOST}-ranlib            CACHE FILEPATH "ranlib")
set(CMAKE_STRIP             ${OSXCROSS}/bin/${OSXCROSS_HOST}-strip             CACHE FILEPATH "strip")
set(CMAKE_INSTALL_NAME_TOOL ${OSXCROSS}/bin/${OSXCROSS_HOST}-install_name_tool CACHE FILEPATH "install_name_tool")

set(CMAKE_OSX_SYSROOT ${OSXCROSS}/SDK/MacOSX11.3.sdk)
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "archs")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "min macOS")

# Strip local symbols at link time: ld64 ad-hoc signs arm64 output after
# linking, and a later "strip" would invalidate that signature.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-x -Wl,-dead_strip")

set(CMAKE_FIND_ROOT_PATH ${CMAKE_OSX_SYSROOT} ${SKU_DEPS_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
