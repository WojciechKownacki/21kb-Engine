"""Check GUI/CLI profile authoring and rejection without damaging the saved profile."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('editor', 'cli', 'test-root'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='PostProcess', dir=args.test_root)).resolve()
    commands = []
    def run(exe, *tail, expected=0):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, capture_output=True, encoding='utf-8', errors='replace', timeout=180)
        commands.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        assert result.returncode == expected, commands[-1]
    run(args.editor, '--selftest-scenario', Path(__file__).with_name('HeadlessPostProcessScenario.json').resolve(),
        '--selftest-root', root, '--selftest-task', 'GUI')
    project = root / 'GUI/workspace'
    profile = project / 'Assets/PostProcessProfile.kbppfx'
    common = ['post-process-set', '--project', project, '--profile', '/Game/PostProcessProfile.kbppfx']
    for field, value in [('bloomStrength', '0.25'), ('bloomThreshold', '2'), ('bloomSoftKnee', '0.75'),
                         ('bloomRadiusPixels', '3'), ('outputTransform.gamma', '2.4'),
                         ('outputTransform.autoExposure.brightAdaptationRate', '4'),
                         ('ambientOcclusionRadius', '0.5'), ('ambientOcclusionIntensity', '2')]:
        run(args.cli, *common, '--set', field + '=' + value)
        assert field + ' ' + value in profile.read_text(encoding='utf-8')
    for field, value in [('fogProfile', 'Unknown'), ('bloomStrength', 'inf'), ('bloomStrength', '-1'), ('bloomThreshold', '-1'),
                         ('bloomSoftKnee', '2'), ('bloomRadiusPixels', '-1'), ('outputTransform.gamma', '0'),
                         ('outputTransform.autoExposure.darkAdaptationRate', '-1'),
                         ('outputTransform.autoExposure.minExposureStops', '100'),
                         ('ambientOcclusionRadius', '0'), ('ambientOcclusionIntensity', '-1')]:
        before = profile.read_bytes()
        run(args.cli, *common, '--set', field + '=' + value, expected=1)
        assert profile.read_bytes() == before, 'invalid edit changed the saved profile'
    for profile_name in ('Desktop', 'Mobile', 'Web', 'Automatic'):
        run(args.cli, *common, '--set', 'fogProfile=' + profile_name)
        assert 'fogProfile ' + profile_name in profile.read_text(encoding='utf-8')
    for operator in ('None', 'Aces', 'AgxApprox'):
        run(args.cli, *common, '--set', 'outputTransform.tonemap=' + operator)
        assert 'outputTransform.tonemap ' + operator in profile.read_text(encoding='utf-8')
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', gui=True, cli=True,
        invalidEditsPreserveSavedProfile=True, outputProfiles=3, fogProfiles=4), indent=2))
    print(root)


if __name__ == '__main__':
    main()
