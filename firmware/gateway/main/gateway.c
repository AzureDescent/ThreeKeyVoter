/*
 * 网关 · ESP-NOW 接收 -> USB Serial 转发
 * ============================================================================
 * 负责方向：网关固件
 *
 * 当前能力：
 *   1) ESP-NOW 接收学生端的投票，按 JSON Lines 经 USB Serial 转发给 PC
 *   2) 可选：自己产生 Mock Data（单板调试用，见 GATEWAY_SELF_MOCK）
 *
 * 数据流（任务书第 2 节）：
 *   学生端 ESP32 --ESP-NOW--> 网关 ESP32 --USB Serial--> PC
 *
 * ---------------------------------------------------------------------------
 * 串口输出格式 —— PC 端解析必须知道
 * ---------------------------------------------------------------------------
 *   本工程同时往 USB Serial 写两类内容：
 *     1. ESP-IDF 日志行：形如  I (1234) gateway: ...
 *     2. 数据行：以 '{' 开头的纯 JSON，以 '\n' 结尾
 *
 *   PC 端解析的正确做法：**只处理以 '{' 开头的行，其余一律跳过。**
 *
 * ---------------------------------------------------------------------------
 * 关于拼 JSON
 * ---------------------------------------------------------------------------
 *   ESP-IDF v6 起 cJSON 已被移出 IDF、变成需要联网拉取的托管组件。
 *   这里要拼的 JSON 很简单，直接 snprintf 手拼，不要引入 cJSON。
 * ============================================================================
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"

static const char *TAG = "gateway";

/* ===========================================================================
 * 开关：网关要不要自己造假数据
 * ---------------------------------------------------------------------------
 *   0 = 只转发 ESP-NOW 收到的真实数据（**两块板联调时用这个**，当前默认）
 *   1 = 自己周期性产生 Mock Data（只有一块板、想单独验证 PC 通路时用）
 * =========================================================================== */
#define GATEWAY_SELF_MOCK   0

/* ESP-NOW 固定信道：所有节点必须一致，否则互相收不到 */
#define ESPNOW_CHANNEL      1

/* Mock 参数（仅 GATEWAY_SELF_MOCK=1 时有效） */
#define MOCK_NODE_COUNT     3
#define MOCK_INTERVAL_MS    1000

/* 无数据时的心跳日志间隔，用来确认板子还活着 */
#define IDLE_LOG_MS         10000

/* 与 docs/protocol.md 第 1 节的取值保持一致 */
static const char *VOTE_STR[3] = {
    "UNDERSTAND",
    "UNSURE",
    "NOT_UNDERSTAND",
};

/* 与网关共用的数据结构 —— **两边必须完全一致**。
 * 改动时三处一起改：gateway.c / student.c / docs/protocol.md */
typedef struct {
    uint8_t  node_id;
    char     vote[16];
    uint32_t sequence;
} __attribute__((packed)) vote_packet_t;

#if GATEWAY_SELF_MOCK
static uint32_t s_sequence[MOCK_NODE_COUNT] = { 0 };
#endif

static volatile uint32_t s_recv_count = 0;

/* 判断 vote 是否是协议里定义的合法值 */
static bool vote_is_valid(const char *vote)
{
    for (int i = 0; i < 3; i++) {
        if (strncmp(vote, VOTE_STR[i], sizeof(((vote_packet_t *)0)->vote)) == 0) {
            return true;
        }
    }
    return false;
}

/* 输出一行 JSON。Mock 和真实数据都走这里，保证 PC 端只认一种格式。 */
static void emit_json_line(unsigned node_id, const char *vote, unsigned sequence)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "{\"node_id\":%u,\"vote\":\"%s\",\"sequence\":%u}\n",
                     node_id, vote, sequence);
    if (n > 0 && n < (int)sizeof(buf)) {
        fwrite(buf, 1, (size_t)n, stdout);
        fflush(stdout);
    }
}

/* ---------------------------------------------------------------------------
 * ESP-NOW 接收回调
 * ------------------------------------------------------------------------ */
static void on_espnow_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < (int)sizeof(vote_packet_t)) {
        ESP_LOGW(TAG, "收到的数据长度异常: %d 字节（期望 %d）", len, (int)sizeof(vote_packet_t));
        return;
    }

    const vote_packet_t *pkt = (const vote_packet_t *)data;

    /* vote 是 char[16]，未必有结尾 0，校验时按定长比较 */
    char vote[sizeof(pkt->vote) + 1] = { 0 };
    memcpy(vote, pkt->vote, sizeof(pkt->vote));

    if (!vote_is_valid(vote)) {
        ESP_LOGW(TAG, "忽略无法识别的 vote: \"%s\"", vote);
        return;
    }

    s_recv_count++;

    /* 真实数据也输出成同样的 JSON 行，PC 端逻辑不用改 */
    emit_json_line(pkt->node_id, vote, (unsigned)pkt->sequence);

    const uint8_t *src = info->src_addr;
    int rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    ESP_LOGI(TAG, "ESP-NOW 收到 #%lu  来自 %02X:%02X:%02X:%02X:%02X:%02X  rssi=%d dBm",
             (unsigned long)s_recv_count,
             src[0], src[1], src[2], src[3], src[4], src[5], rssi);
}

/* ---------------------------------------------------------------------------
 * 无线初始化。刻意不用 ESP_ERROR_CHECK：失败只记警告，方便定位问题。
 * ------------------------------------------------------------------------ */
#define WIFI_TRY(expr)                                                      \
    do {                                                                    \
        esp_err_t e_ = (expr);                                              \
        if (e_ != ESP_OK) {                                                 \
            ESP_LOGW(TAG, "%s 失败: %s", #expr, esp_err_to_name(e_));       \
            return false;                                                   \
        }                                                                   \
    } while (0)

static bool wireless_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        WIFI_TRY(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init 失败: %s", esp_err_to_name(err));
        return false;
    }

    WIFI_TRY(esp_netif_init());
    WIFI_TRY(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_TRY(esp_wifi_init(&cfg));
    WIFI_TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    WIFI_TRY(esp_wifi_set_mode(WIFI_MODE_STA));
    WIFI_TRY(esp_wifi_start());
    WIFI_TRY(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    WIFI_TRY(esp_now_init());
    WIFI_TRY(esp_now_register_recv_cb(on_espnow_recv));

    return true;
}

#if GATEWAY_SELF_MOCK
static void emit_mock_line(int idx)
{
    if (idx < 0 || idx >= MOCK_NODE_COUNT) {
        return;
    }
    const char *vote = VOTE_STR[esp_random() % 3];
    emit_json_line((unsigned)(idx + 1), vote, (unsigned)s_sequence[idx]++);
}
#endif

void app_main(void)
{
#if GATEWAY_SELF_MOCK
    ESP_LOGI(TAG, "ThreeKeyVoter gateway —— 自产 Mock Data 模式");
#else
    ESP_LOGI(TAG, "ThreeKeyVoter gateway —— ESP-NOW 接收模式");
#endif

    /* MAC 先打出来：学生端要发给这个地址，且必须用同一信道。 */
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        ESP_LOGI(TAG, "=======================================================");
        ESP_LOGI(TAG, "Wi-Fi STA MAC : %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "ESP-NOW 信道  : %d", ESPNOW_CHANNEL);
        ESP_LOGI(TAG, "(学生端的 GATEWAY_MAC 必须填上面这个地址)");
        ESP_LOGI(TAG, "=======================================================");
    } else {
        ESP_LOGW(TAG, "读取 MAC 失败");
    }

    if (wireless_init()) {
        ESP_LOGI(TAG, "Wi-Fi + ESP-NOW 初始化完成，等待学生端数据…");
    } else {
        ESP_LOGW(TAG, "无线初始化失败");
    }
    ESP_LOGI(TAG, "PC 端解析规则：只处理以 '{' 开头的行");

    uint32_t last_count = 0;
    uint32_t idle_acc = 0;

    while (true) {
#if GATEWAY_SELF_MOCK
        for (int i = 0; i < MOCK_NODE_COUNT; i++) {
            emit_mock_line(i);
        }
        vTaskDelay(pdMS_TO_TICKS(MOCK_INTERVAL_MS));
#else
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* 没有数据时定期报个平安，否则串口一片安静会让人怀疑板子挂了 */
        idle_acc += 1000;
        if (idle_acc >= IDLE_LOG_MS) {
            idle_acc = 0;
            if (s_recv_count == last_count) {
                ESP_LOGI(TAG, "等待 ESP-NOW 数据…（累计收到 %lu 条）",
                         (unsigned long)s_recv_count);
            }
            last_count = s_recv_count;
        }
#endif
    }
}
