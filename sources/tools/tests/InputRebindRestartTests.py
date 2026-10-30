"""Verify editor-captured bindings and reset across separate CLI processes."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "game/tests"))
from UIInputPackagedTests import recording


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--project', type=Path, required=True)
    args = parser.parse_args()
    project = args.project.resolve()
    profile = project / 'Saved/Bindings.profile'
    assert profile.is_file(), 'Headless editor did not persist the captured binding'
    script = project / 'Assets/Input/RebindProbe.lua'
    original = script.read_bytes()
    try:
        with tempfile.TemporaryDirectory(prefix='InputRebindRestart') as directory:
            cases = [('wrong-pad', 303, 0, False, False),
                     ('persisted-pad', 303, 1, True, False),
                     ('reset', 303, 1, False, True),
                     ('reset-restart', 4, 0, True, False),
                     ('reset-pad', 303, 1, False, False)]
            for name, key, pad, expected, reset in cases:
                source = ('function Ready(self, dt)\n'
                          ' local r = CallFunction("Input.OpenRebindProfile", '
                          '{context="/Game/Input/NewInputMappingContext.21kbinputcontext", '
                          f'path=[[{profile.as_posix()}]]}})\n assert(r.opened, r.error)\n')
                if reset:
                    source += ' local r = CallFunction("Input.ResetRebind", {})\n assert(r.reset, r.error)\n'
                script.write_text(source + 'end\n', encoding='utf-8')
                data = recording([{'keys': [key]}])
                # One v5 digital key: header(16), delta/count(8), key id(4), pad slot.
                data[28] = pad
                replay = Path(directory) / (name + '.inputrecording')
                replay.write_bytes(data)
                result = subprocess.run([str(args.cli), 'run', '--project', str(project),
                    '--scene', 'Assets/Scenes/Rebind.21kbscene', '--frames', '1',
                    '--input-replay', str(replay), '--report', 'input', '--json'],
                    capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
                assert result.returncode == 0, result.stderr
                report = json.loads(result.stdout)
                pressed = any(value['action'] == 'Move' and value['triggered'] for value in report['actions'])
                assert pressed == expected, (name, report)
                print(name, 'PASS')
    finally:
        script.write_bytes(original)


if __name__ == '__main__':
    main()
