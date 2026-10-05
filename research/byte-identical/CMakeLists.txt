# Off unless -DKITSUNE_BYTE_IDENTICAL=ON. Nothing in here is required for the
# library to be done. See README.md in this directory.
message(STATUS "Kitsune: byte-identical research tree is ON")
add_library(KitsuneByteIdentical INTERFACE)
target_include_directories(KitsuneByteIdentical INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/include")
