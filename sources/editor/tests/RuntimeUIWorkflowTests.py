"""Qualify MSDF, semantic speech and audio-clock video in editor and isolated export."""
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
    for name in ('editor', 'cli', 'cooker', 'player', 'engine-root', 'shaderc', 'test-root', 'ffmpeg', 'window-probe'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='RuntimeUI', dir=args.test_root)).resolve()
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

    assert 'Native UI Automation client PASS' in run(args.window_probe)
    cli('new','--parent',root,'--name',project.name,'--template','Empty')
    entity('Camera','0,0,-6','Camera');entity('NativeHost');entity('LuaHost')
    entity('Canvas',preset='Canvas');component('Canvas','Canvas Scaler','scaleMode=0')
    for name,preset,x,y,w,h in [('Label','Text',20,20,600,100),('Action','Button',20,120,200,40),('Video','Raw Image',20,180,260,160),('LuaVideo','Raw Image',340,180,260,160)]:
        entity(name,preset=preset,parent='Canvas')
        component(name,'Rect Transform',f'offsetMin.x={x}',f'offsetMin.y={y}',f'offsetMax.x={x+w}',f'offsetMax.y={y+h}')
    entity('Secret',preset='Input Field',parent='Canvas')
    component('Secret','Rect Transform','offsetMin.x=340','offsetMin.y=120','offsetMax.x=600','offsetMax.y=160')
    component('Secret','Input Field','contentType=5');component('Secret','Text','content=Private sample value')
    component('Secret','Accessibility',action='add');component('Secret','Accessibility','label=Password')
    component('Action','Accessibility',action='add');component('Action','Accessibility','label=Start sample','hint=Run the sample')
    component('Label','Text','content=MSDF AVW 21kb','fontSize=64','fontRenderMode=2')
    video=root/'Clip.mp4'
    run(args.ffmpeg,'-hide_banner','-loglevel','error','-y','-f','lavfi','-i','color=c=red:s=96x64:r=20:d=1','-f','lavfi','-i','color=c=blue:s=96x64:r=20:d=1',
        '-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=2','-filter_complex','[0:v][1:v]concat=n=2:v=1:a=0[v]','-map','[v]','-map','2:a','-c:v','libx264','-pix_fmt','yuv420p','-c:a','aac','-shortest',video)
    font=root/'Font.ttf';shutil.copy2(args.engine_root/'sources/editor/content/EditorShell/Fonts/DejaVuSans.ttf',font)
    cli('import','--project',project,'--destination','/Game/UI',video)
    cli('import','--project',project,'--destination','/Game/UI',font)
    component('Label','Text','fontAssetId=/Game/UI/Font.21kb')
    fixtures=Path(__file__).resolve().parent/'fixtures/ui_runtime'
    cli('script-new','--project',project,'--name','NativeUIProbe','--language','cpp')
    (project/'Source/NativeScripts/NativeUIProbe/NativeUIProbe.cpp').write_text((fixtures/'Probe.cpp').read_text(encoding='utf-8'),encoding='utf-8')
    descriptor=project/'Assets/Scripts/NativeUIProbe.native'
    descriptor.write_text('# @asset /Game/UI/Clip.21kb\n# @asset /Game/UI/Font.21kb\n'+descriptor.read_text(encoding='utf-8'),encoding='utf-8')
    cli('script-build','--project',project,'--script','/Game/Scripts/NativeUIProbe.native')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','NativeHost','--script','/Game/Scripts/NativeUIProbe.native')
    cli('script-new','--project',project,'--name','LuaUIProbe','--language','lua')
    (project/'Assets/Scripts/LuaUIProbe.lua').write_text((fixtures/'Probe.lua').read_text(encoding='utf-8'),encoding='utf-8')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','LuaHost','--script','/Game/Scripts/LuaUIProbe.lua')
    initial=root/'EditorSource';shutil.copytree(project,initial,ignore=shutil.ignore_patterns('Saved'))
    scenario=root/'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',initial_project=str(next(initial.glob('*.21kbproject'))),steps=[
        dict(op='configure_runtime_rendering',width=640,height=360),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='save_scene'),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='play'),
        dict(op='step',frames=20),dict(op='capture_runtime',checkpoint='early',require_non_uniform=True),dict(op='step',frames=12),dict(op='capture_runtime',checkpoint='late',require_non_uniform=True),dict(op='step',frames=48),
        dict(op='assert_console',contains='native-runtime-ui PASS'),dict(op='assert_console',contains='lua-runtime-ui PASS'),dict(op='assert_no_errors'),dict(op='stop')])),encoding='utf-8')
    run(args.editor,'--selftest-scenario',scenario,'--selftest-root',root/'Editor','--selftest-task','RuntimeUI')
    sys.path.insert(0,str(args.engine_root/'scripts'));from png_readback import read_png
    early,late=[read_png(root/f'Editor/RuntimeUI/automation/screenshots/{name}.png') for name in ('early','late')]
    red=sum(early[y][x][0]>early[y][x][2]+100 for y in range(200,310) for x in range(40,250))
    blue=sum(late[y][x][2]>late[y][x][0]+100 for y in range(200,310) for x in range(40,250))
    text=sum(min(early[y][x][:3])>200 for y in range(20,110) for x in range(20,600))
    assert red>10000 and blue>10000 and text>300,dict(redPixels=red,bluePixels=blue,msdfPixels=text)
    output=run(args.player,'--project='+str(next(project.glob('*.21kbproject'))),'--headless','--frames=100','--uncapped','--profile-fixed-step','--profile-file='+str(root/'source.csv'))
    assert 'native-runtime-ui PASS' in output and 'lua-runtime-ui PASS' in output and 'errors=0' in output,output
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
    assert 'native-runtime-ui PASS' in output and 'lua-runtime-ui PASS' in output and 'errors=0' in output and 'shutdown=clean' in output,output
    (root/'result.json').write_text(json.dumps(dict(result='PASS',nativeAndLua=True,msdfGpuPixels=text,videoRedPixels=red,videoBluePixels=blue,sourceUnavailable=True,
        speechSynthesizedPcm=True,semanticFocusAndInvoke=True,videoPcmCursorSync=True,seekPauseResume=True,loopEofAndOwnerCleanup=True,windowsUiAutomationClient=True,systemScreenReaders='Narrator, TalkBack and VoiceOver not host-qualified'),indent=2),encoding='utf-8')
    print(root)

if __name__=='__main__':main()
