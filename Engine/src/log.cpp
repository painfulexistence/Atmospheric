#include "log.hpp"
#include "config.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <spdlog/spdlog.h>
#endif

void Log::Dispatch(Level level, const std::string& message) {
#if RUNTIME_LOG_ON
#ifdef __EMSCRIPTEN__
    switch (level) {
    case Level::Error: EM_ASM({ console.error(UTF8ToString($0)); }, message.c_str()); break;
    case Level::Warn:  EM_ASM({ console.warn(UTF8ToString($0)); }, message.c_str()); break;
    default:           EM_ASM({ console.info(UTF8ToString($0)); }, message.c_str()); break;
    }
#else
    switch (level) {
    case Level::Debug: spdlog::debug(message); break;
    case Level::Info:  spdlog::info(message); break;
    case Level::Warn:  spdlog::warn(message); break;
    case Level::Error: spdlog::error(message); break;
    }
#endif
#else
    (void)level;
    (void)message;
#endif
}
