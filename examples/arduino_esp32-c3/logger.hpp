#pragma once

#include "jetlog/jetlog.hpp"

//
// Here we create simple jetlog wrappers to use in project.
//

using Logger = jetlog::Writer<>;

extern Logger logger;

// We do not use tags in this example, so we pass empty string.
#define LOG_ERROR(...) logger.push("", jetlog::level::error, __VA_ARGS__)
#define LOG_INFO(...) logger.push("", jetlog::level::info, __VA_ARGS__)
#define LOG_DEBUG(...) logger.push("", jetlog::level::debug, __VA_ARGS__)

void logger_start();
