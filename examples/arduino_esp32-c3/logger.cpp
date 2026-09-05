#include <Arduino.h>
#include "logger.hpp"

static jetlog::RingBuffer<1024*10> ringBuffer;

LogWriter logger(ringBuffer);
jetlog::Reader<LogConfig> logReader(ringBuffer);

//
// Output is platform-specific; this example uses blocking Serial writes.
// A task at the lowest priority keeps printing separate from application work.
//

void logger_start() {
    xTaskCreate([](void* pvParameters) {
        (void)pvParameters;

        static etl::string<1024> outputBuffer{};

        Serial.begin(115200);

        // Wait until the serial port is connected before printing. Otherwise
        // the log output from firmware start can be lost.
        while (!Serial) { vTaskDelay(pdMS_TO_TICKS(10)); }

        while (true) {
            while (logReader.pull(outputBuffer)) {
                Serial.println(outputBuffer.c_str());
                outputBuffer.clear();
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }, "LogOutputTask", 1024 * 4, NULL, 0, NULL);
}
