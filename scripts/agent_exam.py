"""Exam for the editor's Agent panel: customer-style requests, run headless, judged by kb_cli.

    python scripts/agent_exam.py --config "AgentA:model-x:medium" --config "AgentB:model-y:medium"
    python scripts/agent_exam.py --tasks game_name,ui_button --config "AgentA:model-x:medium"
    python scripts/agent_exam.py --tag ui --config "AgentA:model-x:medium" --config "AgentB:model-y:medium" --mode all

Each task of the exam file names a project template, one or more requests (what a person who is not a developer
would type) and automatic checks. For every task and every configuration the script

  1. makes a fresh project from the template with kb_cli (plus the task's own setup commands),
  2. runs the editor headless on a generated scenario: the project is opened and the requests are sent to the
     chosen agent exactly as the Agent panel sends them (the scenario operation "agent_request"),
  3. reads what the editor measured (tokens, tool steps, failed steps, seconds, whether the agent reported an
     error), then judges the project with kb_cli and the files, and takes a picture of the result.

A configuration is "<agent>:<model>:<effort>", for example "AgentA:model-x:medium": the agent is named exactly as the
Agent panel lists it, the model and effort are what the panel offers for it, and an empty or "default" model or effort
leaves the agent's own.

Configurations are tried in the order given. By default (--mode first-pass) a task stops at the first configuration
that passes it, so the cheap model goes first and the next is paid for only when it fails; --mode all runs them all.
--tasks (ids) and --tag choose a subset, --list shows the tasks with their tags. Each request is held to --max-tokens and
--max-minutes: past either the editor stops the agent and the task fails with the reason "budget". Tokens are counted
live for an agent that reports the usage of its steps and when the turn ends for one that reports it only then.

Results go to <out>/results.json and <out>/results.md, rewritten after every run so a stopped exam keeps what it did.

The editor starts the agent CLI, which spends the account's quota, so a full exam is run on purpose. The editor and
the heavy kb_cli commands (run, capture) go through --wrapper (or the KB_EXAM_WRAPPER variable) when one is given: a
command that takes the machine's build queue or process guard before the real command, for example
"node <path to the queue tool> -- ". Only one exam runs at a time (a lock file next to the output folder).

Checks (each is an object with "check", and for most "count_at_least", default 1; "new" means the entity, file or
setting must be new or changed against the fresh project, default true):

  scene_entities  an entity: "name" (regex), "component", "behaviour" (regex of its script), "parent_component", "scene"
                  (a scene path; "*" for every scene of the project; the startup scene when absent)
  entity_show     an entity's values from `kb_cli entity show`: filters of scene_entities plus "regex" / "expr"
  files           files by "glob" (or a list of them, from the project folder): "contains" regex, "expr", "max_count",
                  "non_blank_picture", "magic" (the file starts with these bytes, hex), "min_bytes"
  setting         a project setting "key": "equals" | "one_of" | "regex", numbers compared as numbers
  plugin          "id" and "enabled"
  run             `kb_cli run`: "frames", "clean" (no diagnostics, default true), "output_matches" (regexes)
  validate        `kb_cli validate` finds no error
  answer          "regex" on the agent's own answer text
  unchanged       nothing in the project but Saved/ changed
  any_of          "checks": passes when one of them does

"expr" is a Python expression over the groups of "regex"/"contains" as floats g1, g2, ... (for example "g1 > g3", with abs, min and max);
the exam file is trusted input, like a test.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from png_readback import read_png  # noqa: E402

SCHEMA = "21kb.agent-exam/v1"
PROJECT_NAME = "Sample"
DEFAULT_MAX_MINUTES = 10
DEFAULT_MAX_TOKENS = 400_000
MAX_PROJECT_PATH = 140  # the engine refuses a project folder longer than this
SCENE = "Assets/Scenes/Main.21kbscene"


def normalized(text: str) -> str:
    return re.sub(r"[^a-z0-9]", "", text.lower())


# ---------------------------------------------------------------- configurations

def parse_config(text: str) -> dict:
    """AgentA:model-x:medium -> agent name, model, effort (empty for the agent's own)."""
    parts = text.split(":")
    if not 1 <= len(parts) <= 3 or not parts[0]:
        raise ValueError(f"configuration must look like agent:model:effort, got '{text}'")
    parts += [""] * (3 - len(parts))
    model, effort = ("" if value == "default" else value for value in parts[1:])
    agent = parts[0]
    label = "-".join(safe_name(value.lower()) for value in (agent, model or "default", effort or "default"))
    return {"text": text, "agent": agent, "model": model, "effort": effort, "label": label}


# ---------------------------------------------------------------- processes

def process_alive(pid: int) -> bool:
    if os.name == "nt":
        handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        ok = ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        ctypes.windll.kernel32.CloseHandle(handle)
        return bool(ok) and code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


class ExamLock:
    """One exam at a time: a file holding the pid of the exam that runs."""

    def __init__(self, path: Path):
        self.path = path

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        for _ in range(2):
            try:
                descriptor = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            except FileExistsError:
                try:
                    holder = int(self.path.read_text().strip())
                except (OSError, ValueError):
                    holder = 0
                if holder and process_alive(holder):
                    raise SystemExit(f"another exam is running (pid {holder}); only one runs at a time")
                self.path.unlink(missing_ok=True)
                continue
            os.write(descriptor, str(os.getpid()).encode())
            os.close(descriptor)
            return self
        raise SystemExit("the exam lock could not be taken")

    def __exit__(self, *exc):
        self.path.unlink(missing_ok=True)


def kill_tree(process: subprocess.Popen) -> None:
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
    else:
        process.kill()


def run_command(command: list[str], timeout: float, cwd: Path = ROOT) -> tuple[int, str]:
    """Runs a command, returns (exit code, stdout and stderr); 124 when it ran past the timeout."""
    process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        kill_tree(process)
        output, _ = process.communicate()
        return 124, output.decode("utf-8", "replace") + "\n[timed out]"
    return process.returncode, output.decode("utf-8", "replace")


# ---------------------------------------------------------------- one project, read through kb_cli

SCENE_LINE = re.compile(r"^\s*\[(\d+)\]\s+(.*)$")
NODE_TEXT = re.compile(
    r"^(?P<name>.*?)(?: \(parent (?P<parent>\d+)\))?(?: components: (?P<components>.*?))?"
    r"(?: behaviou?rs?: (?P<behaviours>.*))?$")


class Entity:
    def __init__(self, index: int, name: str, parent: int | None, components: list[str], behaviours: str):
        self.index = index
        self.name = name
        self.parent = parent
        self.components = components
        self.behaviours = behaviours
        self.path = name

    def has_component(self, wanted: str) -> bool:
        return normalized(wanted) in {normalized(component) for component in self.components}


def parse_scene_list(text: str) -> list[Entity]:
    """The nodes `kb_cli scene-list` prints: '[3] Name (parent 1) components: A, B behaviour: ... @ x,y,z'."""
    entities: list[Entity] = []
    for line in text.splitlines():
        match = SCENE_LINE.match(line)
        if match is None:
            continue
        body = match.group(2)
        at = body.rfind(" @ ")
        if at >= 0:
            body = body[:at]
        node = NODE_TEXT.match(body)
        if node is None:
            continue
        components = [part.strip() for part in (node.group("components") or "").split(",") if part.strip()]
        entities.append(Entity(int(match.group(1)), node.group("name").strip(),
                               int(node.group("parent")) if node.group("parent") else None,
                               components, node.group("behaviours") or ""))
    by_index = {entity.index: entity for entity in entities}
    for entity in entities:
        names, current, guard = [entity.name], entity, 0
        while current.parent is not None and current.parent in by_index and guard < 64:
            current = by_index[current.parent]
            names.append(current.name)
            guard += 1
        entity.path = "/".join(reversed(names))
    return entities


class Project:
    """A project folder and what kb_cli says about it; answers are kept, so a check costs one call."""

    def __init__(self, folder: Path, cli: Path, wrapper: list[str]):
        self.folder = folder
        self.cli = cli
        self.wrapper = wrapper
        self.cache: dict = {}

    def kb_cli(self, *arguments: str, heavy: bool = False, timeout: float = 240) -> tuple[int, str]:
        command = [str(self.cli), *arguments, "--project", str(self.folder)]
        return run_command((self.wrapper if heavy else []) + command, timeout)

    def cached(self, key, build):
        if key not in self.cache:
            self.cache[key] = build()
        return self.cache[key]

    def entities(self, scene: str | None = None) -> list[Entity]:
        def build():
            arguments = ["scene-list"] + (["--scene", scene] if scene else [])
            code, text = self.kb_cli(*arguments)
            return parse_scene_list(text) if code == 0 else []
        return self.cached(("entities", scene), build)

    def show(self, path: str, scene: str | None = None) -> str:
        def build():
            arguments = ["entity", "show", "--name", path] + (["--scene", scene] if scene else [])
            code, text = self.kb_cli(*arguments)
            return text if code == 0 else ""
        return self.cached(("show", path, scene), build)

    def settings(self) -> dict[str, str]:
        def build():
            code, text = self.kb_cli("project-settings")
            pairs = (line.partition("=") for line in text.splitlines())
            return {key.strip(): value.strip() for key, separator, value in pairs if separator}
        return self.cached("settings", build)

    def plugins(self) -> dict[str, bool]:
        def build():
            code, text = self.kb_cli("plugins")
            return {match.group(2): match.group(1) == "on"
                    for match in (re.match(r"\[(on|off)\]\s+(\S+)", line) for line in text.splitlines()) if match}
        return self.cached("plugins", build)

    def files(self) -> dict[str, str]:
        """Every file of the project (not Saved/) with its size and write time, the way the editor lists them."""
        def build():
            found = {}
            for path in sorted(self.folder.rglob("*")):
                relative = path.relative_to(self.folder).as_posix()
                if path.is_file() and not relative.startswith("Saved/"):
                    info = path.stat()
                    found[relative] = signature(info.st_size, info.st_mtime_ns // 100 + FILETIME_EPOCH_TICKS)
            return found
        return self.cached("files", build)


FILETIME_EPOCH_TICKS = 116444736000000000  # 100 ns ticks from 1601 to 1970


def signature(size: int, ticks: int) -> str:
    return f"{size}:{ticks}"


def read_manifest(path: Path) -> dict[str, str] | None:
    """The files the project had when the first request was sent (the editor wrote automation/pre_agent_files.tsv)."""
    if not path.is_file():
        return None
    found = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split("	")
        if len(parts) == 3 and parts[1].isdigit() and parts[2].lstrip("-").isdigit():
            found[parts[0]] = signature(int(parts[1]), int(parts[2]))
    return found


# ---------------------------------------------------------------- checks

class Verdict:
    def __init__(self, ok: bool, detail: str):
        self.ok = ok
        self.detail = detail


def text_matches(text: str, pattern: str | None, expr: str | None) -> bool:
    """The regex found in the text and, when there is an expression, true for the groups of one match."""
    if pattern is None:
        return True
    for match in re.finditer(pattern, text, re.IGNORECASE | re.MULTILINE):
        if expr is None:
            return True
        try:
            values = {f"g{number}": float(group) for number, group in enumerate(match.groups(), 1) if group is not None}
            if eval(expr, {"__builtins__": {"abs": abs, "min": min, "max": max}}, values):  # noqa: S307 - the exam file is trusted input
                return True
        except (ValueError, TypeError, NameError, ZeroDivisionError):
            continue
    return False


class Judge:
    """Everything a check may look at: the project the agent worked on, the fresh one it started from, and the answers."""

    def __init__(self, project: Project, baseline: Project, baseline_files: dict[str, str], answers: list[str], picture_folder: Path):
        self.project = project
        self.baseline = baseline
        self.baseline_files = baseline_files
        self.answers = answers
        self.picture_folder = picture_folder

    # -- entities
    def scenes(self, check: dict) -> list[str | None]:
        """The scenes a check looks in: the startup scene, one named scene, or "*" for every scene of the project."""
        scene = check.get("scene")
        if scene != "*":
            return [scene]
        assets = self.project.folder / "Assets"
        return ["/Game/" + path.relative_to(assets).as_posix() for path in sorted(assets.rglob("*.21kbscene"))]

    def entity_matches(self, check: dict) -> list[tuple[str | None, Entity]]:
        found = []
        for scene in self.scenes(check):
            entities = self.project.entities(scene)
            before = {entity.path: entity for entity in self.baseline.entities(scene)}
            for entity in entities:
                if check.get("name") and not re.search(check["name"], entity.name, re.IGNORECASE):
                    continue
                if check.get("component") and not entity.has_component(check["component"]):
                    continue
                if check.get("behaviour") and not re.search(check["behaviour"], entity.behaviours, re.IGNORECASE):
                    continue
                if check.get("parent_component"):
                    parent = next((other for other in entities if other.index == entity.parent), None)
                    if parent is None or not parent.has_component(check["parent_component"]):
                        continue
                old = before.get(entity.path) if check.get("new", True) else None
                if old is not None and check["check"] == "scene_entities":
                    # An entity of the fresh project counts only when it gained what the check asks for.
                    gained_component = check.get("component") and not old.has_component(check["component"])
                    gained_behaviour = check.get("behaviour") and not re.search(check["behaviour"], old.behaviours, re.IGNORECASE)
                    if not (gained_component or gained_behaviour):
                        continue
                found.append((scene, entity))
        return found

    def scene_entities(self, check: dict) -> Verdict:
        found = self.entity_matches(check)
        needed = check.get("count_at_least", 1)
        return Verdict(len(found) >= needed, f"{len(found)} of {needed} needed: " + ", ".join(entity.path for _, entity in found[:8]))

    def entity_show(self, check: dict) -> Verdict:
        matching = []
        for scene, entity in self.entity_matches(check):
            shown = self.project.show(entity.path, scene)
            if check.get("new", True) and any(old.path == entity.path for old in self.baseline.entities(scene)):
                if shown == self.baseline.show(entity.path, scene):
                    continue
            if text_matches(shown, check.get("regex"), check.get("expr")):
                matching.append(entity.path)
        needed = check.get("count_at_least", 1)
        return Verdict(len(matching) >= needed, f"{len(matching)} of {needed} needed: " + ", ".join(matching[:8]))

    # -- files
    def changed_files(self) -> list[str]:
        now = self.project.files()
        return [path for path, digest in now.items() if self.baseline_files.get(path) != digest] + \
               [path for path in self.baseline_files if path not in now]

    def files(self, check: dict) -> Verdict:
        patterns = check["glob"] if isinstance(check["glob"], list) else [check["glob"]]
        paths = sorted({path.relative_to(self.project.folder).as_posix()
                        for pattern in patterns for path in self.project.folder.glob(pattern) if path.is_file()
                        and (pattern.startswith("Saved/") or not path.relative_to(self.project.folder).as_posix().startswith("Saved/"))})
        if check.get("new", True):
            paths = [path for path in paths if self.baseline_files.get(path) != self.project.files().get(path) or path.startswith("Saved/")]
        matching = []
        for path in paths:
            if check.get("magic") is not None:
                magic = bytes.fromhex(check["magic"])
                if (self.project.folder / path).read_bytes()[:len(magic)] != magic:
                    continue
            if check.get("min_bytes") is not None and (self.project.folder / path).stat().st_size < check["min_bytes"]:
                continue
            if check.get("contains") is not None:
                text = (self.project.folder / path).read_text(encoding="utf-8", errors="replace")
                if not text_matches(text, check["contains"], check.get("expr")):
                    continue
            if check.get("non_blank_picture") and not picture_is_not_blank((self.project.folder / path)):
                continue
            matching.append(path)
        if "max_count" in check:
            return Verdict(len(matching) <= check["max_count"], f"{len(matching)} found, at most {check['max_count']}: " + ", ".join(matching[:8]))
        needed = check.get("count_at_least", 1)
        return Verdict(len(matching) >= needed, f"{len(matching)} of {needed} needed: " + ", ".join(matching[:8]))

    def unchanged(self, check: dict) -> Verdict:
        changed = self.changed_files()
        return Verdict(not changed, "no file changed" if not changed else "changed: " + ", ".join(changed[:8]))

    # -- settings and plugins
    def setting(self, check: dict) -> Verdict:
        key = check["key"]
        value = self.project.settings().get(key)
        if value is None:
            return Verdict(False, f"{key} is not a setting")

        def same(first: str, second: str) -> bool:
            try:
                return abs(float(first) - float(second)) < 1e-6
            except ValueError:
                return normalized(first) == normalized(second)
        if "equals" in check:
            ok = same(value, str(check["equals"]))
        elif "one_of" in check:
            ok = any(same(value, str(option)) for option in check["one_of"])
        elif "regex" in check:
            ok = re.search(check["regex"], value, re.IGNORECASE) is not None
        else:
            ok = value != self.baseline.settings().get(key)
        return Verdict(ok, f"{key}={value}")

    def plugin(self, check: dict) -> Verdict:
        state = self.project.plugins().get(check["id"])
        return Verdict(state is not None and state == check.get("enabled", True), f"{check['id']} is {'on' if state else 'off' if state is not None else 'unknown'}")

    # -- running the game and the rest
    def run(self, check: dict) -> Verdict:
        frames = str(check.get("frames", 120))
        arguments = ["run", "--frames", frames, "--timeout-ms", "120000"]
        if check.get("scene"):
            arguments += ["--scene", check["scene"]]
        code, output = self.project.kb_cli(*arguments, heavy=True, timeout=300)
        diagnostics = re.search(r"(\d+) diagnostics", output)
        problems = []
        if check.get("clean", True):
            if code != 0:
                problems.append(f"exit code {code}")
            if diagnostics is None:
                problems.append("no summary line")
            elif int(diagnostics.group(1)) != 0:
                problems.append(f"{diagnostics.group(1)} diagnostics")
        for pattern in check.get("output_matches", []):
            if re.search(pattern, output, re.IGNORECASE | re.MULTILINE) is None:
                problems.append(f"no output matches {pattern}")
        return Verdict(not problems, "; ".join(problems) if problems else (diagnostics.group(0) if diagnostics else "ran"))

    def validate(self, check: dict) -> Verdict:
        code, output = self.project.kb_cli("validate", timeout=240)
        errors = [line for line in output.splitlines() if re.match(r"\s*(ERROR|error|FAIL)", line)]
        return Verdict(code == 0 and not errors, "clean" if code == 0 and not errors else (errors[0] if errors else f"exit code {code}"))

    def answer(self, check: dict) -> Verdict:
        text = "\n".join(self.answers)
        return Verdict(re.search(check["regex"], text, re.IGNORECASE | re.DOTALL) is not None, text[:160].replace("\n", " "))

    def any_of(self, check: dict) -> Verdict:
        verdicts = [self.evaluate(inner) for inner in check["checks"]]
        passed = [verdict for verdict in verdicts if verdict.ok]
        return Verdict(bool(passed), (passed[0] if passed else verdicts[0]).detail + (f" (+{len(verdicts) - 1} alternatives)" if len(verdicts) > 1 else ""))

    def evaluate(self, check: dict) -> Verdict:
        kinds = {"scene_entities": self.scene_entities, "entity_show": self.entity_show, "files": self.files, "setting": self.setting,
                 "plugin": self.plugin, "run": self.run, "validate": self.validate, "answer": self.answer,
                 "unchanged": self.unchanged, "any_of": self.any_of}
        handler = kinds.get(check.get("check", ""))
        if handler is None:
            return Verdict(False, f"unknown check '{check.get('check')}'")
        try:
            return handler(check)
        except (KeyError, re.error, OSError, ValueError) as error:
            return Verdict(False, f"check could not be evaluated: {error!r}")


def picture_is_not_blank(path: Path, minimum_colours: int = 24) -> bool:
    """A readable PNG whose pixels are not (nearly) one colour."""
    try:
        rows = read_png(path)
    except (OSError, ValueError, KeyError, EOFError, Exception):  # noqa: BLE001 - any unreadable picture is a blank one
        return False
    step = max(1, len(rows) // 90)
    colours = {pixel for row in rows[::step] for pixel in row[:: max(1, len(row) // 160)]}
    return len(colours) >= minimum_colours


# ---------------------------------------------------------------- one run: one task, one configuration

def safe_name(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9_-]", "_", text)[:90]


def make_source_project(task: dict, sources: Path, cli: Path) -> Path:
    """The fresh project of a task: kb_cli new with its template, then the task's setup commands."""
    folder = sources / task["id"]
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    code, output = run_command([str(cli), "new", "--parent", str(folder), "--name", PROJECT_NAME, "--template", task.get("template", "Basic3D")], 120)
    if code != 0:
        raise RuntimeError(f"kb_cli new failed for {task['id']}: {output.strip()[-300:]}")
    project = folder / PROJECT_NAME
    for setup in task.get("setup", []):
        code, output = run_command([str(cli), *setup, "--project", str(project)], 120)
        if code != 0:
            raise RuntimeError(f"setup {setup} failed for {task['id']}: {output.strip()[-300:]}")
    return project


def write_scenario(path: Path, source: Path, task: dict, config: dict, max_minutes: float = DEFAULT_MAX_MINUTES,
                   max_tokens: int = DEFAULT_MAX_TOKENS) -> None:
    steps = [{"op": "open_scene", "path": task.get("scene", SCENE)}]
    for request in task["requests"]:
        step = {"op": "agent_request", "agent": config["agent"], "text": request,
                "model": config["model"], "effort": config["effort"],
                "timeout_seconds": int(task.get("max_minutes", max_minutes) * 60),
                "max_tokens": int(task.get("max_tokens", max_tokens))}
        steps.append(step)
    scenario = {"schema": "21kb.editor-automation/v1", "initial_project": str(source / f"{PROJECT_NAME}.21kbproject"),
                "continueOnFailure": True, "steps": steps}
    path.write_text(json.dumps(scenario, indent=2), encoding="utf-8")


def read_requests(run_folder: Path) -> list[dict]:
    path = run_folder / "automation" / "agent_requests.jsonl"
    if not path.is_file():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def read_steps(workspace: Path) -> list[dict]:
    """The tool steps of the conversation the agent had, for finding out what a failed task did."""
    steps = []
    for file in sorted((workspace / "Saved" / "Agent" / "Conversations").glob("*.json")):
        try:
            document = json.loads(file.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        steps += [{"text": entry.get("text", "")[:200], "failed": bool(entry.get("failed"))}
                  for entry in document.get("entries", []) if entry.get("kind") == "tool"]
    return steps[:120]


def run_task(task: dict, config: dict, args, source: Path, runs: Path, wrapper: list[str], cli: Path, editor: Path) -> dict:
    name = safe_name(f"{task['id']}__{config['label']}")
    scenario = args.out / "scenarios" / f"{name}.json"
    scenario.parent.mkdir(parents=True, exist_ok=True)
    write_scenario(scenario, source, task, config, args.max_minutes, args.max_tokens)
    run_folder = runs / name
    result = {"task": task["id"], "area": task.get("area", ""), "config": config["text"], "expected": task.get("expected", "pass"),
              "note": task.get("note", ""), "requests": task["requests"]}
    allowed = len(task["requests"]) * task.get("max_minutes", args.max_minutes) * 60 + 300
    command = wrapper + [str(editor), "--selftest-scenario", str(scenario), "--selftest-root", str(runs), "--selftest-task", name]
    started = time.monotonic()
    code, output = run_command(command, allowed)
    result["editor_exit_code"] = code
    result["wall_seconds"] = round(time.monotonic() - started, 1)
    workspace = run_folder / "workspace"
    measures = read_requests(run_folder)
    result["requests_run"] = len(measures)
    for key in ("inputTokens", "outputTokens", "totalTokens", "toolSteps", "failedSteps", "seconds", "costUsd"):
        result[key] = round(sum(float(item.get(key, 0)) for item in measures), 4 if key == "costUsd" else 1 if key == "seconds" else 0)
    result["over_budget"] = any(item.get("timedOut") or item.get("overTokenBudget") for item in measures)
    result["agent_error"] = any(item.get("reportedError") for item in measures) and not result["over_budget"]
    result["agent_messages"] = [item.get("error") or item.get("answer", "")[:300] for item in measures]
    report = run_folder / "report.txt"
    result["scenario_failures"] = [line for line in report.read_text(encoding="utf-8", errors="replace").splitlines()
                                   if line.startswith("[FAIL]")] if report.is_file() else ["no report: the editor did not finish"]
    result["steps"] = read_steps(workspace)
    result["workspace"] = str(workspace)
    if not workspace.is_dir() or len(measures) < len(task["requests"]):
        result["checks"] = []
        result["passed"] = False
        result["failure"] = "the agent was not run" if not measures else "a request did not run"
        result["editor_output_tail"] = output[-600:]
        return result
    project = Project(workspace, cli, wrapper)
    baseline = Project(source, cli, wrapper)
    baseline_files = read_manifest(run_folder / "automation" / "pre_agent_files.tsv")
    if baseline_files is None:
        baseline_files = baseline.files()
    judge = Judge(project, baseline, baseline_files, [item.get("answer", "") for item in measures], run_folder)
    checks = []
    for number, check in enumerate(task.get("checks", []), 1):
        verdict = judge.evaluate(check)
        checks.append({"number": number, "check": check.get("check"), "label": check.get("label", ""), "ok": verdict.ok, "detail": verdict.detail})
    result["checks"] = checks
    result["passed"] = bool(checks) and all(check["ok"] for check in checks) and not result["over_budget"]
    if result["over_budget"]:
        result["failure"] = "budget"
    picture = run_folder / "picture.png"
    code, text = project.kb_cli("capture", "--out", str(picture), "--frames", "90", "--width", "640", "--height", "360", heavy=True, timeout=300)
    result["picture"] = str(picture) if code == 0 and picture.is_file() else ""
    if not result["picture"]:
        result["picture_error"] = text.strip()[-200:]
    return result


# ---------------------------------------------------------------- reports

def table_row(cells: list[str]) -> str:
    return "| " + " | ".join(cell.replace("|", "/") for cell in cells) + " |"


def outcome_text(result: dict) -> str:
    outcome = "PASS" if result.get("passed") else "FAIL (budget)" if result.get("over_budget") else "FAIL"
    if result.get("expected") == "fail":
        outcome += " (was expected to fail)" if result.get("passed") else " (expected to fail today)"
    if result.get("agent_error"):
        outcome += ", agent error"
    return outcome


def summary_lines(results: list[dict], tasks: list[dict]) -> list[str]:
    """One line per task: who passed it (or that nobody did), with the tokens spent on it."""
    lines = []
    for task in tasks:
        mine = [result for result in results if result["task"] == task["id"]]
        if not mine:
            continue
        winner = next((result for result in mine if result.get("passed")), None)
        tokens = sum(int(result.get("totalTokens", 0)) for result in mine)
        tried = ", ".join(f"{result['config']} {'pass' if result.get('passed') else 'budget' if result.get('over_budget') else 'fail'}" for result in mine)
        lines.append(f"{'PASS' if winner else 'FAIL'}  {task['id']:26} {tokens:>9,} tokens  ({tried})")
    total = sum(int(result.get("totalTokens", 0)) for result in results)
    passed = len({result["task"] for result in results if result.get("passed")})
    ran = len({result["task"] for result in results})
    lines.append(f"{passed}/{ran} tasks passed, {total:,} tokens spent in {len(results)} run(s)")
    return lines


def failure_reason(result: dict) -> str:
    if result.get("over_budget"):
        return "over budget (tokens or time)"
    failed = [f"check {check['number']} {check['check']} {check['label']}: {check['detail']}" for check in result.get("checks", []) if not check["ok"]]
    return "; ".join(failed) or result.get("failure", "") or "; ".join(result.get("scenario_failures", []))


def write_reports(out: Path, exam: dict, results: list[dict], configs: list[dict], tasks: list[dict]) -> None:
    (out / "results.json").write_text(json.dumps({"schema": "21kb.agent-exam-results/v1", "exam": exam.get("title", ""), "results": results},
                                                 indent=2, ensure_ascii=False), encoding="utf-8")
    lines = [f"# Agent exam: {exam.get('title', '')}", "", "```", *summary_lines(results, tasks), "```", "",
             f"{len(results)} run(s). Tokens are input plus output as the Agent panel counts them.", "",
             table_row(["Task", "Model", "Result", "Checks", "Tokens", "Steps", "Failed steps", "Seconds", "Picture"]),
             table_row(["---"] * 9)]
    for result in results:
        checks = result.get("checks", [])
        picture = Path(result["picture"]).relative_to(out).as_posix() if result.get("picture") else "-"
        lines.append(table_row([result["task"], result["config"], outcome_text(result), f"{sum(c['ok'] for c in checks)}/{len(checks)}",
                                f"{int(result.get('totalTokens', 0)):,}", str(int(result.get("toolSteps", 0))),
                                str(int(result.get("failedSteps", 0))), str(int(result.get("seconds", 0))), picture]))
    lines += ["", "## Per model", "", table_row(["Model", "Runs", "Passed", "Tokens", "Tokens per run", "Steps per run", "Failed steps"]),
              table_row(["---"] * 7)]
    for config in configs:
        mine = [result for result in results if result["config"] == config["text"]]
        if not mine:
            continue
        tokens = sum(int(result.get("totalTokens", 0)) for result in mine)
        lines.append(table_row([config["text"], str(len(mine)), str(sum(bool(r.get("passed")) for r in mine)), f"{tokens:,}",
                                f"{tokens // len(mine):,}", f"{sum(int(r.get('toolSteps', 0)) for r in mine) / len(mine):.1f}",
                                str(sum(int(r.get("failedSteps", 0)) for r in mine))]))
    failing = [result for result in results if not result.get("passed")]
    if failing:
        lines += ["", "## What failed", ""]
        for result in failing:
            lines.append(f"- **{result['task']}** ({result['config']}): {failure_reason(result)}")
            for message in result.get("agent_messages", [])[:1]:
                if message:
                    lines.append(f"  - agent said: {message[:200]}")
    (out / "results.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------- main

def load_exam(path: Path) -> dict:
    exam = json.loads(path.read_text(encoding="utf-8"))
    if exam.get("schema") != SCHEMA:
        raise SystemExit(f"{path} is not a {SCHEMA} file")
    seen = set()
    for task in exam["tasks"]:
        if not re.fullmatch(r"[A-Za-z0-9_]{1,40}", task.get("id", "")) or task["id"] in seen:
            raise SystemExit(f"task id '{task.get('id')}' is missing, repeated or has odd characters")
        seen.add(task["id"])
        if not task.get("requests") or not task.get("checks"):
            raise SystemExit(f"task {task['id']} needs requests and checks")
        task["tags"] = sorted({normalized(tag) for tag in task.get("tags", [])})
    return exam


def select_tasks(exam: dict, ids: list[str], tags: list[str]) -> list[dict]:
    """The tasks named by --tasks and/or carrying one of the --tag tags (all of them when neither is given)."""
    wanted_ids = {name.strip() for group in ids for name in group.split(",") if name.strip()}
    wanted_tags = {normalized(name) for group in tags for name in group.split(",") if name.strip()}
    known_tags = {tag for task in exam["tasks"] for tag in task["tags"]}
    if wanted_ids - {task["id"] for task in exam["tasks"]}:
        raise SystemExit("unknown task id(s): " + ", ".join(sorted(wanted_ids - {task["id"] for task in exam["tasks"]})))
    if wanted_tags - known_tags:
        raise SystemExit("unknown tag(s): " + ", ".join(sorted(wanted_tags - known_tags)) + "; known: " + ", ".join(sorted(known_tags)))
    if not wanted_ids and not wanted_tags:
        return list(exam["tasks"])
    return [task for task in exam["tasks"] if task["id"] in wanted_ids or wanted_tags & set(task["tags"])]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exam", type=Path, default=ROOT / "scripts" / "agent_exam" / "exam_pl.json")
    parser.add_argument("--config", action="append", help="agent:model:effort, repeatable, in the order to try")
    parser.add_argument("--mode", choices=("first-pass", "all"), default="first-pass",
                        help="first-pass: for each task try the configurations in order and stop at the first that passes (default); "
                             "all: run every configuration on every task")
    parser.add_argument("--tasks", action="append", default=[], help="task ids, comma separated (repeatable)")
    parser.add_argument("--tag", action="append", default=[], help="run the tasks with this tag (comma separated, repeatable)")
    parser.add_argument("--max-tokens", type=int, default=DEFAULT_MAX_TOKENS,
                        help="token limit per request; over it the agent is stopped and the task fails as budget")
    parser.add_argument("--max-minutes", type=float, default=DEFAULT_MAX_MINUTES, help="time limit per request, with the same outcome")
    parser.add_argument("--out", type=Path, help="results folder (default build/agent-exam/<time>)")
    parser.add_argument("--wrapper", default=os.environ.get("KB_EXAM_WRAPPER", ""),
                        help="command put before the editor and the heavy kb_cli commands (default $KB_EXAM_WRAPPER)")
    parser.add_argument("--bin", type=Path, default=ROOT / "build" / "perf-release" / "bin", help="folder of kb_editor and kb_cli")
    parser.add_argument("--list", action="store_true", help="list the (selected) tasks with their tags and stop")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    exam = load_exam(args.exam)
    tasks = select_tasks(exam, args.tasks, args.tag)
    if args.list:
        for task in tasks:
            print(f"{task['id']:26} [{', '.join(task['tags'])}] {task['requests'][0][:60]}")
        return 0
    if not args.config:
        parser.error("give at least one --config agent:model:effort")
    configs = [parse_config(text) for text in args.config]
    suffix = ".exe" if os.name == "nt" else ""
    cli, editor = (args.bin / ("kb_cli" + suffix)).resolve(), (args.bin / ("kb_editor" + suffix)).resolve()
    for tool in (cli, editor):
        if not tool.is_file():
            raise SystemExit(f"{tool} is missing; build kb_cli and kb_editor first")
    wrapper = shlex.split(args.wrapper, posix=(os.name != "nt")) if args.wrapper else []
    args.out = (args.out or ROOT / "build" / "agent-exam" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")).resolve()
    longest = max(len(str(args.out / "runs" / safe_name(f"{task['id']}__{config['label']}") / "workspace")) for task in tasks for config in configs)
    if longest > MAX_PROJECT_PATH:
        raise SystemExit(f"the projects would be {longest} characters deep and the engine takes at most {MAX_PROJECT_PATH}: choose a shorter --out")

    with ExamLock(ROOT / "build" / "agent_exam.lock"):
        args.out.mkdir(parents=True, exist_ok=True)
        sources, runs = args.out / "sources", args.out / "runs"
        results: list[dict] = []
        for task in tasks:
            source = make_source_project(task, sources, cli)
            for config in configs:
                print(f"[exam] {task['id']} with {config['text']} ...", flush=True)
                result = run_task(task, config, args, source, runs, wrapper, cli, editor)
                results.append(result)
                print(f"[exam]   {outcome_text(result)}  tokens {int(result['totalTokens'])}  steps {int(result['toolSteps'])}"
                      f"  failed {int(result['failedSteps'])}  {int(result['seconds'])} s", flush=True)
                write_reports(args.out, exam, results, configs, tasks)
                if args.mode == "first-pass" and result["passed"]:
                    break
    print("[exam] summary")
    for line in summary_lines(results, tasks):
        print("[exam]   " + line)
    print(f"[exam] results: {args.out / 'results.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
