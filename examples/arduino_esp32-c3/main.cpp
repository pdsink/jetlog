#include "logger.hpp"
#include <Arduino.h>

void setup() {
    //
    // Log reader should start in low priority thread, to print in background
    // when app is not busy.
    //
    // Using loop() for arduino is not recommended, because printing to
    // ardiono's Serial can be blocking. This is NOT jetlog restriction. You can
    // create async log print-er, if platform allows. See comments in `.cpp` file.
    //
    logger_start();
    LOG_INFO("Logger started");
}
void loop() {
    // This is just an example. You can call logger from anywhere in your code.

    int8_t int8_val = -8;
    uint8_t uint8_val = 8;
    int16_t int16_val = -16;
    uint16_t uint16_val = 16;
    int32_t int32_val = -32;
    uint32_t uint32_val = 32;
    const char* str_val = "test";
    char* mutable_str_val = const_cast<char*>("mutable");
    std::string std_str = "std_str";
    etl::string<100> etl_str = "etl_str";

    LOG_INFO("Test values: {}, {:04x}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
        int8_val, uint8_val, int16_val, uint16_val,
        int32_val, uint32_val, str_val, mutable_str_val,
        std_str, etl_str, "literal");

    delay(1000);
}
