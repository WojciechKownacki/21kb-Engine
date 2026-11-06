"""scene-attach --set stores exposed script variables in the scene and applies them when the behaviour starts."""
import argparse
from pathlib import Path
import subprocess
import tempfile

SCENE = 'Assets/Scenes/Main.21kbscene'
SCRIPT = """
-- @expose speed Float = 3.5
-- @expose lives Int = 3
-- @expose label String = "idle"
-- @expose armed Bool = false

function Created(self, dt)
    Log('vars speed=' .. tostring(self:GetVariable('speed')) .. ' lives=' .. tostring(self:GetVariable('lives'))
        .. ' label=' .. tostring(self:GetVariable('label')) .. ' armed=' .. tostring(self:GetVariable('armed')))
end

function Tick(self, dt) end
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve(strict=True)

    def run(*words, expect=0):
        result = subprocess.run([str(cli), *map(str, words)], capture_output=True,
                                encoding='utf-8', errors='replace', timeout=60)
        assert result.returncode == expect, (words, result.returncode, result.stdout, result.stderr)
        return result

    with tempfile.TemporaryDirectory(prefix='SceneAttachVariables ') as directory:
        parent = Path(directory)
        run('new', '--parent', parent, '--name', 'Tuned', '--template', 'Empty')
        project = parent / 'Tuned'
        run('entity', 'add', '--project', project, '--name', 'Hero')
        logic = project / 'Assets/Logic'
        logic.mkdir(parents=True, exist_ok=True)
        (logic / 'Hero.lua').write_text(SCRIPT, encoding='utf-8')
        (logic / 'Other.lua').write_text('-- @expose speed Float = 1.0\nfunction Tick(self, dt) end\n', encoding='utf-8')

        def attach(*extra, script='/Game/Logic/Hero.lua', expect=0):
            return run('scene-attach', '--project', project, '--scene', SCENE, '--node', 'Hero',
                       '--script', script, *extra, expect=expect)

        def listing():
            return run('scene-list', '--project', project, '--scene', SCENE).stdout

        # A value that differs from the script default is stored; one equal to it stores nothing.
        result = attach('--set', 'speed=12.5', '--set', 'lives=3', '--set', 'label=ready steady', '--set', 'armed=true')
        assert 'speed = 12.5' in result.stdout and 'lives = 3 (script default, no override stored)' in result.stdout, result.stdout
        stored = listing()
        assert 'variables: speed=12.5, label=ready steady, armed=true' in stored and 'lives' not in stored.split('variables:')[1], stored

        # The overrides reach the script when the behaviour starts.
        report = run('run', '--project', project, '--scene', SCENE, '--frames', '3').stdout
        assert 'vars speed=12.5 lives=3 label=ready steady armed=true' in report, report

        # Attaching the same script again keeps the node's values; --set changes one of them.
        attach()
        assert 'variables: speed=12.5, label=ready steady, armed=true' in listing(), listing()
        attach('--set', 'speed=3.5')
        assert 'variables: label=ready steady, armed=true' in listing(), listing()

        # A name the script does not expose, a value of the wrong type and a malformed assignment are errors, and
        # none of them touches the scene.
        before = listing()
        unknown = attach('--set', 'speeed=2', expect=1)
        assert "exposes no variable 'speeed'" in unknown.stderr and 'speed (Float)' in unknown.stderr and 'lives (Int)' in unknown.stderr, unknown.stderr
        wrong = attach('--set', 'lives=many', expect=1)
        assert "'many' is not a valid Int for variable 'lives'" in wrong.stderr, wrong.stderr
        assert 'name=value' in attach('--set', 'armed', expect=1).stderr
        assert listing() == before, 'a rejected --set changed the scene'

        # Another script starts without the overrides of the first one.
        attach('--replace', script='/Game/Logic/Other.lua')
        assert 'variables:' not in listing(), listing()
    print('scene-attach --set tests passed')


if __name__ == '__main__':
    main()
