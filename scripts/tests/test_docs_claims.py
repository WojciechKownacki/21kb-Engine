from __future__ import annotations

import re
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CLI_DISPATCH = ROOT / "sources" / "tools" / "src" / "CliDispatch.cpp"
COMPONENT_CATALOG = ROOT / "sources" / "editor" / "src" / "inspection" / "InspectorComponentCatalog.cpp"
AGENT_DOC = ROOT / "docs" / "agentic_scripting.md"
README = ROOT / "README.md"


def _cli_commands() -> set[str]:
    """Command names the kb_cli usage text lists: lines indented by two spaces, optionally `a / b`."""
    commands: set[str] = set()
    for line in CLI_DISPATCH.read_text(encoding="utf-8").splitlines():
        match = re.match(r'^(?:R"\()?  ([a-z][a-z0-9-]+)((?: / [a-z][a-z0-9-]+)*)(?:\s|$)', line)
        if match is None:
            continue
        commands.add(match.group(1))
        commands.update(name for name in match.group(2).split(" / ") if name)
    return commands


class DocumentedClaimsTests(unittest.TestCase):
    """What the public documents say about the tools has to match what the tools offer."""

    def test_feature_catalog_links_current_commands_and_evidence(self) -> None:
        catalog = json.loads((ROOT / 'docs/feature_catalog.json').read_text(encoding='utf-8'))
        self.assertEqual('21kb.feature-catalog/v1', catalog['schema'])
        for entry in catalog['entries']:
            self.assertFalse(set(entry['commands']) - _cli_commands(), entry['feature'])
            self.assertIn(entry['status'], {'tutorial-covered', 'separate-test', 'not-covered-by-tutorial'})
            self.assertTrue(entry['scope'])
            self.assertTrue((ROOT / entry['evidence']).is_file(), entry['feature'])

    def test_tutorial_recipe_uses_current_cli_and_standard_export(self) -> None:
        recipe = json.loads((ROOT / 'docs/tutorials/first_scene.json').read_text(encoding='utf-8'))
        self.assertEqual('Windows.x64', recipe['platform'])
        commands = [step[1] for step in recipe['steps'] if step[0] == '{CLI}']
        self.assertFalse(set(commands) - _cli_commands())
        self.assertEqual('new', commands[0])
        self.assertIn('Empty', recipe['steps'][0])
        self.assertEqual('{ENGINE}/scripts/package_game.py', recipe['steps'][-1][1])
        self.assertTrue((ROOT / 'scripts/run_first_scene_tutorial.py').is_file())

    def test_usage_text_is_parsed(self) -> None:
        commands = _cli_commands()
        for expected in ("new", "run", "batch", "vfx-emitter-remove", "lighting-quality"):
            self.assertIn(expected, commands)

    def test_every_cli_command_is_in_the_agent_guide(self) -> None:
        guide = AGENT_DOC.read_text(encoding="utf-8")
        documented = set(re.findall(r"`kb_cli ([a-z][a-z0-9-]+)", guide))
        missing = sorted(_cli_commands() - documented)
        self.assertEqual([], missing, "commands the usage text lists but docs/agentic_scripting.md does not")

    def test_the_guide_names_no_command_the_cli_lacks(self) -> None:
        guide = AGENT_DOC.read_text(encoding="utf-8")
        documented = set(re.findall(r"`kb_cli ([a-z][a-z0-9-]+)", guide))
        unknown = sorted(documented - _cli_commands() - {"help"})
        self.assertEqual([], unknown, "documented commands kb_cli does not list")

    def test_readme_names_every_add_component_category(self) -> None:
        categories = set(re.findall(r'\.category = "([A-Za-z ]+)"', COMPONENT_CATALOG.read_text(encoding="utf-8")))
        self.assertGreaterEqual(len(categories), 8)
        readme = README.read_text(encoding="utf-8")
        match = re.search(r"Add Component groups what the engine offers into (.*?), with search", readme, re.DOTALL)
        self.assertIsNotNone(match, "README no longer describes the Add Component categories")
        text = re.sub(r"\s+", " ", match.group(1))
        missing = sorted(category for category in categories if category not in text)
        self.assertEqual([], missing, "Add Component categories the README leaves out")


if __name__ == "__main__":
    unittest.main()
