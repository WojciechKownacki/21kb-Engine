from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import agent_exam  # noqa: E402
from png_readback import write_png  # noqa: E402

SCENE_LIST = """scene: Main (6 nodes)
  [0] Main Camera components: Camera, AudioListener @ 0,2,-8
  [1] Sun components: Light @ 0,0,0
  [2] Player components: CharacterController behaviour: /Game/Scripts/FirstPersonCharacter.lua (Lua, Gameplay, order 0) @ 0,1,0
  [3] Camera (parent 2) components: Camera, AudioListener @ 0,1.7,0
  [4] Marker (parent 2) @ 1,1,1
"""


class ConfigurationTests(unittest.TestCase):
    def test_agent_model_and_effort(self) -> None:
        config = agent_exam.parse_config("Agent A:model-x:medium")
        self.assertEqual((config["agent"], config["model"], config["effort"]), ("Agent A", "model-x", "medium"))

    def test_default_model_is_empty(self) -> None:
        config = agent_exam.parse_config("AgentB:default:high")
        self.assertEqual((config["agent"], config["model"], config["effort"]), ("AgentB", "", "high"))

    def test_missing_parts_are_the_agents_own(self) -> None:
        config = agent_exam.parse_config("AgentB")
        self.assertEqual((config["model"], config["effort"]), ("", ""))


class SceneListTests(unittest.TestCase):
    def test_nodes_components_and_paths(self) -> None:
        entities = agent_exam.parse_scene_list(SCENE_LIST)
        self.assertEqual([entity.path for entity in entities], ["Main Camera", "Sun", "Player", "Player/Camera", "Player/Marker"])
        self.assertTrue(entities[2].has_component("character controller"))
        self.assertIn("FirstPersonCharacter.lua", entities[2].behaviours)
        self.assertEqual(entities[4].components, [])


class TextMatchTests(unittest.TestCase):
    def test_expression_over_groups(self) -> None:
        text = "Light: kind=Point color=1,0.5,0.1 intensity=2"
        pattern = "color=([0-9.]+),([0-9.]+),([0-9.]+)"
        self.assertTrue(agent_exam.text_matches(text, pattern, "g1 > g3"))
        self.assertFalse(agent_exam.text_matches(text, pattern, "g3 > g1"))

    def test_no_pattern_matches_anything(self) -> None:
        self.assertTrue(agent_exam.text_matches("anything", None, None))

    def test_expression_cannot_reach_builtins(self) -> None:
        self.assertFalse(agent_exam.text_matches("a 1", "a ([0-9])", "__import__('os')"))


class FakeProject:
    """Answers a check from tables instead of running kb_cli."""

    def __init__(self, folder: Path, settings=None, plugins=None, scenes=None, shows=None):
        self.folder = folder
        self._settings = settings or {}
        self._plugins = plugins or {}
        self._scenes = scenes or {}
        self._shows = shows or {}

    def settings(self):
        return self._settings

    def plugins(self):
        return self._plugins

    def entities(self, scene=None):
        return agent_exam.parse_scene_list(self._scenes.get(scene, ""))

    def show(self, path, scene=None):
        return self._shows.get((scene, path), "")

    def files(self):
        return {path.relative_to(self.folder).as_posix(): path.read_bytes().hex()
                for path in self.folder.rglob("*") if path.is_file() and not path.relative_to(self.folder).as_posix().startswith("Saved/")}


class CheckTests(unittest.TestCase):
    def judge(self, work: FakeProject, base: FakeProject, answers=None) -> agent_exam.Judge:
        return agent_exam.Judge(work, base, base.files(), answers or [], work.folder)

    def test_setting_equals_compares_numbers_as_numbers(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            work = FakeProject(Path(folder), settings={"Display.WindowWidth": "1920"})
            judge = self.judge(work, FakeProject(Path(folder)))
            self.assertTrue(judge.evaluate({"check": "setting", "key": "Display.WindowWidth", "equals": 1920}).ok)
            self.assertFalse(judge.evaluate({"check": "setting", "key": "Display.WindowWidth", "equals": 1080}).ok)

    def test_plugin_state(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            judge = self.judge(FakeProject(Path(folder), plugins={"Audio.Miniaudio": False}), FakeProject(Path(folder)))
            self.assertTrue(judge.evaluate({"check": "plugin", "id": "Audio.Miniaudio", "enabled": False}).ok)
            self.assertFalse(judge.evaluate({"check": "plugin", "id": "Audio.Miniaudio", "enabled": True}).ok)

    def test_only_new_entities_count(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            base = FakeProject(Path(folder), scenes={None: SCENE_LIST.split("  [2]")[0]})
            judge = self.judge(FakeProject(Path(folder), scenes={None: SCENE_LIST}), base)
            # The camera the fresh project had does not count; the character's camera does.
            found = judge.evaluate({"check": "scene_entities", "component": "Camera"})
            self.assertTrue(found.ok)
            self.assertIn("Player/Camera", found.detail)
            self.assertNotIn("Main Camera", found.detail)
            self.assertFalse(judge.evaluate({"check": "scene_entities", "component": "Light"}).ok)

    def test_an_entity_that_gained_a_component_counts(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            base = FakeProject(Path(folder), scenes={None: "  [0] Sun components: Light @ 0,0,0\n"})
            work = FakeProject(Path(folder), scenes={None: "  [0] Sun components: Light, Rigidbody @ 0,0,0\n"})
            self.assertTrue(self.judge(work, base).evaluate({"check": "scene_entities", "component": "Rigidbody"}).ok)

    def test_entity_values_must_differ_from_the_fresh_project(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            scene = "  [0] Sun components: Light @ 0,0,0\n"
            key = (None, "Sun")
            base = FakeProject(Path(folder), scenes={None: scene}, shows={key: "Light: color=1,1,1"})
            same = FakeProject(Path(folder), scenes={None: scene}, shows={key: "Light: color=1,1,1"})
            warm = FakeProject(Path(folder), scenes={None: scene}, shows={key: "Light: color=1,0.6,0.2"})
            check = {"check": "entity_show", "component": "Light", "regex": "color=([0-9.]+),([0-9.]+),([0-9.]+)", "expr": "g1 > g3"}
            self.assertFalse(self.judge(same, base).evaluate(check).ok)
            self.assertTrue(self.judge(warm, base).evaluate(check).ok)

    def test_files_new_contains_and_max_count(self) -> None:
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as before:
            (Path(before) / "Assets").mkdir()
            (Path(before) / "Assets" / "Old.lua").write_text("-- old")
            (Path(folder) / "Assets").mkdir()
            (Path(folder) / "Assets" / "Old.lua").write_text("-- old")
            (Path(folder) / "Assets" / "Spin.lua").write_text("function Update() rotate() end")
            judge = self.judge(FakeProject(Path(folder)), FakeProject(Path(before)))
            self.assertTrue(judge.evaluate({"check": "files", "glob": "Assets/*.lua", "contains": "rotate"}).ok)
            self.assertFalse(judge.evaluate({"check": "files", "glob": "Assets/*.lua", "contains": "score"}).ok)
            self.assertFalse(judge.evaluate({"check": "files", "glob": "Assets/*.lua", "count_at_least": 2}).ok)
            self.assertTrue(judge.evaluate({"check": "files", "glob": "Assets/*.lua", "new": False, "count_at_least": 2}).ok)
            self.assertTrue(judge.evaluate({"check": "files", "glob": "Assets/Gone*.lua", "new": False, "max_count": 0}).ok)

    def test_a_blank_picture_is_not_a_picture(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            blank = Path(folder) / "blank.png"
            write_png(blank, [[(10, 10, 10)] * 64 for _ in range(36)])
            varied = Path(folder) / "varied.png"
            write_png(varied, [[((x * 4) % 256, (y * 7) % 256, (x + y) % 256) for x in range(64)] for y in range(36)])
            self.assertFalse(agent_exam.picture_is_not_blank(blank))
            self.assertTrue(agent_exam.picture_is_not_blank(varied))
            self.assertFalse(agent_exam.picture_is_not_blank(Path(folder) / "missing.png"))

    def test_any_of_and_answer(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            judge = self.judge(FakeProject(Path(folder)), FakeProject(Path(folder)), answers=["Na scenie jest gracz i kamera."])
            self.assertTrue(judge.evaluate({"check": "any_of", "checks": [
                {"check": "answer", "regex": "drzewo"}, {"check": "answer", "regex": "kamer"}]}).ok)
            self.assertFalse(judge.evaluate({"check": "answer", "regex": "drzewo"}).ok)
            self.assertFalse(judge.evaluate({"check": "no_such_check"}).ok)


class ExamFileTests(unittest.TestCase):
    def test_the_shipped_exam_is_well_formed(self) -> None:
        exam = agent_exam.load_exam(ROOT / "scripts" / "agent_exam" / "exam_pl.json")
        self.assertGreaterEqual(len(exam["tasks"]), 8)
        kinds = {"scene_entities", "entity_show", "files", "setting", "plugin", "run", "validate", "answer", "unchanged", "any_of"}

        def walk(checks):
            for check in checks:
                self.assertIn(check["check"], kinds)
                if check["check"] == "any_of":
                    walk(check["checks"])
        for task in exam["tasks"]:
            walk(task["checks"])
            self.assertIn(task.get("template", "Basic3D"), {"Basic3D", "Basic2D", "FirstPerson", "ThirdPerson", "TopDown", "Platformer2D", "Vehicle", "Empty"})

    def test_a_scenario_has_one_request_step_per_request(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            task = {"id": "t", "requests": ["a", "b"]}
            path = Path(folder) / "scenario.json"
            agent_exam.write_scenario(path, Path(folder) / "source", task, agent_exam.parse_config("AgentA:model-x:low"))
            steps = json.loads(path.read_text(encoding="utf-8"))["steps"]
            self.assertEqual([step["op"] for step in steps], ["open_scene", "agent_request", "agent_request"])
            self.assertEqual((steps[1]["agent"], steps[1]["model"], steps[1]["effort"]), ("AgentA", "model-x", "low"))
            self.assertEqual((steps[1]["timeout_seconds"], steps[1]["max_tokens"]), (600, 400000))

    def test_tasks_are_chosen_by_id_or_tag(self) -> None:
        exam = agent_exam.load_exam(ROOT / "scripts" / "agent_exam" / "exam_pl.json")
        self.assertEqual([task["id"] for task in agent_exam.select_tasks(exam, ["game_name,ui_button"], [])], ["game_name", "ui_button"])
        by_tag = {task["id"] for task in agent_exam.select_tasks(exam, [], ["ui"])}
        self.assertEqual(by_tag, {"ui_button"})
        self.assertEqual(len(agent_exam.select_tasks(exam, [], [])), len(exam["tasks"]))
        with self.assertRaises(SystemExit):
            agent_exam.select_tasks(exam, ["no_such_task"], [])

    def test_summary_has_one_line_per_task_and_the_total(self) -> None:
        tasks = [{"id": "a"}, {"id": "b"}]
        results = [{"task": "a", "config": "x", "passed": False, "over_budget": True, "totalTokens": 100},
                   {"task": "a", "config": "y", "passed": True, "totalTokens": 50},
                   {"task": "b", "config": "x", "passed": False, "totalTokens": 10}]
        lines = agent_exam.summary_lines(results, tasks)
        self.assertEqual(len(lines), 3)
        self.assertIn("1/2 tasks passed, 160 tokens", lines[-1])
        self.assertIn("x budget", lines[0])

    def test_manifest_matches_file_signatures(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            manifest = Path(folder) / "list.tsv"
            manifest.write_text("Assets/a.lua\t5\t133000000000000000\nbad line\n", encoding="utf-8")
            self.assertEqual(agent_exam.read_manifest(manifest), {"Assets/a.lua": "5:133000000000000000"})
            self.assertIsNone(agent_exam.read_manifest(Path(folder) / "missing.tsv"))


if __name__ == "__main__":
    unittest.main()
