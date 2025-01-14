#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

#include "bt_app_core.h"
#include "bt_app_av.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC     // 选择DAC输出（这里不用）
#include "driver/dac_continuous.h"
#else
#include "driver/i2s_std.h"
#endif

#include "sys/lock.h"

/* AVRCP used transaction labels */         // AVRCP的事务标签定义
#define APP_RC_CT_TL_GET_CAPS            (0)
#define APP_RC_CT_TL_GET_META_DATA       (1)
#define APP_RC_CT_TL_RN_TRACK_CHANGE     (2)
#define APP_RC_CT_TL_RN_PLAYBACK_CHANGE  (3)
#define APP_RC_CT_TL_RN_PLAY_POS_CHANGE  (4)

/* Application layer causes delay value */  // 应用层导致的延迟
#define APP_DELAY_VALUE                  50  // 5ms

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

// 在蓝牙技术中，track 通常指的是音频或视频内容中的一个特定部分，例如一首歌曲、一个视频片段或一个音频章节

/* allocate new meta buffer */
static void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t *param);    // 分配新的元数据缓冲区
/* handler for new track is loaded */
static void bt_av_new_track(void);      // 新track装载的处理函数
/* handler for track status change */
static void bt_av_playback_changed(void);       // track状态转换的处理函数
/* handler for track playing position change */
static void bt_av_play_pos_changed(void);       // track播放位置变换的处理函数
/* notification event handler */
static void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter);   // 通知事件处理函数
/* installation for i2s */
static void bt_i2s_driver_install(void);        // I2S驱动安装
/* uninstallation for i2s */
static void bt_i2s_driver_uninstall(void);      // I2S驱动卸载
/* set volume by remote controller */
static void volume_set_by_controller(uint8_t volume);   // 远程控制器进行音量设置
/* set volume by local host */
static void volume_set_by_local_host(uint8_t volume);   // 本地主机进行音量设置
/* simulation volume change */
static void volume_change_simulation(void *arg);        // 模拟音量变化
/* a2dp event handler */
static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param);   // A2DP事件处理函数
/* avrc controller event handler */
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param);   // AVRC控制器事件处理函数
/* avrc target event handler */
static void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param);   // AVRC目标事件处理函数

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/

static uint32_t s_pkt_cnt = 0;               /* count for audio packet */   // 用于计数audio包
static esp_a2d_audio_state_t s_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;   // audio流数据路径状态
                                             /* audio stream datapath state */  
static const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};   // 连接状态
                                             /* connection state in string */   
static const char *s_a2d_audio_state_str[] = {"Suspended", "Started"};      // 数据路径状态
                                             /* audio stream datapath state in string */
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;       // AVRC目标通知能力位码
                                             /* AVRC target notification capability bit mask */
static _lock_t s_volume_lock;
static TaskHandle_t s_vcs_task_hdl = NULL;    /* handle for volume change simulation task */    // 模拟音量变化任务
static uint8_t s_volume = 0;                 /* local volume value */       // 本地主机音量
static bool s_volume_notify;                 /* notify volume change or not */      // 通知音量是否改变

// 选择输出方式的处理函数
#ifndef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
i2s_chan_handle_t tx_chan = NULL;
#else
dac_continuous_handle_t tx_chan;
#endif

/********************************
 * STATIC FUNCTION DEFINITIONS
 *******************************/

// 分配新的元数据缓冲区
// 元数据是关于数据的数据，用于描述音频文件的属性和特征（作家等信息）
static void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t *param)
{
    // 传参
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);
    // 分配内存
    uint8_t *attr_text = (uint8_t *) malloc (rc->meta_rsp.attr_length + 1);
    // 复制数据
    memcpy(attr_text, rc->meta_rsp.attr_text, rc->meta_rsp.attr_length);
    // 设置终止字符
    attr_text[rc->meta_rsp.attr_length] = 0;
    // 更新指针
    rc->meta_rsp.attr_text = attr_text;
}

// 新track装载的处理函数
static void bt_av_new_track(void)
{
    /* request metadata */  //请求元数据（歌曲标题、艺术家、专辑和流派）
    uint8_t attr_mask = ESP_AVRC_MD_ATTR_TITLE |
                        ESP_AVRC_MD_ATTR_ARTIST |
                        ESP_AVRC_MD_ATTR_ALBUM |
                        ESP_AVRC_MD_ATTR_GENRE;
    // 发送请求命令函数
    esp_avrc_ct_send_metadata_cmd(APP_RC_CT_TL_GET_META_DATA, attr_mask);

    /* register notification if peer support the event_id */    // 注册通知
    // 检查对端设备是否支持特定的通知事件
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_TRACK_CHANGE)) {
        // 发送注册通知命令的函数
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_TRACK_CHANGE,
                                                   ESP_AVRC_RN_TRACK_CHANGE, 0);
    }
}

// track状态转换的处理函数
// 当播放状态（如播放、暂停、停止）发生变化时，这个函数会被调用
static void bt_av_playback_changed(void)
{
    /* register notification if peer support the event_id */
    // 检查对端设备是否支持特定的通知事件
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_PLAY_STATUS_CHANGE)) {
        // 发送注册通知命令的函数
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAYBACK_CHANGE,
                                                   ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
    }
}

// track播放位置变换的处理函数
static void bt_av_play_pos_changed(void)
{
    /* register notification if peer support the event_id */
    // 检查对端设备是否支持特定的通知事件
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,
                                           ESP_AVRC_RN_PLAY_POS_CHANGED)) {
        // 发送注册通知命令的函数
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAY_POS_CHANGE,
                                                   ESP_AVRC_RN_PLAY_POS_CHANGED, 10);
    }
}

// 通知事件处理函数
static void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter)
{
    // 判断事件ID
    switch (event_id) {
    /* when new track is loaded, this event comes */
    // 加载新曲目
    case ESP_AVRC_RN_TRACK_CHANGE:
        bt_av_new_track();
        break;
    /* when track status changed, this event comes */
    // 播放状态变化
    case ESP_AVRC_RN_PLAY_STATUS_CHANGE:
        ESP_LOGI(BT_AV_TAG, "Playback status changed: 0x%x", event_parameter->playback);
        bt_av_playback_changed();
        break;
    /* when track playing position changed, this event comes */
    // 播放位置变化
    case ESP_AVRC_RN_PLAY_POS_CHANGED:
        ESP_LOGI(BT_AV_TAG, "Play position changed: %"PRIu32"-ms", event_parameter->play_pos);
        bt_av_play_pos_changed();
        break;
    /* others */
    // 其他事件不进行处理
    default:
        ESP_LOGI(BT_AV_TAG, "unhandled event: %d", event_id);
        break;
    }
}

// I2S驱动安装
void bt_i2s_driver_install(void)
{
// 选择DAC输出（不用）
#ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
    dac_continuous_config_t cont_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_ALL,
        .desc_num = 8,
        .buf_size = 2048,
        .freq_hz = 44100,
        .offset = 127,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,   // Using APLL as clock source to get a wider frequency range
        .chan_mode = DAC_CHANNEL_MODE_ALTER,
    };
    /* Allocate continuous channels */
    ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &tx_chan));
    /* Enable the continuous channels */
    ESP_ERROR_CHECK(dac_continuous_enable(tx_chan));
#else   // I2S输出
    // 初始化配置
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    // 设置I2S配置
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_EXAMPLE_I2S_BCK_PIN,
            .ws = CONFIG_EXAMPLE_I2S_LRCK_PIN,
            .dout = CONFIG_EXAMPLE_I2S_DATA_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* enable I2S */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_chan, NULL));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
#endif
}

// I2S驱动卸载
void bt_i2s_driver_uninstall(void)
{
#ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
    ESP_ERROR_CHECK(dac_continuous_disable(tx_chan));
    ESP_ERROR_CHECK(dac_continuous_del_channels(tx_chan));
#else
    ESP_ERROR_CHECK(i2s_channel_disable(tx_chan));
    ESP_ERROR_CHECK(i2s_del_channel(tx_chan));
#endif
}

// 远程控制器进行音量设置
static void volume_set_by_controller(uint8_t volume)
{
    // 日志
    ESP_LOGI(BT_RC_TG_TAG, "Volume is set by remote controller to: %"PRIu32"%%", (uint32_t)volume * 100 / 0x7f);
    /* set the volume in protection of lock */      // 在锁保护下进行音量设置
    _lock_acquire(&s_volume_lock);
    s_volume = volume;
    _lock_release(&s_volume_lock);
}

// 本地主机进行音量设置

static void volume_set_by_local_host(uint8_t volume)
{
    ESP_LOGI(BT_RC_TG_TAG, "Volume is set locally to: %"PRIu32"%%", (uint32_t)volume * 100 / 0x7f);
    /* set the volume in protection of lock */      // 在锁保护下进行音量设置
    _lock_acquire(&s_volume_lock);
    s_volume = volume;
    _lock_release(&s_volume_lock);

    /* send notification response to remote AVRCP controller */     // 发送通知给ARVCP控制器
    if (s_volume_notify) {
        esp_avrc_rn_param_t rn_param;
        rn_param.volume = s_volume;
        esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn_param);
        s_volume_notify = false;
    }
}

// 模拟音量变化任务
static void volume_change_simulation(void *arg)
{
    ESP_LOGI(BT_RC_TG_TAG, "start volume change simulation");

    for (;;) {
        /* volume up locally every 10 seconds */
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        // uint8_t volume = (s_volume + 5) & 0x7f;
        // volume_set_by_local_host(volume);
    }
}

// A2DP事件处理函数
static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param)
{
    // 日志打印事件id
    ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);

    esp_a2d_cb_param_t *a2d = NULL;

    // 判断事件，进行具体操作
    switch (event) {
    /* when connection state changed, this event comes */
    // 连接状态改变
    case ESP_A2D_CONNECTION_STATE_EVT: {
        // 记录连接状态和对端蓝牙地址
        a2d = (esp_a2d_cb_param_t *)(p_param);
        uint8_t *bda = a2d->conn_stat.remote_bda;
        ESP_LOGI(BT_AV_TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
            s_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

        // 断开连接状态下，设置为可被发现，卸载I2S驱动和任务
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            bt_i2s_driver_uninstall();
            bt_i2s_task_shut_down();
        } 
        // 连接状态下，设置为不可被发现，启动I2S任务
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED){
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            bt_i2s_task_start_up();
        } 
        // 正在连接状态，安装I2S驱动
        else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
            bt_i2s_driver_install();
        }
        break;
    }
    /* when audio stream transmission state changed, this event comes */
    // 音频流传输状态变换事件
    case ESP_A2D_AUDIO_STATE_EVT: {
        // 日志打印音频流状态
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_AV_TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);
        s_audio_state = a2d->audio_stat.state;
        if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
            s_pkt_cnt = 0;      // 重置包计数器
        }
        break;
    }
    /* when audio codec is configured, this event comes */
    // 音频配置事件
    case ESP_A2D_AUDIO_CFG_EVT: {
        // 日志打印音频流配置状态
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_AV_TAG, "A2DP audio stream configuration, codec type: %d", a2d->audio_cfg.mcc.type);
        /* for now only SBC stream is supported */      // 解析SBC编码方式，配置采样率和声道数
        if (a2d->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            int sample_rate = 16000;        // 采样率
            int ch_count = 2;       // 声道数
            char oct0 = a2d->audio_cfg.mcc.cie.sbc[0];
            if (oct0 & (0x01 << 6)) {
                sample_rate = 32000;
            } else if (oct0 & (0x01 << 5)) {
                sample_rate = 44100;
            } else if (oct0 & (0x01 << 4)) {
                sample_rate = 48000;
            }

            if (oct0 & (0x01 << 3)) {
                ch_count = 1;
            }
        #ifdef CONFIG_EXAMPLE_A2DP_SINK_OUTPUT_INTERNAL_DAC
            dac_continuous_disable(tx_chan);
            dac_continuous_del_channels(tx_chan);
            dac_continuous_config_t cont_cfg = {
                .chan_mask = DAC_CHANNEL_MASK_ALL,
                .desc_num = 8,
                .buf_size = 2048,
                .freq_hz = sample_rate,
                .offset = 127,
                .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,   // Using APLL as clock source to get a wider frequency range
                .chan_mode = (ch_count == 1) ? DAC_CHANNEL_MODE_SIMUL : DAC_CHANNEL_MODE_ALTER,
            };
            /* Allocate continuous channels */
            dac_continuous_new_channels(&cont_cfg, &tx_chan);
            /* Enable the continuous channels */
            dac_continuous_enable(tx_chan);
        #else   // I2S输出
            // 配置并且启用I2S通道
            i2s_channel_disable(tx_chan);
            i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
            i2s_std_slot_config_t slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, ch_count);
            i2s_channel_reconfig_std_clock(tx_chan, &clk_cfg);
            i2s_channel_reconfig_std_slot(tx_chan, &slot_cfg);
            i2s_channel_enable(tx_chan);
        #endif
            // 日志输出配置信息
            ESP_LOGI(BT_AV_TAG, "Configure audio player: %x-%x-%x-%x",
                     a2d->audio_cfg.mcc.cie.sbc[0],
                     a2d->audio_cfg.mcc.cie.sbc[1],
                     a2d->audio_cfg.mcc.cie.sbc[2],
                     a2d->audio_cfg.mcc.cie.sbc[3]);
            ESP_LOGI(BT_AV_TAG, "Audio player configured, sample rate: %d", sample_rate);
        }
        break;
    }
    /* when a2dp init or deinit completed, this event comes */
    // A2DP初始化或去初始化完成事件
    case ESP_A2D_PROF_STATE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        if (ESP_A2D_INIT_SUCCESS == a2d->a2d_prof_stat.init_state) {
            ESP_LOGI(BT_AV_TAG, "A2DP PROF STATE: Init Complete");
        } else {
            ESP_LOGI(BT_AV_TAG, "A2DP PROF STATE: Deinit Complete");
        }
        break;
    }
    /* When protocol service capabilities configured, this event comes */
    // 协议能力服务配置
    case ESP_A2D_SNK_PSC_CFG_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_AV_TAG, "protocol service capabilities configured: 0x%x ", a2d->a2d_psc_cfg_stat.psc_mask);
        if (a2d->a2d_psc_cfg_stat.psc_mask & ESP_A2D_PSC_DELAY_RPT) {
            ESP_LOGI(BT_AV_TAG, "Peer device support delay reporting");
        } else {
            ESP_LOGI(BT_AV_TAG, "Peer device unsupport delay reporting");
        }
        break;
    }
    /* when set delay value completed, this event comes */
    // 设置延迟值事件
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        if (ESP_A2D_SET_INVALID_PARAMS == a2d->a2d_set_delay_value_stat.set_state) {
            ESP_LOGI(BT_AV_TAG, "Set delay report value: fail");
        } else {
            ESP_LOGI(BT_AV_TAG, "Set delay report value: success, delay_value: %u * 1/10 ms", a2d->a2d_set_delay_value_stat.delay_value);
        }
        break;
    }
    /* when get delay value completed, this event comes */
    // 获取延迟值事件
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT: {
        a2d = (esp_a2d_cb_param_t *)(p_param);
        ESP_LOGI(BT_AV_TAG, "Get delay report value: delay_value: %u * 1/10 ms", a2d->a2d_get_delay_value_stat.delay_value);
        /* Default delay value plus delay caused by application layer */
        esp_a2d_sink_set_delay_value(a2d->a2d_get_delay_value_stat.delay_value + APP_DELAY_VALUE);
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

// AVRC控制器事件处理函数
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param)
{
    // 日志打印事件
    ESP_LOGD(BT_RC_CT_TAG, "%s event: %d", __func__, event);

    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(p_param);

    // 根据事件进行操作
    switch (event) {
    /* when connection state changed, this event comes */
    // 连接状态改变事件
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        // 记录连接状态和对端蓝牙地址
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGI(BT_RC_CT_TAG, "AVRC conn_state event: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        // 连接状态下，发送获取对端设备支持的通知事件ID的命令
        if (rc->conn_stat.connected) {
            /* get remote supported event_ids of peer AVRCP Target */
            esp_avrc_ct_send_get_rn_capabilities_cmd(APP_RC_CT_TL_GET_CAPS);
        } else {
            /* clear peer notification capability record */     // 清除标志位
            s_avrc_peer_rn_cap.bits = 0;
        }
        break;
    }
    /* when passthrough responsed, this event comes */
    // 通过响应事件
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
        // 记录通过命令的键码、键状态和响应代码
        ESP_LOGI(BT_RC_CT_TAG, "AVRC passthrough rsp: key_code 0x%x, key_state %d, rsp_code %d", rc->psth_rsp.key_code,
                    rc->psth_rsp.key_state, rc->psth_rsp.rsp_code);
        break;
    }
    /* when metadata responsed, this event comes */
    // 元数据响应事件
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        // 记录元数据的属性ID和文本内容
        ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: attribute id 0x%x, %s", rc->meta_rsp.attr_id, rc->meta_rsp.attr_text);
        // 释放元数据文本的内存
        free(rc->meta_rsp.attr_text);
        break;
    }
    /* when notified, this event comes */
    // 通知事件
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC event notification: %d", rc->change_ntf.event_id);
        bt_av_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
        break;
    }
    /* when feature of remote device indicated, this event comes */
    // 远程设备特性
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        // 记录远程设备的特性掩码和目标设备的特性标志
        ESP_LOGI(BT_RC_CT_TAG, "AVRC remote features %"PRIx32", TG features %x", rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag);
        break;
    }
    /* when notification capability of peer device got, this event comes */
    // 通知能力响应事件
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        // 记录通知能力的计数和位掩码
        ESP_LOGI(BT_RC_CT_TAG, "remote rn_cap: count %d, bitmask 0x%x", rc->get_rn_caps_rsp.cap_count,
                 rc->get_rn_caps_rsp.evt_set.bits);
        // 更新对端设备的通知能力记录
        s_avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;
        // 初始化通知
        bt_av_new_track();
        bt_av_playback_changed();
        bt_av_play_pos_changed();
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_RC_CT_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

// AVRC目标事件处理函数
static void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param)
{
    // 日志打印事件
    ESP_LOGD(BT_RC_TG_TAG, "%s event: %d", __func__, event);

    esp_avrc_tg_cb_param_t *rc = (esp_avrc_tg_cb_param_t *)(p_param);

    // 根据事件进行操作
    switch (event) {
    /* when connection state changed, this event comes */
    // 连接状态改变事件
    case ESP_AVRC_TG_CONNECTION_STATE_EVT: {
        // 记录连接状态和对端设备的蓝牙地址
        uint8_t *bda = rc->conn_stat.remote_bda;
        ESP_LOGI(BT_RC_TG_TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        if (rc->conn_stat.connected) {
            /* create task to simulate volume change */
            // 创建一个任务来模拟音量变化
            xTaskCreate(volume_change_simulation, "vcsTask", 2048, NULL, 5, &s_vcs_task_hdl);
        } else {
            // 删除音量变化模拟任务
            vTaskDelete(s_vcs_task_hdl);
            ESP_LOGI(BT_RC_TG_TAG, "Stop volume change simulation");
        }
        break;
    }
    /* when passthrough commanded, this event comes */
    // 通过命令事件
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT: {
        // 记录通过命令的键码和键状态
        ESP_LOGI(BT_RC_TG_TAG, "AVRC passthrough cmd: key_code 0x%x, key_state %d", rc->psth_cmd.key_code, rc->psth_cmd.key_state);
        break;
    }
    /* when absolute volume command from remote device set, this event comes */
    // 绝对音量命令
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
        // 记录音量
        ESP_LOGI(BT_RC_TG_TAG, "AVRC set absolute volume: %d%%", (int)rc->set_abs_vol.volume * 100 / 0x7f);
        // 设置音量
        volume_set_by_controller(rc->set_abs_vol.volume);
        break;
    }
    /* when notification registered, this event comes */
    // 通知注册事件
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
        // 记录注册的通知事件ID和参数
        ESP_LOGI(BT_RC_TG_TAG, "AVRC register event notification: %d, param: 0x%"PRIx32, rc->reg_ntf.event_id, rc->reg_ntf.event_parameter);
        // 如果注册的事件是音量变化通知，设置通知标志并发送临时响应
        if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            s_volume_notify = true;
            esp_avrc_rn_param_t rn_param;
            rn_param.volume = s_volume;
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn_param);
        }
        break;
    }
    /* when feature of remote device indicated, this event comes */
    // 远程设备特性事件
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT: {
        // 记录远程设备的特性掩码和控制器特性标志
        ESP_LOGI(BT_RC_TG_TAG, "AVRC remote features: %"PRIx32", CT features: %x", rc->rmt_feats.feat_mask, rc->rmt_feats.ct_feat_flag);
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_RC_TG_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

/**
 * @brief  A2DP sink回调函数
 *
 * @param [in] event  事件id
 * @param [in] param  回调参数
 */

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_PROF_STATE_EVT:
    case ESP_A2D_SNK_PSC_CFG_EVT:
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT: {
        // 如果是以上几种事件，将事件和处理函数派发到应用程序任务的工作队列中
        bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param, sizeof(esp_a2d_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "Invalid A2DP event: %d", event);
        break;
    }
}

/**
 * @brief  A2DP sink 音频数据流回调函数
 *
 * @param [out] data  被写入应用任务的数据流
 * @param [in]  len   数据流的比特长度
 */

void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    // 将数据写入ringbuffer
    write_ringbuf(data, len);

    /* log the number every 100 packets */  //每100个包进行一次日志打印
    if (++s_pkt_cnt % 100 == 0) {
        ESP_LOGI(BT_AV_TAG, "Audio packet count: %"PRIu32, s_pkt_cnt);
    }
}

/**
 * @brief  AVRCP控制器回调函数
 *
 * @param [in] event  事件id
 * @param [in] param  回调参数
 */

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_METADATA_RSP_EVT:
        bt_app_alloc_meta_buffer(param);
        /* fall through */
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        // 如果是以上几种事件，将事件和处理函数派发到应用程序任务的工作队列中
        bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

/**
 * @brief  AVRCP目标回调函数
 *
 * @param [in] event  事件id
 * @param [in] param  回调参数
 */

void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
    case ESP_AVRC_TG_SET_PLAYER_APP_VALUE_EVT:
        // 如果是以上几种事件，将事件和处理函数派发到应用程序任务的工作队列中
        bt_app_work_dispatch(bt_av_hdl_avrc_tg_evt, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL);
        break;
    default:
        ESP_LOGE(BT_RC_TG_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}
