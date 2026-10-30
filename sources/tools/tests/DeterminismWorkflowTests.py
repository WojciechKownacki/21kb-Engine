"""Run seeded fixed-tick Lua content in separate real CLI processes."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve(strict=True)

    def run(*words):
        result = subprocess.run([str(cli), *map(str, words)], capture_output=True,
                                encoding='utf-8', errors='replace', timeout=30)
        assert result.returncode == 0, (words, result.returncode, result.stdout, result.stderr)
        return result

    with tempfile.TemporaryDirectory(prefix='DeterminismReplay ') as directory:
        parent = Path(directory)
        run('new', '--parent', parent, '--name', 'SampleScene', '--template', 'Empty')
        project = parent / 'SampleScene'
        run('entity', 'add', '--project', project, '--name', 'Probe')
        run('component', 'add', '--project', project, '--entity', 'Probe', '--type', 'Rigidbody',
            '--set', 'useGravity=false', '--set', 'linearVelocity=1,0,0')
        run('component', 'add', '--project', project, '--entity', 'Probe', '--type', 'Collider')
        script = project / 'Assets/Logic/Probe.lua'
        script.parent.mkdir(parents=True, exist_ok=True)
        script.write_text("""
local count = 0
local stream = {streamSeed=305419896, streamCounter=0}
local sum = 0
function FixedTick(self, dt)
    count = count + 1
    local nextValue, err = CallFunction('Math.RandomNextUInt32', stream)
    assert(nextValue ~= nil, err)
    stream = {streamSeed=nextValue.streamSeed, streamCounter=nextValue.streamCounter}
    sum = sum + nextValue.value
    assert(math.abs(dt - 1/60) < 0.000001, 'fixed delta')
end
function Tick(self, dt)
    local position = Transform.GetPosition(self.entity)
    local velocity = Physics.GetVelocity(self.entity)
    assert(velocity.found and math.abs(velocity.x - 1) <= 0.001, 'physics velocity golden tolerance 0.001')
    assert(math.abs(position.x - count/60) <= 0.001 and math.abs(position.y) <= 0.001,
        'physics position golden tolerance 0.001')
    Log('golden fixed=' .. count .. ' rng=' .. string.format('%.0f', sum))
end
""", encoding='utf-8')
        run('scene-attach', '--project', project, '--scene', 'Assets/Scenes/Main.21kbscene',
            '--node', 'Probe', '--script', '/Game/Logic/Probe.lua')
        results = []
        for _ in range(2):
            result = run('run', '--project', project, '--frames', '30', '--dt', '0.033333335',
                         '--deterministic', '--report', 'determinism', '--json')
            report = json.loads(result.stdout)
            assert report['schema'] == '21kb.determinism/v1' and report['succeeded']
            assert '[module] active Physics.Jolt' in result.stderr, result.stderr
            assert report['enabled'] and report['completed_frames'] == 30
            assert report['fixed_step_index'] == '60', report
            assert 'positive' in report['frame_delta_requirement']
            assert not report['cross_platform_float_bitwise']
            assert report['backend_absolute_tolerance'] is None
            assert 'scenario' in report['tolerance_basis']
            assert all(report[key] for key in ('simulation', 'rng', 'fixed_tick', 'backends'))
            signatures = re.findall(r'golden fixed=(\d+) rng=(\d+)', result.stderr)
            assert len(signatures) == 30 and signatures[-1][0] == '60', result.stderr
            results.append(signatures)
        assert results[0] == results[1], results
        expected = 122439166101  # Golden sum of the first 60 uint32 samples.
        assert int(results[0][-1][1]) == expected, (results[0][-1], expected)
        disabled = json.loads(run('run', '--project', project, '--frames', '1',
                                  '--report', 'determinism').stdout)
        assert disabled['succeeded'] and not disabled['enabled']
        print(json.dumps({'status': 'PASS', 'frames': 30, 'fixed_ticks': 60,
                          'separate_process_runs': 2, 'rng_sum': expected, 'physics_absolute_tolerance': 0.001,
                          'scope': 'actual CLI, Lua fixed-tick RNG and Jolt linear-motion numeric golden; no cross-host execution claim'}))


if __name__ == '__main__':
    main()
