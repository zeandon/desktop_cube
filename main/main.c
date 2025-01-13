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
#include "myuart.h"
#include "myadc.h"

// TAG define
#define HTTP_TAG            "HTTP_CLIENT"
#define TIME_TAG            "Recent time"
#define REFRESHTIME_TAG     "Refresh time"
#define WEATHER_TAG         "now weather"
#define DEBUG_TAG           "Debug"
#define ADC_TAG             "adc detect"

// 变量 define
static int adc_raw;
static int voltage;
static double battery_voltage;
static int bat_percent;

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

        // 使用串口打印当前时间
        // // 判断是否为夏令时
        // uart_send_string("is dst?:");
        // uart_send_num(timeinfo.tm_isdst);
        // uart_send_string("\r\n");

        // // 今年的第几天
        // uart_send_string("yday:");
        // uart_send_num(timeinfo.tm_yday);
        // uart_send_string("\r\n");

        // 年
        uart_send_string("year:");
        uart_send_num(timeinfo.tm_year);
        uart_send_string("\r\n");

        // 月
        uart_send_string("month:");
        uart_send_num(timeinfo.tm_mon);
        uart_send_string("\r\n");

        // 日
        uart_send_string("mday:");
        uart_send_num(timeinfo.tm_mday);
        uart_send_string("\r\n");

        // 周
        uart_send_string("wday:");
        uart_send_num(timeinfo.tm_wday);
        uart_send_string("\r\n");

        // 时
        uart_send_string("hour:");
        uart_send_num(timeinfo.tm_hour);
        uart_send_string("\r\n");

        // 分
        uart_send_string("minute:");
        uart_send_num(timeinfo.tm_min);
        uart_send_string("\r\n");

        // 秒
        uart_send_string("second:");
        uart_send_num(timeinfo.tm_sec);
        uart_send_string("\r\n");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*
 * 简介：  使用ADC检测电池电量
 * 参数：  arg
 * 返回值：无
 */
static void Task_ADCGetVoltage(void* arg){

    //-------------ADC1 Init---------------//
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = EXAMPLE_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));

    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;
    bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN, &adc1_cali_chan0_handle);

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw));
        ESP_LOGI(ADC_TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw);
        if (do_calibration1_chan0) {
            // 原始ADC数据转化为电压数据
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw, &voltage));
            ESP_LOGI(ADC_TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, voltage);

            // 经过分压得到电池电压
            battery_voltage = (double)voltage / 0.6256;
            ESP_LOGI(ADC_TAG, "ADC%d Channel[%d] Battery Voltage: %.1f mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, battery_voltage);

            // 根据查表判断当前电压值对应的电池电量
            if(battery_voltage >= 4.15){
                bat_percent = 100;
            }
            else if(battery_voltage >= 4.08){
                bat_percent = 90;
            }
            else if(battery_voltage >= 3.97){
                bat_percent = 80;
            }
            else if(battery_voltage >= 3.90){
                bat_percent = 70;
            }
            else if(battery_voltage >= 3.84){
                bat_percent = 60;
            }
            else if(battery_voltage >= 3.79){
                bat_percent = 50;
            }
            else if(battery_voltage >= 3.76){
                bat_percent = 40;
            }
            else if(battery_voltage >= 3.73){
                bat_percent = 30;
            }
            else if(battery_voltage >= 3.71){
                bat_percent = 20;
            }
            else if(battery_voltage >= 3.65){
                bat_percent = 10;
            }
            else{
                bat_percent = 0;
            }
            ESP_LOGI(ADC_TAG, "Battery power percent: %d%%",bat_percent);

            // 串口打印
            uart_send_string("battery power percent:");
            uart_send_num(bat_percent);
            uart_send_string("\r\n");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    //Tear Down（测试完成后进行清理）
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
    if (do_calibration1_chan0) {
        example_adc_calibration_deinit(adc1_cali_chan0_handle);
    }

    //删除任务
    vTaskDelete(NULL);
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

    // UART初始化
    uart_init();

    // 创建任务（数字越大优先级越高）
    // 联网获取时间任务
    xTaskCreatePinnedToCore(Task_HttpGetTime, "http get time", 8192, NULL, 6, NULL, 1);
    // 联网获取天气任务
    xTaskCreatePinnedToCore(Task_HttpGetWeather, "http get weather", 8192, NULL, 5, NULL, 1);
    // RTC获取时间任务
    xTaskCreatePinnedToCore(Task_RTCGetTime, "rtc get time", 4096, NULL, 3, NULL, 0);
    // ADC检测电量任务
    xTaskCreatePinnedToCore(Task_ADCGetVoltage, "adc get voltage", 4096, NULL, 4, NULL, 0);

    while(1){
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
