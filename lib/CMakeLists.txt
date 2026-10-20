project(GTProxy-Library)

# Keep the exe self-contained: stop cpp-httplib from discovering and linking the
# build machine's *system* OpenSSL/zlib, which made the exe depend on
# libssl-3-x64.dll / libcrypto-3-x64.dll at runtime. TLS comes from the
# statically linked Conan LibreSSL that src/ already links (together with the
# CPPHTTPLIB_OPENSSL_SUPPORT define), so httplib needs no library of its own.
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)

# MinGW's headers lack GetAddrInfoExCancel, which the non-blocking resolver
# needs; the MSVC build keeps it.
if (MINGW)
    set(HTTPLIB_USE_NON_BLOCKING_GETADDRINFO OFF CACHE BOOL "" FORCE)
endif ()

add_subdirectory(cpp-httplib)
add_subdirectory(enet)
