cmake_minimum_required(VERSION 3.28)

project(Kitsune
    VERSION 0.0.1
    DESCRIPTION "Functional C++23 reimplementation of the StarCraft II 4.10 client"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
# Before the warning flags. Generated protobuf sources are not our code, and
# stacking /W0 on top of the directory /W4 makes cl warn about the override.
include(SC2Protocol)
include(KitsuneCompiler)

# Static GNU runtime when cross-compiling with MinGW, so the exe does not
# need libstdc++ DLLs beside it. MSVC and native Linux do not get these flags.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
endif()

add_library(Kitsune
    src/Version.cpp
    src/Api/CreateGame.cpp
    src/Api/JoinGame.cpp
    src/Api/Lobby.cpp
    src/Api/MapJoin.cpp
    src/Api/Protocol.cpp
    src/Net/BaseConn.cpp
    src/Net/BaseHandshake.cpp
    src/Net/Datagram.cpp
    src/Net/GameEvent.cpp
    src/Net/GameSetup.cpp
    src/Net/KeepAlive.cpp
    src/Net/LocalNet.cpp
    src/Net/MessageChannel.cpp
    src/Net/Describe.cpp
    src/Net/LocalEvent.cpp
    src/Net/Lockstep.cpp
    src/Net/PeerSession.cpp
    src/Net/Records.cpp
    src/Net/Ports.cpp
    src/Net/SystemEvent.cpp
    src/Net/TurnChannel.cpp
    src/Net/UdpJoin.cpp
    src/Net/UdpSocket.cpp)
add_library(Kitsune::Kitsune ALIAS Kitsune)

# One variant over every game event instantiates more templates than a default
# MSVC object file can hold.
if(MSVC)
    # Anything that names the whole variant needs it too, so it is public.
    target_compile_options(Kitsune PUBLIC /bigobj)
endif()

target_include_directories(Kitsune PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
target_compile_features(Kitsune PUBLIC cxx_std_23)
find_package(asio CONFIG REQUIRED)
target_link_libraries(Kitsune PUBLIC asio::asio sc2protocol)

# Side project. The functional library does not need it and it stays off.
option(KITSUNE_BYTE_IDENTICAL
    "Configure research/byte-identical. Off. Not required and not expected."
    OFF)
if(KITSUNE_BYTE_IDENTICAL)
    add_subdirectory(research/byte-identical)
endif()

enable_testing()
add_subdirectory(tests)
