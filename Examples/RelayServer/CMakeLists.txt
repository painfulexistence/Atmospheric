add_executable(RelayServer main.cpp)
set_target_properties(RelayServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/RelayServer
)
# UdpRelay + spdlog come from the engine; no other deps needed.
target_link_libraries(RelayServer PRIVATE AtmosphericEngine)
