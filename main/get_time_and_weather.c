#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "protocol_examples_utils.h"
#include "esp_tls.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "get_time_and_weather.h"
#include "myuart.h"

// 参数 define
#define MAX_HTTP_OUTPUT_BUFFER 4096
// url define
#define SUNING_URL          "https://f.m.suning.com/api/ct.do"
#define BAIDUMAP_URL        "https://api.map.baidu.com/weather/v1/?district_id=420111&data_type=all&ak=giWJALg51ZIWPEUfLbv8p5n9U05xT6Pm"
// TAG define
#define HTTP_TAG            "HTTP_CLIENT"
#define TIME_TAG            "Recent time"
#define REFRESHTIME_TAG     "Refresh time"
#define WEATHER_TAG         "now weather"
#define DEBUG_TAG           "Debug"

static double GetTimeStamp = 0;
static long long TimeStamp = 0;

/*
 * 简介：  http事件处理函数
 * 参数：  evt
 * 返回值：ESP_OK
 */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Clean the buffer in case of a new request
            if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        // We initialize output_buffer with 0 because it is used by strlen() and similar functions therefore should be null terminated.
                        output_buffer = (char *) calloc(content_len + 1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(HTTP_TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (content_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(HTTP_TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(HTTP_TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(HTTP_TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(HTTP_TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(HTTP_TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

/*
 * 简介：  使用http连接苏宁易购API获取时间
 * 参数：  无
 * 返回值：无
 */
static void Time_http_rest_with_url(void)
{
    //临时存放JSON返回值的数组
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};

    //HTTP参数配置
    esp_http_client_config_t config = {
        .url = SUNING_URL,
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = true,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    // debug用代码
    // if (err == ESP_OK) {
    //     ESP_LOGI(HTTP_TAG, "HTTP GET Status = %d, content_length = %"PRId64,
    //             esp_http_client_get_status_code(client),
    //             esp_http_client_get_content_length(client));
    // } else {
    //     ESP_LOGE(HTTP_TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    // }
    // ESP_LOG_BUFFER_HEX(HTTP_TAG, local_response_buffer, strlen(local_response_buffer));

    // 解析API返回的cJSON格式文件
    // Debug时可以拿来看接收到的JSON数据
    // ESP_LOGI(HTTP_TAG,"%s",local_response_buffer);
    cJSON *root = cJSON_Parse(local_response_buffer);
    GetTimeStamp = cJSON_GetObjectItem(root,"currentTime")->valuedouble;

    // 获取的时间戳是ms级别的，除去后三位，变为秒级的时间戳
    TimeStamp = (long long)(GetTimeStamp / 1000);
    // debug
    ESP_LOGI(HTTP_TAG,"UNIX时间戳：%llu",TimeStamp);

    // 变量
    struct tm *time;

    // 调用系统函数，将时间戳转换为结构体
    time = localtime(&TimeStamp);

    //使用时间结构体设置RTC时间
    time_t timeSinceEpoch = mktime(time);
    struct timeval Init_Time = { .tv_sec = timeSinceEpoch };
    settimeofday(&Init_Time, NULL);
    ESP_LOGI(REFRESHTIME_TAG,"联网更新时间成功！");

    //删除cJSON结构体对象，防止内存泄漏
    cJSON_Delete(root);

    //删除HTTP结构体对象，防止内存泄漏
    esp_http_client_cleanup(client);
}


/*
 * 简介：  使用http连接百度地图API获取天气
 * 参数：  无
 * 返回值：无
 */
static void Weather_http_rest_with_url(void)
{
    //临时存放JSON返回值的数组
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};

    //HTTP参数配置
    esp_http_client_config_t config = {
        .url = BAIDUMAP_URL,
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = true,
        .timeout_ms = 60000,        // 60s
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    // debug用代码
    // if (err == ESP_OK) {
    //     ESP_LOGI(HTTP_TAG, "HTTP GET Status = %d, content_length = %"PRId64,
    //             esp_http_client_get_status_code(client),
    //             esp_http_client_get_content_length(client));
    // } else {
    //     ESP_LOGE(HTTP_TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    // }
    // ESP_LOG_BUFFER_HEX(HTTP_TAG, local_response_buffer, strlen(local_response_buffer));

    //解析API返回的cJSON格式文件
    // Debug时可以拿来看接收到的JSON数据
    // ESP_LOGI(HTTP_TAG,"%s",local_response_buffer);
    cJSON *root = cJSON_Parse(local_response_buffer);
    cJSON *result = cJSON_GetObjectItem(root,"result");
    cJSON *location = cJSON_GetObjectItem(result,"location");
    cJSON *now = cJSON_GetObjectItem(result,"now");

    char *name = cJSON_GetObjectItem(location,"name")->valuestring;        //地区
    char *text = cJSON_GetObjectItem(now,"text")->valuestring;             //天气
    int temp = cJSON_GetObjectItem(now,"temp")->valueint;                  //温度
    int rh = cJSON_GetObjectItem(now,"rh")->valueint;                      //湿度
    char *wind_class = cJSON_GetObjectItem(now,"wind_class")->valuestring; //风力
    char *wind_dir = cJSON_GetObjectItem(now,"wind_dir")->valuestring;     //风向

    // 通过LOG打印得到的数据
    ESP_LOGI(WEATHER_TAG,"地区 %s\r\n天气 %s\r\n温度 %d\r\n湿度 %d\r\n风力 %s\r\n风向 %s\r\n",name,text,temp,rh,wind_class,wind_dir);

    // 通过串口打印得到的数据
    uart_send_string("location:");
    uart_send_string(name);
    uart_send_string("\r\n");

    uart_send_string("weather:");
    uart_send_string(text);
    uart_send_string("\r\n");

    uart_send_string("temperature:");
    uart_send_num(temp);
    uart_send_string("\r\n");

    uart_send_string("humidity:");
    uart_send_num(rh);
    uart_send_string("\r\n");

    uart_send_string("wind power:");
    uart_send_string(wind_class);
    uart_send_string("\r\n");

    uart_send_string("wind direction:");
    uart_send_string(wind_dir);
    uart_send_string("\r\n");

    //删除cJSON结构体对象，防止内存泄漏
    cJSON_Delete(root);

    //删除HTTP结构体对象，防止内存泄漏
    esp_http_client_cleanup(client);
}

/*
 * 简介：  获取时间（外部调用函数）
 * 参数：  无
 * 返回值：无
 */
void get_time(void){
    Time_http_rest_with_url();
}

/*
 * 简介：  获取天气（外部调用函数）
 * 参数：  无
 * 返回值：无
 */
void get_weather(void){
    Weather_http_rest_with_url();
}
