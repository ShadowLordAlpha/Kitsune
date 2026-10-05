# One source file per test. Group new files with the module they cover:
#   api/      websocket protocol and the client rules around it
#   net/      ports, datagrams, lockstep, sessions, the UDP join
#   live/     needs a local Base75689 client. Disabled in the default run.
#   version/  the 4.10 build this library targets

find_package(Threads REQUIRED)

function(kitsune_test source test_name)
    get_filename_component(stem "${source}" NAME_WE)
    set(target "kitsune_${stem}")
    add_executable(${target} "${source}")
    target_link_libraries(${target} PRIVATE Kitsune Threads::Threads)
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

kitsune_test(version/version_test.cpp version)

kitsune_test(api/create_game_test.cpp create_game)
kitsune_test(api/map_join_test.cpp map_join)
kitsune_test(api/api_join_test.cpp api_join)
kitsune_test(api/protocol_test.cpp protocol)

kitsune_test(net/ports_test.cpp ports)
kitsune_test(net/lockstep_test.cpp lockstep)
kitsune_test(net/net_event_vectors_test.cpp net_event_vectors)
kitsune_test(net/bind_test.cpp bind)
kitsune_test(net/base_conn_test.cpp base_conn)
kitsune_test(net/base_handshake_test.cpp base_handshake)
kitsune_test(net/game_event_test.cpp game_event)
kitsune_test(net/session_test.cpp session)
kitsune_test(net/system_event_test.cpp system_event)
kitsune_test(net/system_stream_test.cpp system_stream)
kitsune_test(net/turn_channel_test.cpp turn_channel)
kitsune_test(net/game_setup_test.cpp game_setup)
kitsune_test(net/message_channel_test.cpp message_channel)
kitsune_test(net/keep_alive_test.cpp keep_alive)
kitsune_test(net/udp_join_test.cpp udp_join)

# Talks to a real 4.10 client. Default ctest skips it. Run the executable.
add_executable(kitsune_sc2_udp_join live/sc2_udp_join.cpp)
target_link_libraries(kitsune_sc2_udp_join PRIVATE Kitsune Threads::Threads)
add_test(NAME sc2_udp_join COMMAND kitsune_sc2_udp_join)
set_tests_properties(sc2_udp_join PROPERTIES DISABLED TRUE LABELS "live")
