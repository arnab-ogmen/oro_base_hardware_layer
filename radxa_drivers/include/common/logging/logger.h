#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

namespace oro {
namespace media {
namespace logging {

inline void init_logger(const std::string& name) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    // Format: [YYYY-MM-DD HH:MM:SS.mmm] [logger_name] [level] message
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

    auto logger = std::make_shared<spdlog::logger>(name, console_sink);
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    
    // Set to debug level by default for bring-up phase
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::info);
}

} // namespace logging
} // namespace media
} // namespace oro
