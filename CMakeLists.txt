set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CMake Conan requires this to be set or passed in via the command line.
# Pass -DGTPROXY_USE_CONAN=OFF to resolve dependencies from CMAKE_PREFIX_PATH
# instead (e.g. when the Conan remote is unreachable).
option(GTPROXY_USE_CONAN "Fetch dependencies with Conan" ON)
if (GTPROXY_USE_CONAN)
    set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake-conan/conan_provider.cmake)
endif ()

cmake_minimum_required(VERSION 3.24)

# Statically link the MSVC runtime (/MT) so the exe needs no Visual C++
# redistributable (MSVCP140.dll / VCRUNTIME140.dll) on the machines it runs on.
# Set unconditionally and BEFORE project(): the MSVC variable is not defined
# until project() detects the compiler, so an if(MSVC) guard here would skip
# this and leave the build on the dynamic runtime. Non-MSVC toolchains ignore
# the variable. cmake-conan reads it and builds every Conan dependency with
# compiler.runtime=static to match, so the whole exe is self-contained.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(ScarfaceProxy VERSION 3.0.0)

add_subdirectory(lib)
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
