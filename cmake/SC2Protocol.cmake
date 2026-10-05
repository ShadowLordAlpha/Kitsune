# Blizzard s2client-proto at 6c1eedc, the tree that documents the 4.10 client.
# Later commits add 5.0 fields (replay recording, current APM). Those stay out.

find_package(Protobuf CONFIG REQUIRED)

set(KITSUNE_PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/s2client-proto")
set(KITSUNE_PROTO_OUT "${CMAKE_CURRENT_BINARY_DIR}/generated")

set(KITSUNE_PROTOS
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/common.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/data.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/debug.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/error.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/query.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/raw.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/sc2api.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/score.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/spatial.proto"
    "${KITSUNE_PROTO_ROOT}/s2clientprotocol/ui.proto")

add_library(sc2protocol STATIC)
target_link_libraries(sc2protocol PUBLIC protobuf::libprotobuf)
target_include_directories(sc2protocol SYSTEM PUBLIC
    "$<BUILD_INTERFACE:${KITSUNE_PROTO_OUT}>")
target_compile_features(sc2protocol PUBLIC cxx_std_17)

if(MSVC)
    # sc2api.pb.cc exceeds the default object-section limit.
    target_compile_options(sc2protocol PRIVATE /bigobj)
endif()

protobuf_generate(
    TARGET sc2protocol
    LANGUAGE cpp
    PROTOS ${KITSUNE_PROTOS}
    IMPORT_DIRS "${KITSUNE_PROTO_ROOT}"
    PROTOC_OUT_DIR "${KITSUNE_PROTO_OUT}")
