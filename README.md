# ThreeKeyVoter · 三键投票器

课堂实时反馈系统。学生用三个按键表达「理解 / 不确定 / 未理解」，教师端大屏实时显示统计结果。

## 系统架构

```text
学生端 ESP32-C3 × N              网关 ESP32-C3              PC               教室大屏
┌────────────┐                 ┌────────────┐         ┌────────────┐    ┌────────────┐
│  3 个按键   │                 │  ESP-NOW   │         │   读串口    │    │   浏览器    │
│  3 个 LED   │───ESP-NOW──────▶│  接收并转发  │──USB───▶│   解析并    │─HTTP▶│   实时显示  │
│            │    （无线）      │            │  串口    │  WebSocket  │    │            │
└────────────┘                 └────────────┘         └────────────┘    └────────────┘
                                （插在电脑上的）
```

> **插在电脑上的是网关。** 学生端的三个按键节点不接电脑，靠 ESP-NOW 无线把投票发给网关。

数据流经四个环节，对应下面四个目录：

| 环节 | 技术 | 目录 |
| --- | --- | --- |
| 按键采集 | ESP32-C3 + GPIO | `firmware/student/` |
| 无线汇聚 + USB 转发 | ESP-NOW + USB Serial | `firmware/gateway/` |
| 解析与对外提供数据 | 待定 | `backend/` |
| 界面展示 | 待定 | `frontend/` |

## 分工

| 负责方向 | 主要内容 | 目录 |
| --- | --- | --- |
| 学生端固件 | 按键 / LED / Wi-Fi / ESP-NOW 发送 | `firmware/student/` |
| 网关固件 + 结构 | ESP-NOW 接收 / USB Serial 转发 / SolidWorks | `firmware/gateway/` |
| PC 后端 | 读串口 / 解析 / 对外提供数据 | `backend/` |
| 前端 / UI | 教师端页面 | `frontend/` |

## 仓库结构

```text
ThreeKeyVoter/
├── firmware/
│   ├── student/          ESP-IDF 工程 — 学生端三键投票器
│   └── gateway/          ESP-IDF 工程 — 网关（ESP-NOW → USB Serial）
├── backend/              PC 端程序 — 读串口、解析、对外提供数据
├── frontend/             教师端页面
├── docs/
│   └── protocol.md       通信协议 v0（草案，尚未冻结）
├── project_task.md       任务书原文
├── .gitattributes
└── .gitignore
```

## 版本约定

| 项目 | 版本 | 说明 |
| --- | --- | --- |
| ESP-IDF | **v6.1** | 全队统一，不要各用各的 |
| 目标芯片 | `esp32c3` | 写在各工程的 `sdkconfig.defaults` 里，不用每次 set-target |

选 v6.1 的原因：它是当前最新稳定版，支持周期长；而且**内建 `idf.py mcp-server`（AI Agent 支持）只在 v6.0 及以上才存在**，v5.5 分支永远不会有（v5.5 是维护分支）。

## 当前状态（2026-10-09 实测）

| 项目 | 状态 |
| --- | --- |
| `firmware/student` 在 v6.1 下 `idf.py build` | ✅ 通过 |
| `firmware/gateway` 在 v6.1 下 `idf.py build` | ✅ 通过 |
| 两个固件烧录到 ESP32-C3 SuperMini（COM38） | ✅ 通过，flash 校验 OK |
| 串口输出（USB Serial/JTAG 控制台） | ✅ 见到 `I (5069) student: alive` 心跳日志 |
| `idf.py mcp-server` 子命令 | ✅ 已注册 |

也就是说：**环境、编译、烧录、串口四段全部打通**，可以直接开始写业务代码。

## 开发环境

### 1. 安装 ESP-IDF v6.1

如果你机器上还没有 ESP-IDF，二选一：

**方式 A：EIM（Espressif 官方安装器，推荐）**

```powershell
eim install -i v6.1
```

**方式 B：git（本仓库当前使用的方式）**

```powershell
git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git esp-idf-v6.1
cd esp-idf-v6.1
.\install.ps1 esp32c3        # 只装 ESP32-C3 需要的工具，省时间和磁盘
```

> 只装 `esp32c3` 一个 target 就够了，本项目只用 ESP32-C3。以后要用别的芯片，再跑一次 `install.ps1 <target>`。

> **国内网络必读**：安装器要从 GitHub 拉工具链，其中 `riscv32-esp-elf` 单个压缩包就有 **919 MB**。直连经常下到一半断掉（本机实测在 505 MB 处失败）。先设镜像环境变量，走 Espressif 国内 CDN：
>
> ```powershell
> $env:IDF_GITHUB_ASSETS = 'dl.espressif.cn/github_assets'
> .\install.ps1 esp32c3
> ```
>
> 本机实测：换国内镜像后 **4.7 分钟**装完。装完记得在同一终端 `export.ps1` 后验证 `idf.py --version` 输出 `ESP-IDF v6.1`。

### 2. 每次开新终端都必须先做这件事 ⚠️

`idf.py` **不在系统 PATH 上**。每开一个新终端，都要先激活 ESP-IDF 环境：

```powershell
. "$env:IDF_PATH\export.ps1"
```

看到 `Done! You can now compile ESP-IDF projects.` 就说明成功了。

如果你的机器上 `IDF_PATH` 没有设置，用实际路径代替：

```powershell
. "D:\Dev\Espressif\export.ps1"
```

> **不要手动把 `idf.py` 加进系统环境变量。** `idf.py` 只是入口，它还需要 `IDF_PATH`、`IDF_TOOLS_PATH`、工具链 PATH（`riscv32-esp-elf`、cmake、ninja…）和正确的 Python 虚拟环境。只把 `idf.py` 加进 PATH，`idf.py build` 时照样会报找不到工具链。

### 3. 本机参考环境（仅为示例，队友请按自己机器修改）

| 项目 | 路径 / 值 |
| --- | --- |
| ESP-IDF 源码 | `D:\Dev\Espressif`（v6.1） |
| 工具链目录（`IDF_TOOLS_PATH`） | `D:\Dev\Espressif-Tool\Espressif` |
| 编译器 | GCC 15.2（`riscv32-esp-elf` esp-15.2.0；v6.0 起从 14.2 升级） |
| 默认 C 标准 | `gnu23` |

## AI Agent 支持（可选）

ESP-IDF v6.0 起内建了一个本地 MCP server，让 AI 编程助手能直接驱动构建和烧录。

**启用**（在 ESP-IDF 源码目录执行一次）：

```powershell
.\install.ps1 --enable-mcp
```

**启动**：

```powershell
idf.py -C <项目目录> mcp-server
```

它暴露 5 个工具：`build` / `set-target` / `flash` / `create-project` / `clean`。

> ⚠️ **它没有串口监视器（monitor）功能** —— 官方只做到构建和烧录，看串口输出还是要自己跑 `idf.py monitor`。

## 硬件

**ESP32-C3 SuperMini** —— 以下为本仓库实测数据（2026-10-09）：

| 项目 | 值 |
| --- | --- |
| 芯片 | ESP32-C3 (QFN32)，revision v0.4 |
| Flash | 4 MB 嵌入式（XMC） |
| 晶振 | 40 MHz |
| USB | **USB-Serial/JTAG（原生 USB，GPIO18/19）** |
| 板载 LED | GPIO8（蓝灯，**低电平点亮**） |
| BOOT 键 | GPIO9 |
| 实测 MAC | `80:53:e0:a3:76:98` |
| 本机串口 | `COM38`（其余 COM 口是蓝牙虚拟串口，别选错） |

因为 USB-C 走的是原生 USB Serial/JTAG，两个工程的 `sdkconfig.defaults` 已经把控制台配成 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`，`idf.py monitor` 直接就能看到输出。

## 编译 / 烧录 / 监视

```powershell
. "$env:IDF_PATH\export.ps1"     # 每个新终端都要先做一次

cd firmware\student
idf.py build
idf.py -p COM38 flash monitor    # 退出 monitor 按 Ctrl+]
```

网关工程把 `student` 换成 `gateway` 即可。

> **烧录提示**：v6.1 起 `idf.py flash` 默认是「快速重刷」——只写变化的 flash 区域，写完会校验，校验失败才全量重写。平时开发这么用很快；但**换新板子、芯片被擦干净、或者烧录行为可疑时**，用 `idf.py -p COM38 flash -a monitor` 强制全量写入。

## 写代码时的两个 v6 注意事项

1. **驱动头文件不再自动包含 FreeRTOS 头。**
   只 `#include "driver/gpio.h"` 然后调用 `vTaskDelay()` 会编译失败。必须显式：

   ```c
   #include "freertos/FreeRTOS.h"
   #include "freertos/task.h"
   ```

   （`main/student.c`、`main/gateway.c` 骨架里已经写好了。）

2. **cJSON 已被移出 ESP-IDF**，变成了需要联网拉取的托管组件。
   本项目要拼的 JSON 非常简单，**直接用 `snprintf` 手拼就行，不要引入 cJSON**：

   ```c
   snprintf(buf, sizeof(buf), "{\"node_id\":%d,\"vote\":\"%s\",\"sequence\":%d}\n",
            node_id, vote_str, sequence);
   ```

3. 网上大量教程是 v4/v5 时代的。照抄时注意这两个常见坑：`ESP_IF_WIFI_STA` 宏在 v6 已删除（改用 `WIFI_IF_STA`）；ESP-NOW 的 `esp_wifi_config_espnow_rate` 已改为 `esp_now_set_peer_rate_config`。

## Git 工作流

- `main` 是稳定分支，**不要直接往 `main` 推**
- 每个功能开一个分支：`feat/student-button-demo`、`fix/gateway-serial-crash`
- 完成后开 Pull Request，至少一人 review 再合并

```powershell
git checkout main
git pull
git checkout -b feat/your-feature
# ...写代码...
git add .
git commit -m "feat(student): 三按键输入 demo"
git push -u origin feat/your-feature
```

首次 clone：

```powershell
git clone https://github.com/AzureDescent/ThreeKeyVoter.git
cd ThreeKeyVoter
```

## 常见问题

**`idf.py` 不是内部或外部命令**
→ 忘了激活环境，回到上面执行 `export.ps1`。

**`idf.py monitor` 没有任何输出**
→ 先确认端口选的是 `COM38`（别选成蓝牙虚拟串口）。本仓库两个工程的控制台已按实测结果配置好。

**提示串口被占用 / 无法打开**
→ 关掉其它正在 `monitor` 的窗口。

**VS Code 的 ESP-IDF 插件报找不到工具链**
→ 插件里把 IDF 路径设为 `D:\Dev\Espressif`、工具路径设为 `D:\Dev\Espressif-Tool\Espressif`，版本选 v6.1。

**编译出现一堆 GCC 15 的新警告**
→ 正常。v6.0 把 GCC 从 14.2 升到 15.1，v6.1 实际是 15.2，新增了警告。能改就改；确认是误报时可用 `CONFIG_COMPILER_DISABLE_GCC15_WARNINGS` 整体压制。

**`export.ps1` 提示 "Found tools that are not used by active ESP-IDF version"**
→ 说明机器上还留着旧版工具链（本机就留有 v5.5 的 cmake / riscv32-esp-elf 等，约几 GB）。不影响使用，想清理可以跑：
>
> ```powershell
> python "$env:IDF_PATH\tools\idf_tools.py" uninstall
> ```

## 任务书

见 [project_task.md](project_task.md)。
