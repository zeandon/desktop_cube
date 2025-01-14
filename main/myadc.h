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

#ifndef __MYADC_H__
#define __MYADC_H__

// ADC1 通道定义
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_7
// ADC衰减定义
#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12

bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
void example_adc_calibration_deinit(adc_cali_handle_t handle);

#endif