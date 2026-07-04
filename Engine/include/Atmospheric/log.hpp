#pragma once
#include <fmt/format.h>
#include <string>
#include <utility>

// Single logging front-end for the whole engine.
//
// Formats with fmt (compile-time-checked format strings) and dispatches to
// spdlog on native / the browser console on web. Stateless and safe to call
// before any subsystem exists — unlike routing through ConsoleSubsystem::Get(),
// which is null until the Application is constructed.
//
//   Log::Info("loaded {} in {:.1f}s", name, seconds);
//   Log::Warn("texture '{}' missing, using fallback", path);
//
// Non-instantiable: a static-only utility, not an object.
class Log {
public:
    Log() = delete;

    template<typename... Args> static void Debug(fmt::format_string<Args...> f, Args&&... args) {
        Dispatch(Level::Debug, fmt::format(f, std::forward<Args>(args)...));
    }
    template<typename... Args> static void Info(fmt::format_string<Args...> f, Args&&... args) {
        Dispatch(Level::Info, fmt::format(f, std::forward<Args>(args)...));
    }
    template<typename... Args> static void Warn(fmt::format_string<Args...> f, Args&&... args) {
        Dispatch(Level::Warn, fmt::format(f, std::forward<Args>(args)...));
    }
    template<typename... Args> static void Error(fmt::format_string<Args...> f, Args&&... args) {
        Dispatch(Level::Error, fmt::format(f, std::forward<Args>(args)...));
    }

private:
    enum class Level { Debug, Info, Warn, Error };
    static void Dispatch(Level level, const std::string& message);// defined in log.cpp
};
