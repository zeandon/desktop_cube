#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "get_time_and_weather.h"

// TAG define
#define HTTP_TAG            "HTTP_CLIENT"
#define TIME_TAG            "Recent time"
#define REFRESHTIME_TAG     "Refresh time"
#define WEATHER_TAG         "now weather"
#define DEBUG_TAG           "Debug"

/*
 * 简介：  使用Http联网刷新时间
 * 参数：  arg
 * 返回值：无
 */
static void Task_HttpGetTime(void* arg){
    while(1){
        get_time();
        //1min刷新一次时间（调试用10s）
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    //删除任务
    vTaskDelete(NULL);
}

/*
 * 简介：  使用Http联网获取天气
 * 参数：  arg
 * 返回值：无
 */
static void Task_HttpGetWeather(void* arg){
    while(1){
        get_weather();
        //10min刷新一次天气（调试用1min）
        vTaskDelay(pdMS_TO_TICKS(60000));
    }

    //删除任务
    vTaskDelete(NULL);
}

/*
 * 简介：  使用RTC获取当前时间
 * 参数：  arg
 * 返回值：无
 */
static void Task_RTCGetTime(void* arg){
    //变量定义
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;

    //循环获取时间
    while(1){
        time(&now);
        // 将时区设置为中国标准时间
        setenv("TZ", "CST-8", 1);
        tzset();

        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TIME_TAG, "现在日期、时间: %s", strftime_buf);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*
 * 简介：  main函数
 * 参数：  无
 * 返回值：无
 */
void app_main(void)
{
    //初始化nvs，保存SSID等参数
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //连接WIFI
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(HTTP_TAG, "Connected to AP, begin task");

    //创建任务（数字越大优先级越高）
    xTaskCreatePinnedToCore(Task_HttpGetTime, "http get time", 8192, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(Task_HttpGetWeather, "http get weather", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(Task_RTCGetTime, "rtc get time", 4096, NULL, 3, NULL, 0);

    while(1){
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
