#pragma once

#include "jetlog/jetlog.hpp"

using LogConfig = jetlog::Config<>;

using LogWriter = jetlog::Writer<LogConfig>;

extern LogWriter logger;

// No tags in this example, so the tag is an empty string.
#define LOG_ERROR(...) logger.push("", jetlog::level::error, __VA_ARGS__)
#define LOG_INFO(...) logger.push("", jetlog::level::info, __VA_ARGS__)
#define LOG_DEBUG(...) logger.push("", jetlog::level::debug, __VA_ARGS__)

void logger_start();
