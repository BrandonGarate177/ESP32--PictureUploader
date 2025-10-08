#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

void app_main(void) {
    while (1) {
        printf("Hello from Test\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
