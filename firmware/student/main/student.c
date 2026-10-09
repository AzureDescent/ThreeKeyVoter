/*
 * 学生端 · 三键投票器 —— 骨架工程
 * ============================================================================
 * 负责方向：学生端固件
 *
 * 目标（任务书第 2 节）：
 *   Button 1 -> UNDERSTAND
 *   Button 2 -> UNSURE
 *   Button 3 -> NOT_UNDERSTAND
 *
 *   按下按钮后：
 *     - 对应 LED 给出反馈
 *     - 串口打印对应状态，例如：
 *         Button 1 pressed
 *         State = UNDERSTAND
 *   重点不是界面，而是搞清楚：GPIO 输入 / GPIO 输出 / 按键读取 / 基本消抖 / 串口调试
 *
 * 待实现（TODO）：
 *   [ ] GPIO 输入配置（按键；注意上拉/下拉，悬空会乱跳）
 *   [ ] GPIO 输出配置（LED）
 *   [ ] 按键扫描 + 基本消抖
 *   [ ] Wi-Fi Station 模式，连上后打印 "Connected" 和 IP
 *   [ ] ESP-NOW 发送 { node_id, vote, sequence } 给网关（协议见 docs/protocol.md）
 *   [ ] 把 vote 状态做成三态机，而不是三个独立的 if
 *
 * 已实测的板级信息（详见仓库根目录 README「硬件」章节）：
 *   - 板载 LED：GPIO8，蓝灯，低电平点亮（gpio_set_level(8, 0) 是亮）
 *   - BOOT 键：GPIO9
 *   - 控制台：USB Serial/JTAG（USB-C 直连原生 USB，插在 COM38）
 *   - 按键接哪几个脚由你自己定，定下来后写进本文件顶部注释和 README
 * ============================================================================
 */

#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "student";

void app_main(void)
{
    ESP_LOGI(TAG, "ThreeKeyVoter student terminal");
    ESP_LOGI(TAG, "skeleton booted. TODO: GPIO 按键 / LED / Wi-Fi / ESP-NOW");

    /* 心跳：用来确认串口通路正常，实现完按键逻辑后可以删掉 */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive");
    }
}
