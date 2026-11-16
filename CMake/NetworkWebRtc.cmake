# WebRTC data channels for native peers. The library needs four more sources that its repository pulls
# in as git submodules; each is fetched as its own pinned archive into the folder the library expects,
# and the crypto backend is Mbed TLS built from source.
set(_kb_webrtc_nobuild kb_no_cmake)
FetchContent_Declare(kb_libdatachannel
    URL https://codeload.github.com/paullouisageneau/libdatachannel/tar.gz/6b1e2e620f1e37f0eafeee702eaea0043cb305fd
    URL_HASH SHA256=e15e6426a6ac601cbfa97708e0c8089bec4cb262f622dd1f76b8350dabd5a0bd
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR ${_kb_webrtc_nobuild}
)
FetchContent_MakeAvailable(kb_libdatachannel)

FetchContent_Declare(kb_libjuice
    URL https://codeload.github.com/paullouisageneau/libjuice/tar.gz/b89c792e3612faf2f12cf35bcc56857313a06be3
    URL_HASH SHA256=713b22174e8cb2e06538fa9bac2650bda21b83c86f5128813ab4dcf8f5bea9ed
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR ${kb_libdatachannel_SOURCE_DIR}/deps/libjuice
    SOURCE_SUBDIR ${_kb_webrtc_nobuild}
)
FetchContent_Declare(kb_usrsctp
    URL https://codeload.github.com/paullouisageneau/usrsctp/tar.gz/fec583d54493f879d2ae44a743423bf8a04371ab
    URL_HASH SHA256=e5c114afe73c9a0ec419fab5f5b3f63f3ce57b09b90d06ea85e63dca8aed3e7c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR ${kb_libdatachannel_SOURCE_DIR}/deps/usrsctp
    SOURCE_SUBDIR ${_kb_webrtc_nobuild}
)
FetchContent_Declare(kb_plog
    URL https://codeload.github.com/SergiusTheBest/plog/tar.gz/94899e0b926ac1b0f4750bfbd495167b4a6ae9ef
    URL_HASH SHA256=92a08bce559b5f28aa88d3fd9071567414b9f43a83fe2a05a6dd14f1da536072
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR ${kb_libdatachannel_SOURCE_DIR}/deps/plog
    SOURCE_SUBDIR ${_kb_webrtc_nobuild}
)
FetchContent_Declare(kb_mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(kb_libjuice kb_usrsctp kb_plog)

# The libraries are linked statically into kb_net_webrtc; nothing of theirs is installed or exported.
set(_kb_webrtc_skip_install ${CMAKE_SKIP_INSTALL_RULES})
set(CMAKE_SKIP_INSTALL_RULES ON)

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_USER_CONFIG_FILE ${CMAKE_SOURCE_DIR}/CMake/WebRtcMbedTlsConfig.h CACHE FILEPATH "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(kb_mbedtls)
add_library(kb_mbedtls_all INTERFACE)
target_link_libraries(kb_mbedtls_all INTERFACE mbedtls mbedx509 mbedcrypto)
add_library(MbedTLS::MbedTLS ALIAS kb_mbedtls_all)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(${kb_libdatachannel_SOURCE_DIR} kb_libdatachannel-build EXCLUDE_FROM_ALL)
set(CMAKE_SKIP_INSTALL_RULES ${_kb_webrtc_skip_install})
