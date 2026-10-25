#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
状态聚合的单元测试（不依赖硬件、不依赖网络）

    cd backend
    python tests/test_state.py
"""

import os
import sys

# Windows 控制台默认可能不是 UTF-8，中文会乱码
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from state import GATEWAY_TIMEOUT_S, SessionState

PASS = 0


def check(name, actual, expected):
    global PASS
    assert actual == expected, f"{name}: 期望 {expected!r}，实际 {actual!r}"
    PASS += 1
    print(f"  [OK] {name}")


def main():
    s = SessionState()

    print("1. 初始状态")
    snap = s.snapshot(now=1000.0)
    check("state", snap["state"], "idle")
    check("total", snap["total"], 0)
    check("devices_online", snap["devices_online"], 0)
    check("gateway_online", snap["gateway_online"], False)

    print("2. 第一条投票自动开启 session 并计数")
    s.apply({"t": "vote", "node": 1, "vote": "UNDERSTAND", "seq": 1}, now=1001.0)
    snap = s.snapshot(now=1001.0)
    check("state", snap["state"], "voting")
    check("total", snap["total"], 1)
    check("understand", snap["responses"]["understand"], 1)
    check("devices_online", snap["devices_online"], 1)
    check("该节点累计票数", snap["nodes"][0]["count"], 1)

    print("3. 节点离线事件")
    s.apply({"t": "node", "node": 1, "online": False, "count": 1}, now=1002.0)
    check("devices_online", s.snapshot(now=1002.0)["devices_online"], 0)

    print("4. 网关心跳与超时")
    s.apply({"t": "gw", "uptime": 1, "rx": 1, "online": [1], "ch": 1}, now=1003.0)
    check("gateway_online", s.snapshot(now=1003.0)["gateway_online"], True)
    check("超时后离线",
          s.snapshot(now=1003.0 + GATEWAY_TIMEOUT_S + 0.5)["gateway_online"], False)

    print("5. End 之后的票不计数")
    s.session_id_before = s.session_id
    s.end_session(now=1010.0)
    total_before = s.snapshot(now=1010.0)["total"]
    s.apply({"t": "vote", "node": 2, "vote": "UNSURE", "seq": 9}, now=1011.0)
    snap = s.snapshot(now=1011.0)
    check("total 不变", snap["total"], total_before)
    check("state", snap["state"], "ended")
    check("after_end 计数", snap["dropped"]["after_end"], 1)

    print("6. 非法 vote 与未知事件")
    s.apply({"t": "vote", "node": 3, "vote": "MAYBE", "seq": 1}, now=1012.0)
    s.apply({"t": "future_thing", "x": 1}, now=1012.0)
    snap = s.snapshot(now=1012.0)
    check("bad", snap["dropped"]["bad"], 1)
    check("unknown", snap["dropped"]["unknown"], 1)

    print("7. 重新开一场会清零并换 session_id")
    old_id = s.snapshot(now=1020.0)["session_id"]
    s.start_session(now=1020.0)
    snap = s.snapshot(now=1020.0)
    check("total 清零", snap["total"], 0)
    check("session_id 递增", snap["session_id"], old_id + 1)
    check("state", snap["state"], "voting")

    print("8. 快照字段完整")
    snap = s.snapshot(now=1030.0)
    for key in ("session_id", "state", "devices_online", "responses", "total",
                "gateway_online", "nodes", "dropped", "updated_at"):
        assert key in snap, f"快照缺字段 {key}"
    check("字段齐全", True, True)
    check("updated_at 是字符串", isinstance(snap["updated_at"], str), True)

    print(f"\n全部通过：{PASS} 项断言")


if __name__ == "__main__":
    main()
