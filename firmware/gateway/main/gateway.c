/*
 * 网关 · ESP-NOW 接收 -> USB Serial 转发 —— 骨架工程
 * ============================================================================
 * 负责方向：网关固件
 *
 * 目标（任务书第 1 节）：先完全不用等学生端。
 *   让 ESP32 自己周期性产生 Mock Data：
 *       {"node_id":1,"vote":"UNDERSTAND"}
 *   经 USB Serial 发给 PC，PC 上的程序读出来打印：
 *       Node 1: UNDERSTAND
 *       Node 2: UNSURE
 *   验收：ESP32 周期性产生 Mock Data，电脑程序可以稳定读取并打印。
 *
 * 之后（任务书第 2 节）换成真正的 Gateway：
 *   学生端 ESP32 --ESP-NOW--> 网关 ESP32 --USB Serial--> PC
 *
 * 关于拼 JSON（v6.1 注意）：
 *   ESP-IDF v6 起 cJSON 已被移出 IDF、改成需要联网拉取的托管组件。
 *   本项目要输出的 JSON 很简单，直接用 snprintf 手拼即可，不要引入 cJSON：
 *     snprintf(buf, sizeof(buf), "{\"node_id\":%d,\"vote\":\"%s\",\"sequence\":%d}\n",
 *              node_id, vote_str, sequence);
 *   注意两点：引号要转义（\"），缓冲区要记得给结尾的 '\n' 留位置。
 *
 * 待实现（TODO）：
 *   [ ] 周期性产生 Mock Data，按 JSON Lines 输出（一行一条，\n 结尾）
 *   [ ] Wi-Fi Station 模式（ESP-NOW 需要先初始化 Wi-Fi）
 *   [ ] ESP-NOW 接收学生端数据
 *   [ ] 把收到的数据原样转发到 USB Serial
 *   [ ] 处理 PC 端断开重连（不要因为写串口失败就崩）
 *
 * 已实测的板级信息（详见仓库根目录 README「硬件」章节）：
 *   - 控制台：USB Serial/JTAG，本机枚举为 COM38
 *   - 板载 LED：GPIO8，低电平点亮
 *   - 实测 MAC：80:53:e0:a3:76:98（ESP-NOW 配对时会用到）
 * ============================================================================
 */

#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "gateway";

void app_main(void)
{
    ESP_LOGI(TAG, "ThreeKeyVoter gateway");
    ESP_LOGI(TAG, "skeleton booted. TODO: Mock Data / Wi-Fi / ESP-NOW / 串口转发");

    /* 心跳：用来确认串口通路正常，实现完 Mock Data 后可以删掉 */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive");
    }
}
