add_executable(RelayServer main.cpp)
set_target_properties(RelayServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/RelayServer
)
# UdpRelay + spdlog come from the engine; no other deps needed.
target_link_libraries(RelayServer PRIVATE AtmosphericEngine)

# RelayServerApp: same relay, driven through a headless Application +
# RelaySubsystem instead of a bare loop (see main_app.cpp for when that's
# worth the extra moving part). Not built on Emscripten: AppConfig::headless
# and RelaySubsystem are both native-only (see their respective headers).
if(NOT EMSCRIPTEN)
    add_executable(RelayServerApp main_app.cpp)
    set_target_properties(RelayServerApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/RelayServerApp
    )
    target_link_libraries(RelayServerApp PRIVATE AtmosphericEngine)
endif()
