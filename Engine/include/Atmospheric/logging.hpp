#pragma once
#include "config.hpp"
#include <spdlog/spdlog.h>

// One-liner logging over spdlog — the single sink for the whole engine.
// ConsoleSubsystem owns the sink, level and pattern (and drives the level
// radios in the debug overlay); these macros are just the ergonomic front
// door. No Log:: class to spell out, no ConsoleSubsystem::Get() null-check,
// and no separate web path — Emscripten maps stdout to the browser console,
// so spdlog works on WASM too.
//
// Two categories, distinguished by the [Engine] / [App] prefix:
//   ENGINE_* — engine internals (subsystems, asset pipeline, renderer)
//   APP_*    — game / example code
//
//   ENGINE_INFO("loaded {} meshes in {:.1f}s", n, secs);
//   APP_WARN("controller {} disconnected", id);
//
// spdlog checks the format string at compile time, so it must be a string
// literal. To log a prebuilt std::string, pass it as an argument instead:
//   APP_INFO("{}", message);

#if RUNTIME_LOG_ON
#define ENGINE_DEBUG(msg, ...) ::spdlog::debug("[Engine] " msg __VA_OPT__(, ) __VA_ARGS__)
#define ENGINE_INFO(msg, ...)  ::spdlog::info("[Engine] " msg __VA_OPT__(, ) __VA_ARGS__)
#define ENGINE_WARN(msg, ...)  ::spdlog::warn("[Engine] " msg __VA_OPT__(, ) __VA_ARGS__)
#define ENGINE_ERROR(msg, ...) ::spdlog::error("[Engine] " msg __VA_OPT__(, ) __VA_ARGS__)

#define APP_DEBUG(msg, ...) ::spdlog::debug("[App] " msg __VA_OPT__(, ) __VA_ARGS__)
#define APP_INFO(msg, ...)  ::spdlog::info("[App] " msg __VA_OPT__(, ) __VA_ARGS__)
#define APP_WARN(msg, ...)  ::spdlog::warn("[App] " msg __VA_OPT__(, ) __VA_ARGS__)
#define APP_ERROR(msg, ...) ::spdlog::error("[App] " msg __VA_OPT__(, ) __VA_ARGS__)
#else
// Logging compiled out (RUNTIME_LOG_ON == 0): expand to nothing, and — unlike
// the old Log:: front-end — never even format the arguments.
#define ENGINE_DEBUG(...) ((void)0)
#define ENGINE_INFO(...)  ((void)0)
#define ENGINE_WARN(...)  ((void)0)
#define ENGINE_ERROR(...) ((void)0)
#define APP_DEBUG(...) ((void)0)
#define APP_INFO(...)  ((void)0)
#define APP_WARN(...)  ((void)0)
#define APP_ERROR(...) ((void)0)
#endif
