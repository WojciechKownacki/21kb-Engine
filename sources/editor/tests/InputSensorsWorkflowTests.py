"""Qualify sensor SI snapshots, replay, real Windows provider status and cooked C++/Lua input."""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'cli', 'cooker', 'player', 'engine-root', 'shaderc', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='InputSensors', dir=args.test_root)).resolve()
    project = root / 'SampleScene'
    history = []
    environment = os.environ.copy()
    environment['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'

    def run(exe, *tail, expected=0):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=exe.resolve().parent, env=environment,
            capture_output=True, encoding='utf-8', errors='replace', timeout=240)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    def cli(*tail, **options):
        return run(args.cli, *tail, **options)

    def entity(name, position='0,0,0', preset=None, **options):
        tail = ['entity', 'add', '--project', project, '--name', name, '--position', position]
        if preset:
            tail += ['--create', preset]
        for key, value in options.items():
            tail += ['--' + key.replace('_', '-'), value]
        cli(*tail)

    def component(name, kind, *values, action='set'):
        cli('component', action, '--project', project, '--entity', name, '--type', kind,
            *[part for value in values for part in ('--set', value)])

    cli('new','--parent',root,'--name',project.name,'--template','Empty')
    entity('Camera','0,0,-6','Camera');entity('NativeHost');entity('LuaHost')
    fixtures=Path(__file__).resolve().parent/'fixtures/input_sensors'
    cli('script-new','--project',project,'--name','NativeSensorProbe','--language','cpp')
    (project/'Source/NativeScripts/NativeSensorProbe/NativeSensorProbe.cpp').write_text((fixtures/'Probe.cpp').read_text(encoding='utf-8'),encoding='utf-8')
    cli('script-build','--project',project,'--script','/Game/Scripts/NativeSensorProbe.native')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','NativeHost','--script','/Game/Scripts/NativeSensorProbe.native')
    authored=cli('run','--project',project,'--frames','3')
    assert 'sensor-recording-author PASS' in authored and (project/'Assets/Input/Motion.21kbinputrecording').is_file(),authored
    descriptor=project/'Assets/Scripts/NativeSensorProbe.native'
    descriptor.write_text('# @asset /Game/Input/Motion.21kbinputrecording\n'+descriptor.read_text(encoding='utf-8'),encoding='utf-8')
    cli('script-build','--project',project,'--script','/Game/Scripts/NativeSensorProbe.native')
    cli('script-new','--project',project,'--name','LuaSensorProbe','--language','lua')
    (project/'Assets/Scripts/LuaSensorProbe.lua').write_text((fixtures/'Probe.lua').read_text(encoding='utf-8'),encoding='utf-8')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','LuaHost','--script','/Game/Scripts/LuaSensorProbe.lua')
    initial=root/'EditorSource';shutil.copytree(project,initial,ignore=shutil.ignore_patterns('Saved'))
    scenario=root/'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',initial_project=str(next(initial.glob('*.21kbproject'))),steps=[
        dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='save_scene'),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='play'),dict(op='step',frames=80),
        dict(op='assert_console',contains='native-input-sensors PASS'),dict(op='assert_console',contains='lua-input-sensors PASS'),dict(op='assert_no_errors'),dict(op='stop')])),encoding='utf-8')
    run(args.editor,'--selftest-scenario',scenario,'--selftest-root',root/'Editor','--selftest-task','InputSensors')
    output=run(args.player,'--project='+str(next(project.glob('*.21kbproject'))),'--headless','--frames=100','--uncapped','--profile-fixed-step','--profile-file='+str(root/'source.csv'))
    assert 'native-input-sensors PASS' in output and 'lua-input-sensors PASS' in output and 'errors=0' in output,output
    package=root/'Export'
    run(Path(sys.executable),args.engine_root/'scripts/package_game.py','--project',next(project.glob('*.21kbproject')),'--target','Windows.x64',
        '--configuration','Development','--output',package,'--engine-root',args.engine_root,'--build-root',args.player.resolve().parents[1],
        '--product-name','SampleScene','--publisher','Engine Samples','--version','0.1.0','--executable-name','SamplePlayer')
    assert (package/'phonon.dll').is_file()
    sbom=json.loads((package/'sbom.cdx.json').read_text(encoding='utf-8'));assert any(c['name']=='Steam Audio' for c in sbom['components'])
    isolated=root.parent/(root.name+'Package');assert isolated.resolve().parent==args.test_root.resolve();shutil.copytree(package,isolated)
    unavailable=root.parent/(root.name+'SourceUnavailable');assert root.parent==args.test_root.resolve() and unavailable.resolve().parent==args.test_root.resolve()
    original=root;root.rename(unavailable);root=unavailable;assert not original.exists()
    output=run(isolated/'SamplePlayer.exe','--headless','--frames=100','--uncapped')
    assert 'native-input-sensors PASS' in output and 'lua-input-sensors PASS' in output and 'errors=0' in output and 'shutdown=clean' in output,output
    provider=[line for line in output.splitlines() if 'native-sensor-provider ' in line]
    assert provider,output
    (root/'result.json').write_text(json.dumps(dict(result='PASS',nativeAndLua=True,recordingAndCookedReplay=True,siUnitsAndTimestamps=True,
        invalidDataPreservesState=True,providerPauseResume=True,sourceUnavailable=True,providerObservations=provider,
        actualProviderReportsObserved=any('measured=1' in line for line in provider),hardwareQualification='Replay does not qualify physical hardware; inspect actual provider reports.'),indent=2),encoding='utf-8')
    print(root)

if __name__=='__main__':main()
