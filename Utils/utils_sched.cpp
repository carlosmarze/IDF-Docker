#include "utils_sched.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils_logger.h"


static const char* TAG = "SCHED";

// Punteros a funciones definidas en main
//capaz que este archivo no debería estar en utils, sino el proyecto
static sched_cb_t cb_60 = nullptr;
static sched_cb_t cb_60_temp = nullptr;
static sched_cb_t cb_3600_post = nullptr;
static sched_cb_t cb_3600_updt = nullptr;

void sched_register_task_60(sched_cb_t cb)        { cb_60 = cb; }
void sched_register_task_60_temp(sched_cb_t cb)   { cb_60_temp = cb; }
void sched_register_task_3600_post(sched_cb_t cb) { cb_3600_post = cb; }
void sched_register_task_3600_updt(sched_cb_t cb) { cb_3600_updt = cb; }

static void scheduler_task(void* arg)
{
    //std::string log_msg;

    uint32_t seconds = 0;
    LOGI(TAG, "Scheduler iniciado");
    //write_system_log(TAG, log_msg.c_str());
    //ESP_LOGI(TAG, "%s",log_msg.c_str());
    //ESP_LOGI(TAG, "Scheduler iniciado");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        seconds++;

        if (seconds % 60 == 0 && cb_60) {
            cb_60();
            cb_60_temp();
        }

        if (seconds % 3600 == 0) {
            if (cb_3600_post) cb_3600_post();
            if (cb_3600_updt) cb_3600_updt();
        }

        if (seconds >= 86400) seconds = 0;
    }
}

void start_scheduler()
{
    xTaskCreate(scheduler_task, "scheduler", 8192, NULL, 5, NULL);
}
