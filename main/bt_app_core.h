#ifndef __BT_APP_CORE_H__
#define __BT_APP_CORE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* log tag */
#define BT_APP_CORE_TAG    "BT_APP_CORE"

/* signal for `bt_app_work_dispatch` */
// bt_app_work_dispatch 函数用于将事件和处理函数派发到应用程序任务的工作队列中
#define BT_APP_SIG_WORK_DISPATCH    (0x01)

/**
 * @brief  dispatched work 的处理函数
 *
 * @param [in] event  事件id
 * @param [in] param  处理参数
 */
typedef void (* bt_app_cb_t) (uint16_t event, void *param);

/* message to be sent */
// 发送信号的结构体定义
typedef struct {
    uint16_t       sig;      /*!< signal to bt_app_task */              // 通知应用程序的信号
    uint16_t       event;    /*!< message event id */                   // 消息的事件id
    bt_app_cb_t    cb;       /*!< context switch callback */            // 回调函数指针
    void           *param;   /*!< parameter area needs to be last */    // 参数指针，传递给回调函数的指针
} bt_app_msg_t;

/**
 * @brief  parameter deep-copy function to be customized（可定制的参数深拷贝函数）
 *
 * @param [out] p_dest  pointer to destination data（目标数据的指针）
 * @param [in]  p_src   pointer to source data（源数据的指针）
 * @param [in]  len     data length in byte（数据长度，以字节为单位）
 */
typedef void (* bt_app_copy_cb_t) (void *p_dest, void *p_src, int len);

/**
 * @brief  work dispatcher for the application task（将事件和处理函数派发到应用程序的工作队列中）
 *
 * @param [in] p_cback       callback function              // 回调函数
 * @param [in] event         event id                       // 事件id
 * @param [in] p_params      callback paramters             // 回调参数
 * @param [in] param_len     parameter length in byte       // 参数长度
 * @param [in] p_copy_cback  parameter deep-copy function   // 参数深拷贝函数
 *
 * @return  true if work dispatch successfully, false otherwise（如果派发成功返回ture）
 */
bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback);

/**
 * @brief  开始应用任务
 */
void bt_app_task_start_up(void);

/**
 * @brief  关闭应用任务
 */
void bt_app_task_shut_down(void);

/**
 * @brief  开始I2S任务
 */
void bt_i2s_task_start_up(void);

/**
 * @brief  关闭I2S任务
 */
void bt_i2s_task_shut_down(void);

/**
 * @brief  将数据写入ringbuffer中
 *
 * @param [in] data  数据流的指针
 * @param [in] size  数据长度
 *
 * @return size if writteen ringbuffer successfully, 0 others（写入ringbuffer的数据长度）
 */
size_t write_ringbuf(const uint8_t *data, size_t size);

#endif /* __BT_APP_CORE_H__ */
