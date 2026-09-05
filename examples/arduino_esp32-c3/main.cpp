#include "logger.hpp"
#include <Arduino.h>

void setup() {
    //
    // Arduino's Serial can block, so the reader prints in a low-priority task
    // instead of delaying loop(). With a nonblocking output driver, logs can
    // also be drained from loop().
    //
    logger_start();
    LOG_INFO("Logger started");
}
void loop() {
    // The logger can be called from anywhere.

    int8_t int8_val = -8;
    uint8_t uint8_val = 8;
    int16_t int16_val = -16;
    uint16_t uint16_val = 16;
    int32_t int32_val = -32;
    uint32_t uint32_val = 32;
    const char* str_val = "test";
    char mutable_str_val[] = "mutable";
    std::string std_str = "std_str";
    etl::string<100> etl_str = "etl_str";

    // The format is stored by pointer: use a literal or a static const array.
    LOG_INFO("Test values: {}, {:04x}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
        int8_val, uint8_val, int16_val, uint16_val,
        int32_val, uint32_val, str_val, mutable_str_val,
        std_str, etl_str, "literal");

    delay(1000);
}
