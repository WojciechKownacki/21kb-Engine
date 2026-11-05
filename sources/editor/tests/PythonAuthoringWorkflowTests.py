"""Qualify embedded Python editor authoring, history, reload and C++/Lua scene consumption."""
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
    root = Path(tempfile.mkdtemp(prefix='PythonAuthoring', dir=args.test_root)).resolve()
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

    fixtures=Path(__file__).resolve().parent/'fixtures/python_authoring'
    cli('new','--parent',root,'--name',project.name,'--template','Empty')
    entity('Camera','0,0,-8','Camera',look_at='0,0,0');component('Camera','Camera','primary=true','projection=1','orthographicHeight=5')
    entity('NativeHost');entity('LuaHost')
    entity('Ambient');component('Ambient','Ambient Radiance',action='add');component('Ambient','Ambient Radiance','mode=0','color=1,1,1')
    cli('post-process-new','--project',project,'--name','Look','--set','autoExposureMetering=Manual','--set','temporalAntiAliasingEnabled=0','--set','bloomEnabled=0')
    cli('scene-post-process','--project',project,'--profile','/Game/PostProcess/Look.kbppfx')
    author=root/'Autor_żółć.py';verify=root/'Weryfikacja_żółć.py'
    shutil.copy2(fixtures/'Author.py',author);shutil.copy2(fixtures/'Verify.py',verify)
    # Real in-process batch entry runs without relying on a host Python install.
    output=run(args.editor,'--project',next(project.glob('*.21kbproject')),'--python-script',author)
    assert 'embedded Python authoring completed' in output,output
    scene=project/'Assets/Scenes/Main.21kbscene';before=scene.read_bytes()
    bad=root/'Error.py';bad.write_text('raise ValueError("authoring-exception-probe")',encoding='utf-8')
    output=run(args.editor,'--project',next(project.glob('*.21kbproject')),'--python-script',bad,expected=1)
    assert 'authoring-exception-probe' in output and scene.read_bytes()==before,output
    cli('script-new','--project',project,'--name','NativePythonProbe','--language','cpp')
    (project/'Source/NativeScripts/NativePythonProbe/NativePythonProbe.cpp').write_text((fixtures/'Probe.cpp').read_text(encoding='utf-8'),encoding='utf-8')
    cli('script-build','--project',project,'--script','/Game/Scripts/NativePythonProbe.native')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','NativeHost','--script','/Game/Scripts/NativePythonProbe.native')
    cli('script-new','--project',project,'--name','LuaPythonProbe','--language','lua')
    (project/'Assets/Scripts/LuaPythonProbe.lua').write_text((fixtures/'Probe.lua').read_text(encoding='utf-8'),encoding='utf-8')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','LuaHost','--script','/Game/Scripts/LuaPythonProbe.lua')
    initial=root/'EditorSource';shutil.copytree(project,initial,ignore=shutil.ignore_patterns('Saved'))
    scenario=root/'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',initial_project=str(next(initial.glob('*.21kbproject'))),steps=[
        dict(op='configure_runtime_rendering',width=640,height=360),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),
        dict(op='python_script',path=str(verify)),dict(op='save_scene'),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),
        dict(op='python_script',path=str(verify)),dict(op='assert_console',contains='embedded-python-reload PASS'),
        dict(op='play'),dict(op='step',frames=30),dict(op='capture_runtime',checkpoint='authored',require_non_uniform=True),
        dict(op='assert_console',contains='native-python-content PASS'),dict(op='assert_console',contains='lua-python-content PASS'),dict(op='assert_no_errors'),dict(op='stop')])),encoding='utf-8')
    run(args.editor,'--selftest-scenario',scenario,'--selftest-root',root/'Editor','--selftest-task','PythonAuthoring')
    sys.path.insert(0,str(args.engine_root.resolve()/'scripts'))
    from png_readback import read_png
    image=read_png(root/'Editor/PythonAuthoring/automation/screenshots/authored.png')
    visible=sum(min(image[y][x][:3])>65 for y in range(125,235) for x in range(360,470))
    assert visible>2000,('Python-created geometry missing on GPU',visible)
    (root/'result.json').write_text(json.dumps(dict(result='PASS',embeddedVersion='3.14.10',inProcessEditor=True,createSetReadSchema=True,
        invalidPropertyPreservesState=True,staleHandlesAndOwnerThread=True,undoRedo=True,saveReload=True,unicodeScriptPaths=True,explicitException=True,nativeAndLua=True,gpuAuthoredPixels=visible),indent=2),encoding='utf-8')
    print(root)

if __name__=='__main__':main()
