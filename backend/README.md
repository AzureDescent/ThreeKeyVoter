# backend — PC 端程序

**负责方向：PC 后端**（前期的串口读取由网关固件侧打通）

## 这一层要做什么

1. 从网关 ESP32 的串口读数据（JSON Lines 格式）
2. 解析成结构化数据
3. 通过 HTTP / WebSocket 提供给前端
4. （后期）保存 session 历史，供课后回看

```text
网关 ESP32 ──USB Serial──▶ backend ──HTTP / WebSocket──▶ frontend
```

通信格式见 [../docs/protocol.md](../docs/protocol.md)。

---

## 已实现：`read_serial.py`

最小可用的串口读取程序，对应任务书「网关固件 第 1 节」的验收
（*ESP32 周期性产生数据，电脑程序可以稳定读取并打印*）。

### 1. 依赖

只有一个：`pyserial`。两种拿到它的方式，**任选其一**。

**方式 A：装到你自己的 Python 里**（推荐，长期用这个）

```powershell
python -m pip install -r backend/requirements.txt
```

**方式 B：零安装 —— 直接用 ESP-IDF 自带的 Python**

ESP-IDF 的 Python 虚拟环境里已经有 `pyserial`，装了 ESP-IDF v6.1 就能用：

```powershell
. "$env:IDF_PATH\export.ps1"
& "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" backend\read_serial.py --port COM38
```

> 方式 B 不用装任何东西，适合「先把链路验通」；但它依赖 ESP-IDF 环境，长期还是建议用方式 A。

### 2. 运行

```powershell
python backend/read_serial.py                # 自动识别 ESP32 串口
python backend/read_serial.py --port COM38   # 指定串口
python backend/read_serial.py --list         # 列出当前可用串口
python backend/read_serial.py --json         # 输出规范化 JSON，方便接前端
python backend/read_serial.py -v             # 同时显示 ESP-IDF 日志行与 sequence
```

输出就是任务书要求的格式：

```text
Node 1: UNDERSTAND
Node 1: UNSURE
Node 1: NOT_UNDERSTAND
```

按 `Ctrl+C` 退出时会打印本次统计（各 vote 数量、出现过的 `node_id`、异常条数）。

### 设计上的几个要点

- **只处理以 `{` 开头的行**。串口上混着 ESP-IDF 的日志行，不能假设串口上只有 JSON。
- **自动识别串口**：按 Espressif 的 USB 厂商号（`VID 303A`）来找。**同时插着两块 ESP32 时会报错并要求用 `--port` 指定** —— 要选跑 ESP-NOW 接收模式的那块（网关），不是学生端那块。
- **断线自动重连**：板子复位、或串口被别的程序占用时会自动重连，不会直接退出。

### 常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| `could not open port` | 串口被占用（有别的 `idf.py monitor` 开着），或端口号变了。先 `--list` 看看 |
| 一直没有任何输出 | 网关没在发数据。确认网关跑的哪个模式（`GATEWAY_SELF_MOCK`），以及学生端有没有在发 |
| `[警告] JSON 解析失败` | 波特率不对，或数据被截断 |
| `ModuleNotFoundError: serial` | 没装 pyserial，见上面「依赖」两种方式 |

---

## 对应任务书

- 网关固件 第 1 节：ESP32 → PC Demo，先证明 PC 通路工作
- 网关固件 第 2 节 + 前端 第 4 节：真正的 Gateway 与接口对接

## 现状

| 项 | 状态 |
| --- | --- |
| 串口读取 + 解析 + 打印 | ✅ 已完成（`read_serial.py`） |
| 升级成本地服务（HTTP / WebSocket），供前端连接 | ⬜ 未开始 |
| 保存 session 历史 | ⬜ 未开始 |
| 技术选型（继续用 Python，还是换成 Node 与前端统一） | ⬜ 未定 |

定了之后请在这里补上启动方式。
