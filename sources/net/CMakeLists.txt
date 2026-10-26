# kb_net: datagram transport (loopback and UDP), authenticated sessions with reliable and
# unreliable channels, and the connection handshake. It depends on kb_engine for cryptography and
# the network budget types and is not linked into any engine target.
add_library(kb_net STATIC
    include/kb/net/Auth.hpp
    include/kb/net/ByteIO.hpp
    include/kb/net/Host.hpp
    include/kb/net/Prediction.hpp
    include/kb/net/Replication.hpp
    include/kb/net/Rpc.hpp
    include/kb/net/SceneReplication.hpp
    include/kb/net/ScriptBridge.hpp
    include/kb/net/Session.hpp
    include/kb/net/Transport.hpp
    src/Auth.cpp
    src/Host.cpp
    src/Loopback.cpp
    src/Replication.cpp
    src/Rpc.cpp
    src/SceneReplication.cpp
    src/ScriptBridge.cpp
    src/Session.cpp
    src/UdpTransport.cpp
    src/Wire.hpp
)

target_include_directories(kb_net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kb_net PUBLIC kb_engine)

if(WIN32)
    target_link_libraries(kb_net PUBLIC ws2_32)
endif()

kb_apply_quality_gates(kb_net)

if(BUILD_TESTING)
    add_executable(kb_net_tests tests/NetTests.cpp)
    target_link_libraries(kb_net_tests PRIVATE kb_net)
    add_test(NAME kb_net_tests COMMAND $<TARGET_FILE:kb_net_tests>)
    set_tests_properties(kb_net_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 120 LABELS "network;correctness")
    kb_apply_quality_gates(kb_net_tests)

    add_executable(kb_net_replication_tests tests/ReplicationTests.cpp)
    target_link_libraries(kb_net_replication_tests PRIVATE kb_net)
    add_test(NAME kb_net_replication_tests COMMAND $<TARGET_FILE:kb_net_replication_tests>)
    set_tests_properties(kb_net_replication_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 120 LABELS "network;correctness")
    kb_apply_quality_gates(kb_net_replication_tests)

    add_executable(kb_net_script_tests tests/ScriptBridgeTests.cpp)
    target_link_libraries(kb_net_script_tests PRIVATE kb_net)
    add_test(NAME kb_net_script_tests COMMAND $<TARGET_FILE:kb_net_script_tests>)
    set_tests_properties(kb_net_script_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 120 LABELS "network;correctness;script")
    kb_apply_quality_gates(kb_net_script_tests)

    add_executable(kb_net_auth_tests tests/AuthTests.cpp)
    target_link_libraries(kb_net_auth_tests PRIVATE kb_net)
    add_test(NAME kb_net_auth_tests COMMAND $<TARGET_FILE:kb_net_auth_tests>)
    set_tests_properties(kb_net_auth_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 120 LABELS "network;correctness;security")
    kb_apply_quality_gates(kb_net_auth_tests)
endif()

# The dedicated server: a project's scene without a window or a renderer, replicated to clients over UDP.
add_library(kb_net_server STATIC
    server/DedicatedServer.cpp
    server/DedicatedServer.hpp
)
target_include_directories(kb_net_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/server)
target_link_libraries(kb_net_server PUBLIC kb_net)
kb_apply_quality_gates(kb_net_server)

add_executable(kb_dedicated_server server/ServerMain.cpp)
target_link_libraries(kb_dedicated_server PRIVATE kb_net_server)
# Next to kb_cli, where project modules resolve their plugin files the same way.
set_target_properties(kb_dedicated_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
kb_apply_quality_gates(kb_dedicated_server)

if(BUILD_TESTING)
    add_executable(kb_dedicated_server_tests tests/DedicatedServerTests.cpp)
    target_link_libraries(kb_dedicated_server_tests PRIVATE kb_net_server)
    add_dependencies(kb_dedicated_server_tests kb_dedicated_server)
    target_compile_definitions(kb_dedicated_server_tests PRIVATE "KB_DEDICATED_SERVER_PATH=\"$<TARGET_FILE:kb_dedicated_server>\"")
    add_test(NAME kb_dedicated_server_tests COMMAND $<TARGET_FILE:kb_dedicated_server_tests>)
    set_tests_properties(kb_dedicated_server_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 240 LABELS "network;correctness;process")
    kb_apply_quality_gates(kb_dedicated_server_tests)
endif()
