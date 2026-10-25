#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ThreeKeyVoter — PC 端串口读取程序
==================================

读网关 ESP32 的 USB 串口，解析出投票数据并打印。

对应任务书「网关固件 第 1 节」的验收：

    ESP32 周期性产生数据，电脑程序可以稳定读取并打印：

        Node 1: UNDERSTAND
        Node 2: UNSURE

数据格式见 docs/protocol.md 第 3 节：**JSON Lines**，一行一条 JSON，以 \\n 结尾。

注意：串口上不只有 JSON —— ESP-IDF 的日志行（形如 `I (1234) gateway: ...`）
会混在里面。本程序只处理以 `{` 开头的行，其余一律跳过。

用法
----
    python backend/read_serial.py                    # 自动找串口
    python backend/read_serial.py --port COM38       # 指定串口
    python backend/read_serial.py --json             # 输出原始 JSON（方便接前端）
    python backend/read_serial.py --list             # 列出可用串口
    python backend/read_serial.py -v                 # 同时显示 ESP-IDF 日志行

按 Ctrl+C 退出，会打印本次的统计。

依赖
----
    python -m pip install -r backend/requirements.txt
"""

from __future__ import annotations

import argparse
import json
import sys
import time

# ---------------------------------------------------------------------------
# 依赖检查：给出明确的安装提示，而不是丢一个 ModuleNotFoundError
# ---------------------------------------------------------------------------
try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    sys.exit(
        "缺少依赖 pyserial。\n"
        "安装：  python -m pip install -r backend/requirements.txt\n"
        "或者：  python -m pip install pyserial"
    )

# Windows 控制台默认可能不是 UTF-8，中文会乱码
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

# 协议里定义的合法取值（docs/protocol.md 第 1 节）
VALID_VOTES = ("UNDERSTAND", "UNSURE", "NOT_UNDERSTAND")

# Espressif 的 USB 厂商号，用来自动识别 ESP32
ESPRESSIF_VID = 0x303A

DEFAULT_BAUD = 115200

# 串口断线后的重连间隔（秒）
RECONNECT_DELAY = 2.0


def find_espressif_ports():
    """找出所有像 ESP32 的串口。"""
    found = []
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            found.append(p)
    return found


def resolve_port(requested: str | None) -> str:
    """决定用哪个串口。"""
    if requested:
        return requested

    candidates = find_espressif_ports()
    if len(candidates) == 1:
        p = candidates[0]
        print(f"自动识别到 ESP32：{p.device}（{p.description}）")
        return p.device
    if len(candidates) > 1:
        print("检测到多个 ESP32 串口，请用 --port 指定：", file=sys.stderr)
        for p in candidates:
            print(f"    {p.device}  {p.description}", file=sys.stderr)
            print(f"        提示：网关那边跑的是 ESP-NOW 接收模式", file=sys.stderr)
        sys.exit(2)

    print("没找到 ESP32 串口。用 --list 看看当前有哪些串口。", file=sys.stderr)
    sys.exit(2)


def cmd_list() -> None:
    """列出所有串口。"""
    ports = list(list_ports.comports())
    if not ports:
        print("没有检测到任何串口。")
        return
    print(f"{'端口':<10} {'类型':<12} 说明")
    print("-" * 60)
    for p in ports:
        kind = "ESP32" if p.vid == ESPRESSIF_VID else "其它"
        print(f"{p.device:<10} {kind:<12} {p.description}")


def print_summary(stats: dict, elapsed: float) -> None:
    total = stats["ok"]
    print()
    print("──────── 本次统计 ────────")
    print(f"总计 {total} 条，用时 {elapsed:.1f} 秒", end="")
    if elapsed > 0:
        print(f"（约 {total / elapsed:.1f} 条/秒）")
    else:
        print()
    for vote in VALID_VOTES:
        n = stats["by_vote"].get(vote, 0)
        ratio = (n / total * 100) if total else 0
        print(f"  {vote:<16} {n:>5}  ({ratio:5.1f}%)")
    if stats["nodes"]:
        nodes = ", ".join(str(n) for n in sorted(stats["nodes"]))
        print(f"出现过的 node_id: {nodes}")
    if stats["node_events"]:
        print(f"节点上线/离线事件: {stats['node_events']} 条")
    if stats["gw_events"]:
        print(f"网关心跳: {stats['gw_events']} 条")
    if stats["bad_json"]:
        print(f"  ⚠ 解析失败的 JSON: {stats['bad_json']} 条")
    if stats["bad_vote"]:
        print(f"  ⚠ 未知 vote 取值: {stats['bad_vote']} 条")
    if stats["unknown_events"]:
        print(f"  ⚠ 不认识的事件类型: {stats['unknown_events']} 条")


def handle_line(raw: bytes, args, stats: dict) -> None:
    """处理串口上来的一行。"""
    text = raw.decode("utf-8", "replace").strip()
    if not text:
        return

    # ESP-IDF 日志行 —— 默认跳过，-v 时才显示
    if not text.startswith("{"):
        if args.verbose:
            print(f"[log] {text}", flush=True)
        return

    try:
        msg = json.loads(text)
    except json.JSONDecodeError:
        stats["bad_json"] += 1
        print(f"[警告] JSON 解析失败（累计 {stats['bad_json']} 条）: {text[:100]}", flush=True)
        return

    # ---- 协议 v1 事件分发 ------------------------------------------------
    # 见 docs/protocol-v1-proposal.md：串口上行分三类事件，用 "t" 字段区分。
    # 没有 "t" 字段的行按 v0 的纯投票行处理，保持向后兼容。
    kind = msg.get("t")

    if kind == "node":
        stats["node_events"] += 1
        node = msg.get("node")
        online = msg.get("online")
        print(f"[{'上线' if online else '离线'}] Node {node}（累计 {msg.get('count')} 票）", flush=True)
        return

    if kind == "gw":
        stats["gw_events"] += 1
        if args.verbose:
            print(f"[网关] uptime={msg.get('uptime')}ms rx={msg.get('rx')} "
                  f"drop={msg.get('drop')} online={msg.get('online')} ch={msg.get('ch')}", flush=True)
        return

    if kind not in (None, "vote"):
        stats["unknown_events"] += 1
        if args.verbose:
            print(f"[未知事件 t={kind!r}] {text[:100]}", flush=True)
        return

    vote = msg.get("vote")
    node_id = msg.get("node", msg.get("node_id"))   # v1 用 node，v0 用 node_id

    if vote not in VALID_VOTES:
        stats["bad_vote"] += 1
        print(f"[警告] 未知 vote 取值: {vote!r}（原始行: {text[:100]}）", flush=True)
        return

    stats["ok"] += 1
    stats["by_vote"][vote] = stats["by_vote"].get(vote, 0) + 1
    stats["nodes"].add(node_id)

    if args.json:
        # 规范化后输出，方便后面直接管道给前端
        print(json.dumps(
            {"node_id": node_id, "vote": vote, "sequence": msg.get("seq", msg.get("sequence"))},
            ensure_ascii=False,
        ), flush=True)
    elif args.verbose:
        print(f"Node {node_id}: {vote}   (sequence={msg.get('seq', msg.get('sequence'))})", flush=True)
    else:
        # 任务书要求的格式
        print(f"Node {node_id}: {vote}", flush=True)


def run(port: str, args, stats: dict, started: float) -> None:
    """打开串口并一直读。打开失败或中途断开都会自动重连。"""
    connected_once = False

    while True:
        try:
            with serial.Serial(port, args.baud, timeout=1) as ser:
                connected_once = True
                print(f"已连接 {port} @ {args.baud}，等待数据…（Ctrl+C 退出）")
                buf = b""
                while True:
                    chunk = ser.read(4096)
                    if not chunk:
                        continue
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        handle_line(line, args, stats)
        except KeyboardInterrupt:
            raise
        except (serial.SerialException, OSError) as exc:
            # Windows 上，刚被别的程序（比如 idf.py monitor）放开的串口
            # 第一次打开经常直接报错，隔一下重试就好了，所以这里不退出。
            if connected_once:
                print(f"\n串口断开：{exc}", file=sys.stderr)
                print(f"{RECONNECT_DELAY:.0f} 秒后重连…（Ctrl+C 退出）", file=sys.stderr)
            else:
                print(f"打开 {port} 失败：{exc}", file=sys.stderr)
                print(f"{RECONNECT_DELAY:.0f} 秒后重试…（Ctrl+C 退出）", file=sys.stderr)
            time.sleep(RECONNECT_DELAY)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="读取 ThreeKeyVoter 网关串口并打印投票数据",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--port", help="串口号，例如 COM38。不填则自动寻找 ESP32")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"波特率（默认 {DEFAULT_BAUD}）")
    ap.add_argument("--json", action="store_true", help="输出规范化的 JSON，而不是 'Node N: VOTE'")
    ap.add_argument("--verbose", "-v", action="store_true", help="同时显示 ESP-IDF 日志行和 sequence")
    ap.add_argument("--list", action="store_true", help="列出当前可用串口后退出")
    args = ap.parse_args()

    if args.list:
        cmd_list()
        return 0

    port = resolve_port(args.port)

    stats = {
        "ok": 0,
        "bad_json": 0,
        "bad_vote": 0,
        "node_events": 0,
        "gw_events": 0,
        "unknown_events": 0,
        "by_vote": {},
        "nodes": set(),
    }
    started = time.time()

    try:
        run(port, args, stats, started)
    except KeyboardInterrupt:
        print("\n收到 Ctrl+C，退出。")
    finally:
        print_summary(stats, time.time() - started)

    return 0


if __name__ == "__main__":
    sys.exit(main())
