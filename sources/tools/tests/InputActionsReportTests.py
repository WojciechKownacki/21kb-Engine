"""Check CLI diagnostics against input assets saved by the headless editor."""
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
    cases = [
        ('digital', [{'keys': [4]}], 1.0, 'triggered'),
        ('analog', [{'analog': [(340, .75)]}], .375, None),
        ('release', [{'keys': [4]}, {}], 0.0, 'completed'),
        ('idle', [{'keys': [4]}, {}, {}], 0.0, None),
    ]
    with tempfile.TemporaryDirectory(prefix='InputActionsReport') as directory:
        for name, frames, expected, phase in cases:
            replay = Path(directory) / (name + '.inputrecording')
            replay.write_bytes(recording(frames))
            result = subprocess.run([
                str(args.cli), 'run', '--project', str(args.project),
                '--scene', 'Assets/Scenes/InputActions.21kbscene',
                '--frames', str(len(frames)), '--input-replay', str(replay),
                '--report', 'input', '--json'], capture_output=True, text=True,
                encoding='utf-8', errors='replace', timeout=60)
            assert result.returncode == 0, result.stderr
            report = json.loads(result.stdout)
            assert report['succeeded'] and report['action_snapshot_frame'] == len(frames)
            assert report['action_snapshot_player'] == 0
            if phase is not None:
                assert any(event['action'] == 'Move' and event['phase'] == phase
                           for event in report['action_events']), report
            if name != 'release':
                value = next(value for value in report['actions'] if value['action'] == 'Move')
                assert value['value_type'] == 'Axis1D' and abs(value['x'] - expected) < .001, report
                if name in ('analog', 'idle'):
                    assert not value['triggered'], report
            if name == 'idle':
                assert not report['action_events'], report
            print(name, 'PASS', json.dumps(report))


if __name__ == '__main__':
    main()
