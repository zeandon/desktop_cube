/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "myadc.h"

const static char *TAG = "ADC";

// ADC1 通道定义
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_7
// ADC衰减定义
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12

static int adc_raw;
static int voltage;
static double battery_voltage;

/*
 * 简介：  使用ADC读取电压值，进行电量检测
 * 参数：  无
 * 返回值：无
 */

static void Task_ADC(void){

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
        ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, adc_raw);
        if (do_calibration1_chan0) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw, &voltage));
            ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, voltage);
            battery_voltage = (double)voltage / 0.6256;
            ESP_LOGI(TAG, "ADC%d Channel[%d] Battery Voltage: %.1f mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, battery_voltage);
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

void app_main(void)
{
    // 创建ADC任务
    xTaskCreatePinnedToCore(Task_ADC, "get battery power", 4096, NULL, 4, NULL, 0);
}
