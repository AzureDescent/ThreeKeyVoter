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

## 已实现：`server.py`（B1 · HTTP 接口）

把串口事件聚合成状态，用 HTTP 提供给前端。**前端拿到的永远是已经算好的数**，不需要自己统计。

### 启动

```powershell
# 接真板子（自动找 ESP32 串口）
python server.py

# 指定串口
python server.py --serial COM5

# 没有硬件：回放录制好的数据
python server.py --replay tests/sample-40s.jsonl
```

默认监听 `http://127.0.0.1:8000`。要让**另一台机器**（比如安崎的电脑）访问，用：

```powershell
python server.py --serial COM5 --host 0.0.0.0
```

然后她访问 `http://<你的内网IP>:8000/state` —— 这时候两个人才需要连同一个 Wi-Fi。

### 接口

`GET /state` —— 当前状态快照（前端 1 Hz 轮询）

```json
{
  "session_id": 1,
  "state": "voting",
  "devices_online": 3,
  "responses": { "understand": 38, "unsure": 29, "not_understand": 25 },
  "total": 92,
  "gateway_online": true,
  "nodes": [
    { "node": 1, "online": true, "count": 34 },
    { "node": 2, "online": true, "count": 34 },
    { "node": 3, "online": true, "count": 24 }
  ],
  "dropped": { "bad": 0, "unknown": 0, "after_end": 0 },
  "updated_at": "2026-10-25T19:31:48+08:00"
}
```

| 字段 | 含义 |
| --- | --- |
| `state` | `idle` / `voting` / `ended`。第一张票到达自动进入 `voting` |
| `devices_online` | 在线节点数，由 `node` 事件维护 |
| `responses` | 三种状态的票数。**小写**，与任务书前端 Mock 一致 |
| `gateway_online` | 3 秒内收到过 `gw` 心跳则为 `true` |
| `nodes[].count` | 该节点累计票数（离线期间不计） |
| `dropped` | 坏数据 / 未知事件 / End 之后到达的票，便于排查 |

`GET /health` —— 存活检查。

### 文件组成

| 文件 | 作用 |
| --- | --- |
| `state.py` | 纯逻辑状态机，不碰串口和网络，可单独测试 |
| `sources.py` | 数据来源：串口（自动重连）/ 回放文件 |
| `server.py` | FastAPI 服务，`--serial` 或 `--replay` |
| `tests/sample-40s.jsonl` | 从真实板子录的 34 秒事件（含上线 / 离线 / 恢复） |
| `tests/test_state.py` | 状态机单元测试，不需要硬件 |

### 测试

```powershell
cd backend
python tests\test_state.py                          # 22 项断言
python server.py --replay tests/sample-40s.jsonl    # 用回放数据起服务
```

---

## 对应任务书

- 网关固件 第 1 节：ESP32 → PC Demo，先证明 PC 通路工作
- 网关固件 第 2 节 + 前端 第 4 节：真正的 Gateway 与接口对接

## 现状

| 项 | 状态 |
| --- | --- |
| 串口读取 + 解析 + 打印 | ✅ 已完成（`read_serial.py`） |
| 技术选型 | ✅ Python + FastAPI（复用 pyserial，不引入第二种语言） |
| B1：聚合状态 + `GET /state` | ✅ 已完成（`server.py`） |
| B2：WebSocket 推送 | ⬜ 未开始 |
| B3：Start / End 控制接口 | ⬜ 状态机已支持，接口未开 |
| B4：session 历史落盘 | ⬜ 未开始 |

B1 已验证：回放 34 秒录制数据，计数与录制内容完全一致；接真实板子时每秒 3 票递增、
`devices_online` 与 `gateway_online` 正确。
