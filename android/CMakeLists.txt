# Mod Loader - Android port (AML plugin for GTA San Andreas 2.00, ARMv7)
#
#   Android (the actual mod):
#     cmake -S android -B build -G Ninja \
#           -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#           -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static \
#           -DCMAKE_BUILD_TYPE=Release
#     cmake --build build                      ->  build/libmodloader.so
#
#   Host (unit tests of the portable parts, any PC):
#     cmake -S android -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure
#
# The AML SDK headers are NOT part of this repository: they are fetched from the AML repository at configure time
# (or taken from -DAML_SDK_DIR=<path to a checkout of RusJJ/AndroidModLoader>).

cmake_minimum_required(VERSION 3.16)
project(modloader_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Where the original Mod Loader sources live (this directory is android/ inside the original repository)
get_filename_component(ML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(ML_INCLUDE   "${ML_ROOT}/include")          # modloader/modloader.h, modloader/util/hash.hpp ...
set(ML_SHARED    "${ML_ROOT}/src/shared")       # ini_parser/ini_parser.hpp

# --------------------------------------------------------------------------------------------------------------------
# Sources that do not depend on the game or on AML (they are what the unit tests build)
# --------------------------------------------------------------------------------------------------------------------
set(MLA_PORTABLE_SOURCES
    src/util/fs.cpp
    src/util/log.cpp
    src/core/config.cpp
    src/core/loader.cpp
    src/game/host.cpp
    src/game/gtasa200.cpp
    src/plugins/vfs.cpp
    src/plugins/gtadat.cpp
    src/plugins/img.cpp
    src/plugins/stream_patcher.cpp
    src/plugins/std_data.cpp
    src/plugins/std_stream.cpp
)

function(mla_common_options target)
    target_include_directories(${target} PRIVATE "${ML_INCLUDE}" "${ML_SHARED}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    # ini_parser.hpp (upstream) still uses std::not1, deprecated since C++17 but present in every standard library we target
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-comment -fno-strict-aliasing)
endfunction()

if(ANDROID)
    # ----------------------------------------------------------------------------------------------------------------
    # The AML plugin
    # ----------------------------------------------------------------------------------------------------------------
    if(NOT ANDROID_ABI STREQUAL "armeabi-v7a")
        message(FATAL_ERROR "GTA San Andreas for Android 2.00 is a 32 bits ARM game, configure with -DANDROID_ABI=armeabi-v7a (got ${ANDROID_ABI})")
    endif()

    set(AML_SDK_DIR "" CACHE PATH "Checkout of https://github.com/RusJJ/AndroidModLoader (downloaded when empty)")
    set(AML_GIT_TAG "3ef5c918ec855f3db5e3a216dcb176e209b89922" CACHE STRING "AML commit used when AML_SDK_DIR is not given")

    if(NOT AML_SDK_DIR)
        include(FetchContent)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)   # we only want the headers, not add_subdirectory()
        endif()
        FetchContent_Declare(aml_sdk GIT_REPOSITORY https://github.com/RusJJ/AndroidModLoader.git GIT_TAG ${AML_GIT_TAG})
        FetchContent_GetProperties(aml_sdk)
        if(NOT aml_sdk_POPULATED)
            FetchContent_Populate(aml_sdk)
        endif()
        set(AML_SDK_DIR "${aml_sdk_SOURCE_DIR}")
    endif()
    message(STATUS "AML SDK: ${AML_SDK_DIR}")

    add_library(modloader SHARED
        ${MLA_PORTABLE_SOURCES}
        src/aml_entry.cpp
        "${AML_SDK_DIR}/mod/logger.cpp"
    )
    mla_common_options(modloader)
    target_include_directories(modloader PRIVATE "${AML_SDK_DIR}")
    target_compile_options(modloader PRIVATE -O2 -fvisibility=hidden -fvisibility-inlines-hidden)
    target_compile_definitions(modloader PRIVATE NDEBUG)
    target_link_libraries(modloader PRIVATE log android dl)
    # Only the AML entry points (__GetModInfo, OnModLoad...) are exported, everything else stays private
    target_link_options(modloader PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    set_target_properties(modloader PROPERTIES PREFIX "lib" OUTPUT_NAME "modloader")
else()
    # ----------------------------------------------------------------------------------------------------------------
    # Host build: unit tests
    # ----------------------------------------------------------------------------------------------------------------
    enable_testing()
    add_executable(modloader_tests tests/test_main.cpp ${MLA_PORTABLE_SOURCES})
    mla_common_options(modloader_tests)
    target_compile_options(modloader_tests PRIVATE -O1 -g)
    add_test(NAME modloader_tests COMMAND modloader_tests)
endif()
