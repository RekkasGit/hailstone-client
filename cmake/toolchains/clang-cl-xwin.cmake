# Windows x64 from Linux (or macOS): clang-cl and lld-link against the MSVC C runtime and the Windows SDK that
# xwin (https://github.com/Jake-Shadle/xwin) unpacks. docs/ci.md "Windows builds from Linux" has the steps.
#
#   cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/clang-cl-xwin.cmake
#
# The SDK is looked for in $NMSC_XWIN_DIR, else ~/.xwin; clang-cl-xwin.sh reads the same variable while building.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(DEFINED ENV{NMSC_XWIN_DIR})
  set(NMSC_XWIN_DIR "$ENV{NMSC_XWIN_DIR}")
else()
  set(NMSC_XWIN_DIR "$ENV{HOME}/.xwin")
endif()
if(NOT EXISTS "${NMSC_XWIN_DIR}/crt/include" OR NOT EXISTS "${NMSC_XWIN_DIR}/sdk/include/um")
  message(FATAL_ERROR "No MSVC CRT and Windows SDK in ${NMSC_XWIN_DIR}: run xwin --accept-license --arch x86_64 "
                      "splat --output ${NMSC_XWIN_DIR} (or set NMSC_XWIN_DIR)")
endif()
foreach(tool clang-cl lld-link llvm-lib llvm-rc llvm-mt)
  find_program(NMSC_XWIN_${tool} ${tool})
  if(NOT NMSC_XWIN_${tool})
    message(FATAL_ERROR "${tool} not found: the Windows cross build needs LLVM's clang-cl, lld-link, llvm-lib, "
                        "llvm-rc and llvm-mt")
  endif()
endforeach()

# The wrapper gives every compile the target and the SDK's headers: CEF's CMake files (tools/hailstone-web) set
# CMAKE_CXX_FLAGS themselves, so flags given here would not reach them.
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/clang-cl-xwin.sh")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/clang-cl-xwin.sh")
set(CMAKE_LINKER "${NMSC_XWIN_lld-link}")
set(CMAKE_AR "${NMSC_XWIN_llvm-lib}")
set(CMAKE_RC_COMPILER "${NMSC_XWIN_llvm-rc}")
set(CMAKE_MT "${NMSC_XWIN_llvm-mt}")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(CMAKE_RC_FLAGS_INIT "-I${NMSC_XWIN_DIR}/sdk/include/um -I${NMSC_XWIN_DIR}/sdk/include/shared \
-I${NMSC_XWIN_DIR}/crt/include -I${NMSC_XWIN_DIR}/sdk/include/ucrt")
set(nmsc_xwin_libs "/libpath:${NMSC_XWIN_DIR}/crt/lib/x86_64 /libpath:${NMSC_XWIN_DIR}/sdk/lib/um/x86_64 \
/libpath:${NMSC_XWIN_DIR}/sdk/lib/ucrt/x86_64")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${nmsc_xwin_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${nmsc_xwin_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${nmsc_xwin_libs}")

# Nothing from the build machine: cmake/Dependencies.cmake takes a system package when one is found, and a Linux
# SDL3, glm or zlib would be picked up otherwise. Programs (glslang for the shaders) are the build machine's.
set(CMAKE_FIND_ROOT_PATH "${NMSC_XWIN_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)
