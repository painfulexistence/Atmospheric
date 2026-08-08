# Unit tests (Catch2, registered with ctest). Built when AE_BUILD_TESTS is ON —
# on by default for a top-level desktop build (see the root CMakeLists.txt),
# off for Emscripten/Android/iOS and when Atmospheric is consumed as a submodule.
#
# Catch2 is pulled via FetchContent rather than vcpkg so the test dependency
# doesn't touch the manifest used by the shipping platforms. The networking
# primitives under test (ReliableChannel, NetConditioner) are dependency-free,
# so we compile their sources straight into the test binary instead of linking
# AtmosphericEngine — the suite builds and runs without the graphics stack.

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(net_tests
    reliable_channel_test.cpp
    net_conditioner_test.cpp
    reliable_killfeed_test.cpp
    ${CMAKE_SOURCE_DIR}/Engine/src/reliable_channel.cpp
    ${CMAKE_SOURCE_DIR}/Engine/src/net_conditioner.cpp
)
target_include_directories(net_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/include/Atmospheric
    ${CMAKE_SOURCE_DIR}/Examples/Deathmatch# reliable_killfeed_test shares the game's kill-event codec (protocol.hpp)
)
target_link_libraries(net_tests PRIVATE Catch2::Catch2WithMain)

# catch_discover_tests registers each TEST_CASE as an individual ctest test.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(net_tests)
