/* include */
// 蓝牙的include
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "esp_bt.h"
#include "bt_app_core.h"
#include "bt_app_av.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

// 天气时钟的include
#include <sys/param.h>
#include <ctype.h>
#include <time.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include "esp_tls.h"
#include "esp_system.h"
#include "esp_sntp.h"

#include "get_time_and_weather.h"
#include "myuart.h"
#include "myadc.h"

/* device name */
#define LOCAL_DEVICE_NAME    "Desktop_Cube"

// TAG define
#define HTTP_TAG            "HTTP_CLIENT"
#define TIME_TAG            "Recent time"
#define REFRESHTIME_TAG     "Refresh time"
#define WEATHER_TAG         "now weather"
#define DEBUG_TAG           "Debug"
#define ADC_TAG             "adc detect"

// 变量 define
int adc_raw;
int voltage;
double bat_voltage;
int bat_percent;

/* event for stack up */
enum {
    BT_APP_EVT_STACK_UP = 0,
};

/********************************
 * STATIC FUNCTION DECLARATIONS
 *******************************/

/* Device callback function */
static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param);
/* GAP callback function */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
/* handler for bluetooth stack enabled events */
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param);


/*******************************
 * STATIC FUNCTION DEFINITIONS
 ******************************/
// 蓝牙设备地址（Bluetooth Device Address）转字符串函数
// 蓝牙地址通常显示为6个字节，以十六进制表示，用冒号分隔(示例- 00:11:22:33:FF:EE)
static char *bda2str(uint8_t * bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

// 设备回调函数
static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param)
{
    switch (event) {
    // 如果事件为获取设备名称
    case ESP_BT_DEV_NAME_RES_EVT: {
        if (param->name_res.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(BT_AV_TAG, "Get local device name success: %s", param->name_res.name);
        } else {
            ESP_LOGE(BT_AV_TAG, "Get local device name failed, status: %d", param->name_res.status);
        }
        break;
    }
    // 如果是其他事件，直接打印
    default: {
        ESP_LOGI(BT_AV_TAG, "event: %d", event);
        break;
    }
    }
}

// gap（Generic Access Profile，通用访问配置文件）回调函数
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    uint8_t *bda = NULL;

    switch (event) {
    /* when authentication completed, this event comes */
    // 蓝牙设备认证事件
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(BT_AV_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            esp_log_buffer_hex(BT_AV_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(BT_AV_TAG, "authentication failed, status: %d", param->auth_cmpl.stat);
        }
        ESP_LOGI(BT_AV_TAG, "link key type of current link is: %d", param->auth_cmpl.lk_type);
        break;
    }
    // 加密模式改变事件
    case ESP_BT_GAP_ENC_CHG_EVT: {
        char *str_enc[3] = {"OFF", "E0", "AES"};
        bda = (uint8_t *)param->enc_chg.bda;
        ESP_LOGI(BT_AV_TAG, "Encryption mode to [%02x:%02x:%02x:%02x:%02x:%02x] changed to %s",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], str_enc[param->enc_chg.enc_mode]);
        break;
    }

// 如果启用安全简单配对
#if (CONFIG_EXAMPLE_A2DP_SINK_SSP_ENABLED == true)
    /* when Security Simple Pairing user confirmation requested, this event comes */
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %"PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    /* when Security Simple Pairing passkey notified, this event comes */
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %"PRIu32, param->key_notif.passkey);
        break;
    /* when Security Simple Pairing passkey requested, this event comes */
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    /* when GAP mode changed, this event comes */
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d", param->mode_chg.mode);
        break;
    /* when ACL connection completed, this event comes */
    // ACL链路（用于传输非实时逻辑数据）连接成功事件
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        bda = (uint8_t *)param->acl_conn_cmpl_stat.bda;
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT Connected to [%02x:%02x:%02x:%02x:%02x:%02x], status: 0x%x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], param->acl_conn_cmpl_stat.stat);
        break;
    /* when ACL disconnection completed, this event comes */
    // ACL链路（用于传输非实时逻辑数据）连接断开事件
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        bda = (uint8_t *)param->acl_disconn_cmpl_stat.bda;
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_ACL_DISC_CMPL_STAT_EVT Disconnected from [%02x:%02x:%02x:%02x:%02x:%02x], reason: 0x%x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], param->acl_disconn_cmpl_stat.reason);
        break;
    /* others */
    // 其他事件直接打印
    default: {
        ESP_LOGI(BT_AV_TAG, "event: %d", event);
        break;
    }
    }
}

// 蓝牙协议栈处理函数
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);

    switch (event) {
    /* when do the stack up, this event comes */
    case BT_APP_EVT_STACK_UP: {
        esp_bt_gap_set_device_name(LOCAL_DEVICE_NAME);      // 设置设备名称
        esp_bt_dev_register_callback(bt_app_dev_cb);        // 注册设备回调函数
        esp_bt_gap_register_callback(bt_app_gap_cb);        // 注册gap回调函数

        // 初始化AVRCP控制器并注册回调函数
        assert(esp_avrc_ct_init() == ESP_OK);
        esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
        // 初始化AVRCP目标并注册回调函数
        assert(esp_avrc_tg_init() == ESP_OK);
        esp_avrc_tg_register_callback(bt_app_rc_tg_cb);

        // 设置AVRCP通知事件的能力（这里设置了更改音量的能力，暂停等功能修改需要从这里入手）
        esp_avrc_rn_evt_cap_mask_t evt_set = {0};
        esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
        assert(esp_avrc_tg_set_rn_evt_cap(&evt_set) == ESP_OK);

        // 初始化A2DP汇并注册回调函数
        assert(esp_a2d_sink_init() == ESP_OK);
        esp_a2d_register_callback(&bt_app_a2d_cb);
        esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);

        /* Get the default value of the delay value */
        esp_a2d_sink_get_delay_value();     // 获取延迟值
        /* Get local device name */
        esp_bt_gap_get_device_name();       // 获取本地设备名称

        /* set discoverable and connectable mode, wait to be connected */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);      // 设置设备为可连接和可发现模式
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);     // 打印信息
        break;
    }
}

/*******************************
 * MY TASK
 ******************************/

/*
 * 简介：  使用Http联网刷新时间
 * 参数：  arg
 * 返回值：无
 */
static void Task_HttpGetTime(void* arg){
    //上电之后延时5s刷新一次时间，之后不再联网刷新，完全使用RTC
    vTaskDelay(pdMS_TO_TICKS(5000));
    get_time();
    //删除任务
    vTaskDelete(NULL);
}

/*
 * 简介：  使用Http联网获取天气
 * 参数：  arg
 * 返回值：无
 */
static void Task_HttpGetWeather(void* arg){
    //上电之后延时5s刷新一次天气，之后不再联网刷新
    vTaskDelay(pdMS_TO_TICKS(10000));
    get_weather();
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

        // // 年
        // uart_send_string("year:");
        // uart_send_num(timeinfo.tm_year);
        // uart_send_string("\r\n");

        // 月
        uart_send_string("month.val=");
        int real_mon = timeinfo.tm_mon + 1;     // 补偿
        uart_send_num(real_mon);
        uart_send_string("\xff\xff\xff");

        // 日
        uart_send_string("day.val=");
        uart_send_num(timeinfo.tm_mday);
        uart_send_string("\xff\xff\xff");

        // // 周
        // uart_send_string("wday:");
        // uart_send_num(timeinfo.tm_wday);
        // uart_send_string("\r\n");

        // 时
        uart_send_string("hour.val=");
        uart_send_num(timeinfo.tm_hour);
        uart_send_string("\xff\xff\xff");

        // 分
        uart_send_string("minute.val=");
        uart_send_num(timeinfo.tm_min);
        uart_send_string("\xff\xff\xff");

        // // 秒
        // uart_send_string("second:");
        // uart_send_num(timeinfo.tm_sec);
        // uart_send_string("\r\n");

        // 10s刷新一次
        vTaskDelay(pdMS_TO_TICKS(10000));
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
            bat_voltage = (double)voltage * 2;
            ESP_LOGI(ADC_TAG, "ADC%d Channel[%d] Battery Voltage: %.1f mV", ADC_UNIT_1 + 1, EXAMPLE_ADC1_CHAN0, bat_voltage);
            // 根据查表判断当前电压值对应的电池电量
            if(bat_voltage >= 4150){
                bat_percent = 100;
            }
            else if(bat_voltage >= 4080){
                bat_percent = 90;
            }
            else if(bat_voltage >= 3970){
                bat_percent = 80;
            }
            else if(bat_voltage >= 3900){
                bat_percent = 70;
            }
            else if(bat_voltage >= 3840){
                bat_percent = 60;
            }
            else if(bat_voltage >= 3790){
                bat_percent = 50;
            }
            else if(bat_voltage >= 3760){
                bat_percent = 40;
            }
            else if(bat_voltage >= 3730){
                bat_percent = 30;
            }
            else if(bat_voltage >= 3710){
                bat_percent = 20;
            }
            else if(bat_voltage >= 3650){
                bat_percent = 10;
            }
            else{
                bat_percent = 0;
            }
            ESP_LOGI(ADC_TAG, "Battery power percent: %d%%",bat_percent);

            // 串口打印
            uart_send_string("bat.val=");
            uart_send_num(bat_percent);
            uart_send_string("\xff\xff\xff");
        }

        // 1min刷新一次
        vTaskDelay(pdMS_TO_TICKS(60000));
    }

    //Tear Down（测试完成后进行清理）
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
    if (do_calibration1_chan0) {
        example_adc_calibration_deinit(adc1_cali_chan0_handle);
    }

    //删除任务
    vTaskDelete(NULL);
}

/*******************************
 * MAIN ENTRY POINT
 ******************************/

void app_main(void)
{
    // 设备地址
    char bda_str[18] = {0};
    /* initialize NVS — it is used to store PHY calibration data */
    // nvs初始化
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /*
     * This example only uses the functions of Classical Bluetooth.
     * So release the controller memory for Bluetooth Low Energy.
     */
    // 释放BLE内存
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    // 初始化蓝牙控制器，设置为经典蓝牙模式
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((err = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(err));
        return;
    }
    if ((err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(err));
        return;
    }

    // 初始化蓝牙协议栈
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
#if (CONFIG_EXAMPLE_A2DP_SINK_SSP_ENABLED == false)
    bluedroid_cfg.ssp_en = false;
#endif
    if ((err = esp_bluedroid_init_with_cfg(&bluedroid_cfg)) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(err));
        return;
    }

    if ((err = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(err));
        return;
    }

// 配置SSP参数（这里没用到）
#if (CONFIG_EXAMPLE_A2DP_SINK_SSP_ENABLED == true)
    /* set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    /* set default parameters for Legacy Pairing (use fixed pin code 1234) */
    // 设置Legacy Pairing参数
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code;
    pin_code[0] = '1';
    pin_code[1] = '2';
    pin_code[2] = '3';
    pin_code[3] = '4';
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    // 打印设备地址
    ESP_LOGI(BT_AV_TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
    // 启动蓝牙任务
    bt_app_task_start_up();
    /* bluetooth device name, connection mode and profile set up */
    // 派发栈初始化任务
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL);

    //连接WIFI
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(HTTP_TAG, "Connected to AP, begin task");

    // UART初始化
    uart_init();

    // 创建任务（数字越大优先级越高）
    // 联网获取时间任务
    xTaskCreatePinnedToCore(Task_HttpGetTime, "http get time", 16384, NULL, 10, NULL, 1);
    // 联网获取天气任务
    xTaskCreatePinnedToCore(Task_HttpGetWeather, "http get weather", 16384, NULL, 8, NULL, 1);
    // RTC获取时间任务
    xTaskCreatePinnedToCore(Task_RTCGetTime, "rtc get time", 4096, NULL, 3, NULL, 1);
    // ADC检测电量任务
    xTaskCreatePinnedToCore(Task_ADCGetVoltage, "adc get voltage", 2048, NULL, 12, NULL, 0);

    // 拉高GPIO25，使能MAX98357
    gpio_config_t Gpio_config = {
        .pin_bit_mask = (1ull << GPIO_NUM_25),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = true,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&Gpio_config);
    gpio_set_level(GPIO_NUM_25, 1);

}
