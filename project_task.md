# 三键投票器初期任务书

接下来项目正式进入开发阶段。

后续的：

- 学生端固件
- 网关固件
- PC 端代码
- 前端代码

都会统一放到 GitHub 仓库中，并使用 Git 进行版本管理。

嵌入式开发统一使用 **ESP-IDF**。

这几天大家首先需要学习 Git / GitHub 的基础使用，至少掌握：

```text
git clone
git pull
git add
git commit
git push
```

以及大概理解：

```text
repository
branch
commit
pull request
```

现阶段不要求深入 Git 原理，先保证自己能正常参与多人项目。

我稍后会创建 GitHub 仓库和基本目录结构。

---

## 学生端固件

你的方向主要是 **ESP32 学生终端固件**。

### 0. 开发环境

完成：

- ESP-IDF 安装与基本配置
- 能创建、编译、烧录一个 ESP32-C3 工程
- 配置 Git / GitHub
- 能把自己修改的代码 commit 到本地仓库

验收：

> ESP32-C3 能正常运行一个自己编译的程序，并在串口输出信息。

---

### 1. 准备基础电子元件

需要：

- 面包板
- 按钮
- 电阻
- 杜邦线
- LED

学创 C200 可能已有部分元件，可以先确认。

课程每人的报销额度我印象中是 200 元，采购前可以再确认一下。

---

### 2. 三按键输入 Demo

完成一个非常简单的面包板 Demo：

```text
Button 1 → UNDERSTAND
Button 2 → UNSURE
Button 3 → NOT_UNDERSTAND
```

要求：

按下对应按钮后：

- LED 可以产生对应反馈；
- 串口能够打印对应状态。

例如：

```text
Button 1 pressed
State = UNDERSTAND
```

重点不是界面，而是理解：

```text
GPIO 输入
GPIO 输出
按键读取
基本消抖
串口调试
```

验收：

> 三个按钮都能可靠识别，并打印正确状态。

---

### 3. Wi-Fi 基础

了解 ESP32 Wi-Fi Station 模式，并做到：

```text
ESP32 → 手机热点
```

连接成功后打印：

```text
Connected
IP: xxx.xxx.xxx.xxx
```

现阶段不要求深入研究 Wi-Fi 协议栈。

---

### 4. ESP-NOW

在至少两块 ESP32 之间跑通：

```text
ESP32 A
   ↓ ESP-NOW
ESP32 B
```

能发送一段简单数据，例如：

```text
hello
```

之后再逐步替换为：

```text
node_id
vote
sequence
...
```

---

### 5. 中期目标

学生端能够：

```text
按键
 ↓
ESP32
 ↓
ESP-NOW
 ↓
Gateway
```

把投票状态发送给网关。

---

### 第五周上课前要求

完成：

> **0 + 1 + 2**

3 / 4 可以提前了解。

如果 ESP-IDF 或 GitHub 基建导致时间不足，允许先提交伪代码或未完全整理的工程，但 **0 和基本按键 Demo 必须真正跑起来。**

---

## 网关固件 + 结构

你的工作前期主要是：

> **ESP32 ↔ PC**

后期转向：

> **SolidWorks 结构设计**

---

### 0. 开发环境

完成：

- ESP-IDF 环境
- 能编译烧录 ESP32-C3
- Git / GitHub 基础
- 确保 SolidWorks 可以正常使用
- 了解 `make.sjtu.edu.cn` 的基本使用方式

验收：

> 能独立烧一个 ESP32 程序，并能正常使用 Git 管理代码。

---

### 1. ESP32 → PC Demo

现在先完全不用等学生端。

让 ESP32 自己随机产生 Mock Data，例如：

```json
{
  "node_id": 1,
  "vote": "UNDERSTAND"
}
```

然后：

```text
ESP32
 ↓ USB Serial
PC
```

PC 上写一个简单程序把它读出来。

例如最终终端显示：

```text
Node 1: UNDERSTAND
Node 2: UNSURE
```

这一阶段你可以和前端协调，但不要求前后端现在就完全连接。

验收：

> ESP32 周期性产生 Mock Data，电脑程序可以稳定读取并打印。

先证明 PC 通路工作。

---

### 2. 中期：真正的 Gateway

之后与学生端对接：

```text
Student ESP32
    ↓
 ESP-NOW
    ↓
Gateway ESP32
    ↓
 USB Serial
    ↓
    PC
```

---

### 3. 后期：SolidWorks

学生端和网关的元件基本确定之后：

设计：

```text
Student Terminal Enclosure
Gateway Enclosure
```

主要考虑：

- ESP32 固定
- 按键安装
- USB-C 开孔
- 可拆装
- 人机操作
- 后期可能的 PCB / 电池空间

没必要提前按想象画最终外壳。

**等真实元件尺寸出来以后再冻结结构。**

---

### 第五周上课前要求

完成：

> **0 + 1**

即：

> ESP32 能自己产生 Mock Data，USB 接电脑后电脑程序能够读到。

---

## Frontend / UI

你现在**完全不需要等待任何硬件**。

前期直接使用 Mock Data 开发。

---

### 0. Git / 项目环境

完成：

- Git / GitHub 基础
- 建立自己熟悉的 Web 开发环境
- 能启动前端项目

框架和语言可以使用自己熟悉的方案。

---

### 1. Mock Data

你需要先自己假设数据。

例如：

```json
{
  "session_id": 1,
  "devices_online": 3,
  "responses": {
    "understand": 1,
    "unsure": 1,
    "not_understand": 1
  }
}
```

具体格式目前**不用完全定死**。

你的目标是：

> 在没有任何 ESP32 的情况下，把页面先做起来。

---

### 2. 前端基本框架

先完成一个教师页面 Demo。

至少显示：

```text
设备在线数量

✓ 理解
? 不确定
× 未理解

当前人数 / 比例

Start Feedback
End Feedback
```

数据全部可以先随机生成。

---

### 3. UI / UX

在实现过程中思考实际使用场景：

例如：

```text
字体是否足够大？

教室一体机远距离能不能看清？

投票中是否应该立即展示结果？

Session 结束后怎么显示？
```

不要先追求特别漂亮。

第一阶段优先：

> **清楚 + 好用。**

---

### 4. 后期接口对接

之后与后端协调：

```text
ESP32 Gateway
↓
PC Backend
↓
Frontend
```

那时候再正式冻结数据格式。

---

### 第五周上课前要求

完成：

> **0 + 1**

最好能够进一步做到：

> Mock Data 已经能驱动一个简单的页面变化。

---

> **这周遇到环境配置、代码、硬件等任何问题，不要卡超过半天。卡住就直接在群里问，或者 @我。现在的目标不是考察谁能一个人查三天 Stack Overflow，而是尽快把整个团队开发环境跑起来。**
