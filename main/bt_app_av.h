#ifndef __BT_APP_AV_H__
#define __BT_APP_AV_H__

#include <stdint.h>
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

/* log tags */
#define BT_AV_TAG       "BT_AV"
#define BT_RC_TG_TAG    "RC_TG"
#define BT_RC_CT_TAG    "RC_CT"

/**
 * @brief  A2DP sink回调函数
 *
 * @param [in] event  事件id
 * @param [in] param  回调参数
 */
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

/**
 * @brief  A2DP sink 音频数据流回调函数
 *
 * @param [out] data  被写入应用任务的数据流
 * @param [in]  len   数据流的比特长度
 */
void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len);

/**
 * @brief  AVRCP控制器回调函数
 *
 * @param [in] event  事件id
 * @param [in] param  回调参数
 */
void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

/**
 * @brief  AVRCP目标回调函数
 *
 * @param [in] event  事件id
 * @param [in] param  回调参数
 */
void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param);

#endif /* __BT_APP_AV_H__*/
