#!/usr/bin/env python3
"""scripts/affect_eval/export.py 的回归测试：临时库 + 与 DecisionStore 相同的三表 DDL。"""

import argparse
import contextlib
import csv
import importlib.util
import io
import json
import os
import sqlite3
import sys
import tempfile
import unittest

SCRIPT = None

# 与 src/Affect/DecisionStore.cpp 的 DDL 保持一致
DDL = """
CREATE TABLE affect_decisions (
 decision_id TEXT PRIMARY KEY, created_at INTEGER NOT NULL, source TEXT NOT NULL,
 group_id INTEGER NOT NULL DEFAULT 0, user_id INTEGER NOT NULL DEFAULT 0,
 trigger_message_id TEXT NOT NULL DEFAULT '', plan_json TEXT NOT NULL,
 policy_version TEXT NOT NULL DEFAULT '', plan_version TEXT NOT NULL DEFAULT '',
 state_json TEXT NOT NULL DEFAULT '{}', fallback_reason TEXT NOT NULL DEFAULT '');
CREATE TABLE affect_executions (
 execution_id TEXT PRIMARY KEY, decision_id TEXT NOT NULL, created_at INTEGER NOT NULL,
 status TEXT NOT NULL, actual_text TEXT NOT NULL DEFAULT '',
 outbound_message_id TEXT NOT NULL DEFAULT '', deviation_reason TEXT NOT NULL DEFAULT '');
CREATE TABLE affect_feedback (
 feedback_id TEXT PRIMARY KEY, decision_id TEXT NOT NULL, created_at INTEGER NOT NULL,
 source TEXT NOT NULL, status TEXT NOT NULL, confidence REAL NOT NULL DEFAULT 0,
 evidence_json TEXT NOT NULL DEFAULT '{}');
"""


def load_script():
    # 导入被测脚本不得在 scripts/ 下留 __pycache__
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("affect_eval_export", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ExportTest(unittest.TestCase):
    def setUp(self):
        self.module = load_script()
        self.directory = tempfile.TemporaryDirectory()
        self.db = os.path.join(self.directory.name, "test.db")
        connection = sqlite3.connect(self.db)
        connection.executescript(DDL)

        def decision(did, ts, source, action, policy="baseline_v2.4.1", state="{}",
                     group=100, fallback=""):
            plan = {"action": action, "tool_policy": "none" if action == "no_reply" else "inherit"}
            connection.execute(
                "INSERT INTO affect_decisions (decision_id, created_at, source, group_id, plan_json,"
                " policy_version, state_json, fallback_reason) VALUES (?,?,?,?,?,?,?,?)",
                (did, ts, source, group, json.dumps(plan), policy, state, fallback))

        def execution(eid, did, ts, status, text, deviation=""):
            connection.execute(
                "INSERT INTO affect_executions (execution_id, decision_id, created_at, status,"
                " actual_text, deviation_reason) VALUES (?,?,?,?,?,?)",
                (eid, did, ts, status, text, deviation))

        def feedback(fid, did, ts, source, label):
            connection.execute(
                "INSERT INTO affect_feedback (feedback_id, decision_id, created_at, source, status,"
                " evidence_json) VALUES (?,?,?,?,'known',?)",
                (fid, did, ts, source, json.dumps({"label": label, "note": "群友备注不该出现"})))

        decision("d1", 1000, "at_reply", "reply")
        execution("e1", "d1", 1001, "sent", "她的第一句")
        feedback("f1", "d1", 1100, "manual_admin", "good")

        decision("d2", 2000, "engagement_turn", "group_say")
        execution("e2", "d2", 2001, "sent", "她的第二句")
        feedback("f2", "d2", 2100, "manual_user", "bad")

        decision("d3", 3000, "engagement_turn", "no_reply", policy="judge_v1",
                 state=json.dumps({"trigger_kind": "lull", "judge_latency_ms": 800}))
        execution("e3", "d3", 3001, "sent", "影子期她照常说了", deviation="shadow")
        feedback("f3", "d3", 3100, "manual_admin", "bad")
        feedback("f4", "d3", 3200, "manual_admin", "good")  # 最新一条为准

        decision("d4", 4000, "engagement_turn", "group_say", policy="judge_v1",
                 state=json.dumps({"trigger_kind": "name_mention", "judge_latency_ms": 2400}),
                 group=200, fallback="judge_timeout")
        execution("e4", "d4", 4001, "skipped", "", deviation="shadow")
        connection.commit()
        connection.close()

    def tearDown(self):
        self.directory.cleanup()

    def run_main(self, *extra):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = self.module.main(["--db", self.db, *extra])
        self.assertEqual(code, 0)
        return output.getvalue()

    def test_records_join_and_latest_label(self):
        connection = self.module.open_readonly(self.db)
        records = {r["decision_id"]: r for r in self.module.collect(connection, None, None)}
        connection.close()
        self.assertEqual(records["d1"]["actual"], "spoke_text")
        self.assertEqual(records["d1"]["label_admin"], "good")
        self.assertIsNone(records["d2"]["label_admin"])
        self.assertEqual(records["d2"]["label_all"], "bad")
        self.assertEqual(records["d3"]["label_admin"], "good")
        self.assertEqual(records["d3"]["trigger_kind"], "lull")
        self.assertEqual(records["d4"]["actual"], "skipped")
        self.assertEqual(records["d4"]["fallback_reason"], "judge_timeout")

    def test_metrics_follow_plan_action(self):
        connection = self.module.open_readonly(self.db)
        records = self.module.collect(connection, None, None)
        connection.close()
        baseline = [r for r in records if r["policy_version"] == "baseline_v2.4.1"]
        values = self.module.metrics(baseline, "label_all")
        self.assertEqual(values["有标注决策"], 2)
        self.assertTrue(values["参与精确率"].startswith("50.0%"))
        self.assertTrue(values["打扰率"].startswith("50.0%"))
        judge = [r for r in records if r["policy_version"] == "judge_v1"]
        self.assertTrue(self.module.metrics(judge, "label_admin")["沉默正确率"].startswith("100.0%"))

    def test_report_and_filters(self):
        text = self.run_main()
        self.assertIn("决策 4 条", text)
        self.assertIn("judge_timeout", text)
        self.assertIn("p50=800", text)
        self.assertIn("p95=2400", text)
        self.assertNotIn("群友备注", text)
        self.assertNotIn("她的第一句", text)  # 正文只进 CSV
        self.assertIn("决策 1 条", self.run_main("--group", "200"))
        self.assertIn("决策 2 条", self.run_main("--since", "3000"))

    def test_csv_only_carries_actual_text(self):
        path = os.path.join(self.directory.name, "out.csv")
        self.run_main("--csv", path)
        with open(path, encoding="utf-8-sig") as handle:
            rows = list(csv.DictReader(handle))
        self.assertEqual(len(rows), 4)
        self.assertEqual(rows[0]["actual_text"], "她的第一句")
        self.assertNotIn("note", rows[0])
        with open(path, encoding="utf-8-sig") as handle:
            self.assertNotIn("群友备注", handle.read())

    def test_database_is_not_modified(self):
        before = os.path.getmtime(self.db), os.path.getsize(self.db)
        self.run_main()
        self.assertEqual(before, (os.path.getmtime(self.db), os.path.getsize(self.db)))
        connection = self.module.open_readonly(self.db)
        with self.assertRaises(sqlite3.OperationalError):
            connection.execute("DELETE FROM affect_feedback")
        connection.close()

    def test_since_parsing(self):
        self.assertEqual(self.module.parse_since("1793169349"), 1793169349)
        self.assertIsInstance(self.module.parse_since("2026-11-01"), int)
        with self.assertRaises(argparse.ArgumentTypeError):
            self.module.parse_since("昨天")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True)
    args, remaining = parser.parse_known_args()
    SCRIPT = args.script
    unittest.main(argv=[sys.argv[0], *remaining])
