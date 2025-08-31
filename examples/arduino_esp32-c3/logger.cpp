#include <Arduino.h>
#include "logger.hpp"

// 10K buffer
static jetlog::RingBuffer<1024*10> ringBuffer;

Logger logger(ringBuffer);
jetlog::Reader<> logReader(ringBuffer);

//
// This printer is platform-specific. In this demo we use Serial to keep things
// simple.
//
// For Arduino, with limited async support, threads are the most obvious way
// to decouple output printing. Use the lowest possible priority.
//

void logger_start() {
    xTaskCreate([](void* pvParameters) {
        (void)pvParameters;

        etl::string<1024> outputBuffer{};

        Serial.begin(115200);

        // Wait until the serial port is connected before printing. Otherwise
        // the log output from firmware start can be lost.
        while (!Serial) { vTaskDelay(pdMS_TO_TICKS(10)); }

        while (true) {
            // Read and print log records until there is no more data.
            while (logReader.pull(outputBuffer)) {
                Serial.println(outputBuffer.c_str());
                outputBuffer.clear();
            }
            // If there is no data, wait a bit before trying again.
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }, "LogOutputTask", 1024 * 4, NULL, 0, NULL);
}
