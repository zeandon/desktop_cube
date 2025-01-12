#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

static const int RX_BUF_SIZE = 1024;

#define TXD_PIN     GPIO_NUM_23
#define RXD_PIN     GPIO_NUM_22

/*
 * 简介：  uart初始化函数
 * 参数：  无
 * 返回值：无
 */

void uart_init(void)
{
    // uart参数配置
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // We won't use a buffer for sending data.
    // uart驱动安装（初始化）
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/*
 * 简介：  UART发送字符串函数
 * 参数：  string  串口发送的数据
 * 返回值：无
 */

void uart_send_string(const char* string)
{
    const int len = strlen(string);
    uart_write_bytes(UART_NUM_1, string, len);
}

/*
 * 简介：  UART发送整数函数
 * 参数：  num  串口发送的整数
 * 返回值：无
 */

void uart_send_num(int num)
{
    // int转字符串
    char num_string[3] = {0};
    itoa(num,num_string,10);
    
    // 转化完成后按照字符串格式发送
    uart_send_string(num_string);
}
