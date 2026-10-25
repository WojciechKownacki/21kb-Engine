# kb_net: datagram transport (loopback and UDP), authenticated sessions with reliable and
# unreliable channels, and the connection handshake. It depends on kb_engine for cryptography and
# the network budget types and is not linked into any engine target.
add_library(kb_net STATIC
    include/kb/net/ByteIO.hpp
    include/kb/net/Host.hpp
    include/kb/net/Prediction.hpp
    include/kb/net/Replication.hpp
    include/kb/net/Rpc.hpp
    include/kb/net/Session.hpp
    include/kb/net/Transport.hpp
    src/Host.cpp
    src/Loopback.cpp
    src/Replication.cpp
    src/Rpc.cpp
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
endif()
