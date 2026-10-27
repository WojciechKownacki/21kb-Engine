# kb_http: an HTTP and HTTPS client for gameplay (web APIs, services). It has no dependency on the engine.
# Windows uses WinHTTP; other platforms answer Status::Unsupported, and no TLS library is bundled.
add_library(kb_http STATIC
    include/kb/http/HttpClient.hpp
    src/Backend.hpp
    src/HttpClient.cpp
)

if(WIN32)
    target_sources(kb_http PRIVATE src/HttpWinHttp.cpp)
    target_link_libraries(kb_http PUBLIC winhttp bcrypt crypt32)
else()
    target_sources(kb_http PRIVATE src/HttpUnsupported.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(kb_http PUBLIC Threads::Threads)
endif()

target_include_directories(kb_http
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

kb_apply_quality_gates(kb_http)

if(BUILD_TESTING)
    add_executable(kb_http_tests tests/HttpTests.cpp)
    target_link_libraries(kb_http_tests PRIVATE kb_http)
    target_compile_definitions(kb_http_tests PRIVATE "KB_HTTP_TEST_SERVER=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/http_test_server.py\"")
    add_test(NAME kb_http_tests COMMAND $<TARGET_FILE:kb_http_tests>)
    set_tests_properties(kb_http_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 180 LABELS "network;correctness;process" SKIP_RETURN_CODE 77)
    kb_apply_quality_gates(kb_http_tests)
endif()
