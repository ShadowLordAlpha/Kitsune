# Language level and warnings for every Kitsune target.
# C++23 on every compiler. CLion consumes this through the presets.

if(MSVC)
    # CMake 4 maps cxx_std_23 to /std:c++23. /Zc:__cplusplus makes the macro
    # match the flag. /EHsc is explicit because a toolchain file can drop it,
    # and then destructors do not run.
    add_compile_options(/EHsc /Zc:__cplusplus /utf-8 /permissive- /W4)
    # Asio picks a default of Windows 7 when this is unset and warns.
    # CMAKE_CXX_FLAGS reaches every directory. add_compile_definitions does not.
    string(APPEND CMAKE_CXX_FLAGS " /D_WIN32_WINNT=0x0A00")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()
