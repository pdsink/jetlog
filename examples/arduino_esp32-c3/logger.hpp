#pragma once

#include "jetlog/jetlog.hpp"

using LogConfig = jetlog::Config<>;

using LogWriter = jetlog::Writer<LogConfig>;

extern LogWriter logger;

// No tags in this example, so the tag is an empty string.
namespace jetlog_options {
inline constexpr jetlog::PushOptions error{jetlog::static_str{""}, jetlog::level::error};
inline constexpr jetlog::PushOptions info{jetlog::static_str{""}, jetlog::level::info};
inline constexpr jetlog::PushOptions debug{jetlog::static_str{""}, jetlog::level::debug};
} // namespace jetlog_options

#define LOG_ERROR(...) logger.push(jetlog_options::error, __VA_ARGS__)
#define LOG_INFO(...) logger.push(jetlog_options::info, __VA_ARGS__)
#define LOG_DEBUG(...) logger.push(jetlog_options::debug, __VA_ARGS__)

void logger_start();
