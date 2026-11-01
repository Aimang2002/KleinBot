#!/usr/bin/env python3
"""affect 影子期只读导出（R0.4 / R1.d）。

只读打开 SQLite（file:...?mode=ro），把 affect_decisions / affect_executions /
affect_feedback 按 decision_id 连起来，打印分布、混淆矩阵与重规划 §2 的指标。
不改库、不进 CMake、只用标准库。唯一会输出的文本是 actual_text（她自己的话，
仅 --csv）；反馈备注等任何群友文本都不输出。

用法：
    python3 scripts/affect_eval/export.py --db build/source/.conversations.db
    python3 scripts/affect_eval/export.py --db ... --since 2026-11-01 --group 123 --csv out.csv
"""

import argparse
import csv
import json
import sqlite3
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path

# 重规划 §2 的口径：speak = reply/group_say，沉默 = no_reply；leave_topic 两边都不计
SPEAK_ACTIONS = {"reply", "group_say"}
SILENT_ACTIONS = {"no_reply"}
MISSING = "-"


def parse_since(text):
    """ISO 日期/时间（按本地时区）或 unix 秒 → unix 秒。"""
    if text is None:
        return None
    stripped = text.strip()
    if stripped.lstrip("-").isdigit():
        return int(stripped)
    try:
        return int(datetime.fromisoformat(stripped).timestamp())
    except ValueError:
        raise argparse.ArgumentTypeError("--since 需要 ISO 日期/时间或 unix 秒：" + text)


def load_json(text):
    try:
        value = json.loads(text or "{}")
    except (TypeError, ValueError):
        return {}
    return value if isinstance(value, dict) else {}


def open_readonly(path):
    db = Path(path)
    if not db.is_file():
        raise SystemExit("数据库不存在：" + str(path))
    uri = "file:" + db.resolve().as_posix() + "?mode=ro"
    connection = sqlite3.connect(uri, uri=True)
    tables = {row[0] for row in connection.execute(
        "SELECT name FROM sqlite_master WHERE type='table'")}
    needed = {"affect_decisions", "affect_executions", "affect_feedback"}
    if not needed <= tables:
        raise SystemExit("缺少 affect 三表（affect.enabled 未开启过？）：" +
                         ", ".join(sorted(needed - tables)))
    return connection


def actual_kind(executions):
    """实际动作：none（无执行行）/ skipped / failed / spoke_text / spoke_nontext。"""
    if not executions:
        return "none"
    # 一条决策正常只有一条执行；多条时以最后一条为准
    last = executions[-1]
    if last["status"] == "skipped":
        return "skipped"
    if last["status"] == "failed":
        return "failed"
    return "spoke_text" if last["actual_text"] else "spoke_nontext"


def latest_label(feedback, admin_only):
    """该决策最新一条反馈的 good/bad；admin_only 时只看 manual_admin。"""
    chosen = None
    for row in feedback:
        if admin_only and row["source"] != "manual_admin":
            continue
        label = load_json(row["evidence_json"]).get("label")
        if label in ("good", "bad"):
            chosen = label
    return chosen


def collect(connection, since, group):
    connection.row_factory = sqlite3.Row
    where, params = [], []
    if since is not None:
        where.append("created_at >= ?")
        params.append(since)
    if group is not None:
        where.append("group_id = ?")
        params.append(group)
    sql = "SELECT * FROM affect_decisions"
    if where:
        sql += " WHERE " + " AND ".join(where)
    sql += " ORDER BY created_at, decision_id"
    decisions = [dict(row) for row in connection.execute(sql, params)]

    executions, feedback = {}, {}
    for row in connection.execute(
            "SELECT * FROM affect_executions ORDER BY created_at, execution_id"):
        executions.setdefault(row["decision_id"], []).append(dict(row))
    for row in connection.execute(
            "SELECT * FROM affect_feedback ORDER BY created_at, feedback_id"):
        feedback.setdefault(row["decision_id"], []).append(dict(row))

    records = []
    for decision in decisions:
        plan = load_json(decision["plan_json"])
        state = load_json(decision["state_json"])
        rows = executions.get(decision["decision_id"], [])
        notes = feedback.get(decision["decision_id"], [])
        records.append({
            "decision_id": decision["decision_id"],
            "created_at": decision["created_at"],
            "source": decision["source"],
            "group_id": decision["group_id"],
            "policy_version": decision["policy_version"] or MISSING,
            "trigger_kind": state.get("trigger_kind") or MISSING,
            "plan_action": plan.get("action") or MISSING,
            "tool_policy": plan.get("tool_policy") or MISSING,
            "fallback_reason": decision["fallback_reason"] or MISSING,
            "judge_latency_ms": state.get("judge_latency_ms"),
            "actual": actual_kind(rows),
            "execution_status": rows[-1]["status"] if rows else MISSING,
            "deviation_reason": (rows[-1]["deviation_reason"] if rows else "") or MISSING,
            "actual_text": rows[-1]["actual_text"] if rows else "",
            "label_admin": latest_label(notes, admin_only=True),
            "label_all": latest_label(notes, admin_only=False),
        })
    return records


def rate(numerator, denominator):
    if denominator == 0:
        return "n/a（0 条）"
    return "{:.1%}（{}/{}）".format(numerator / denominator, numerator, denominator)


def metrics(records, label_key):
    """重规划 §2：参与精确率、打扰率、沉默正确率（按 plan.action 划分）。"""
    speak = [r for r in records if r["plan_action"] in SPEAK_ACTIONS]
    silent = [r for r in records if r["plan_action"] in SILENT_ACTIONS]
    speak_labeled = [r for r in speak if r[label_key]]
    silent_labeled = [r for r in silent if r[label_key]]
    return {
        "有标注决策": sum(1 for r in records if r[label_key]),
        "参与精确率": rate(sum(r[label_key] == "good" for r in speak_labeled), len(speak_labeled)),
        "打扰率": rate(sum(r[label_key] == "bad" for r in speak), len(speak)),
        "沉默正确率": rate(sum(r[label_key] == "good" for r in silent_labeled), len(silent_labeled)),
    }


def percentile(values, fraction):
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1)))))
    return ordered[index]


def print_counter(title, counter):
    print("\n## " + title)
    if not counter:
        print("（无）")
        return
    width = max(len(" × ".join(map(str, key)) if isinstance(key, tuple) else str(key))
                for key in counter)
    for key, count in sorted(counter.items(), key=lambda item: (-item[1], str(item[0]))):
        label = " × ".join(map(str, key)) if isinstance(key, tuple) else str(key)
        print("  {:<{w}}  {}".format(label, count, w=width))


def print_matrix(records):
    print("\n## plan.action × 实际动作")
    rows = sorted({r["plan_action"] for r in records})
    cols = ["spoke_text", "spoke_nontext", "skipped", "failed", "none"]
    cells = Counter((r["plan_action"], r["actual"]) for r in records)
    if not rows:
        print("（无）")
        return
    width = max(len(row) for row in rows + ["plan\\actual"])
    print("  {:<{w}}  ".format("plan\\actual", w=width) + "  ".join("{:>13}".format(c) for c in cols))
    for row in rows:
        print("  {:<{w}}  ".format(row, w=width) +
              "  ".join("{:>13}".format(cells.get((row, c), 0)) for c in cols))


def report(records, since, group):
    print("# affect 导出")
    scope = []
    if since is not None:
        scope.append("since " + datetime.fromtimestamp(since).isoformat(sep=" "))
    if group is not None:
        scope.append("group " + str(group))
    print("范围：" + ("，".join(scope) if scope else "全部") + "；决策 {} 条".format(len(records)))
    if records:
        first = datetime.fromtimestamp(records[0]["created_at"]).isoformat(sep=" ")
        last = datetime.fromtimestamp(records[-1]["created_at"]).isoformat(sep=" ")
        print("时间：{} → {}".format(first, last))

    print_counter("policy_version × source × trigger_kind × plan.action",
                  Counter((r["policy_version"], r["source"], r["trigger_kind"], r["plan_action"])
                          for r in records))
    print_counter("fallback_reason", Counter(r["fallback_reason"] for r in records))
    print_matrix(records)
    print_counter("deviation_reason", Counter(r["deviation_reason"] for r in records))

    for policy in sorted({r["policy_version"] for r in records}):
        subset = [r for r in records if r["policy_version"] == policy]
        print("\n## 指标：" + policy)
        for scope_name, key in (("仅管理员标注", "label_admin"), ("全部标注", "label_all")):
            values = metrics(subset, key)
            print("  [{}] ".format(scope_name) +
                  "，".join("{} {}".format(name, value) for name, value in values.items()))

    latencies = [r["judge_latency_ms"] for r in records
                 if isinstance(r["judge_latency_ms"], (int, float))]
    print("\n## judge_latency_ms")
    if latencies:
        print("  n={}  p50={}  p95={}".format(
            len(latencies), percentile(latencies, 0.5), percentile(latencies, 0.95)))
    else:
        print("  （无 judge 记录）")


CSV_FIELDS = [
    "decision_id", "created_at", "source", "group_id", "policy_version", "trigger_kind",
    "plan_action", "tool_policy", "fallback_reason", "judge_latency_ms", "execution_status",
    "actual", "deviation_reason", "label_admin", "label_all", "actual_text",
]


def write_csv(records, path):
    with open(path, "w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS, extrasaction="ignore")
        writer.writeheader()
        for record in records:
            row = dict(record)
            row["label_admin"] = row["label_admin"] or ""
            row["label_all"] = row["label_all"] or ""
            row["judge_latency_ms"] = "" if row["judge_latency_ms"] is None else row["judge_latency_ms"]
            writer.writerow(row)


def main(argv=None):
    parser = argparse.ArgumentParser(description="affect 影子期只读导出")
    parser.add_argument("--db", required=True, help="SQLite 路径，如 build/source/.conversations.db")
    parser.add_argument("--since", type=parse_since, help="起点：ISO 日期/时间（本地时区）或 unix 秒")
    parser.add_argument("--group", type=int, help="只看某个群号")
    parser.add_argument("--csv", help="逐决策明细输出到 CSV（含 actual_text）")
    args = parser.parse_args(argv)

    connection = open_readonly(args.db)
    try:
        records = collect(connection, args.since, args.group)
    finally:
        connection.close()
    report(records, args.since, args.group)
    if args.csv:
        write_csv(records, args.csv)
        print("\nCSV 已写出：{}（{} 行）".format(args.csv, len(records)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
