"""Qualify CSG volumes, ordinary mesh rendering/collision and isolated cooked C++/Lua attachment."""
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
    root = Path(tempfile.mkdtemp(prefix='Csg', dir=args.test_root)).resolve()
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
    def brush(operation,size,position=[0,0,0],rotation=[0,0,0]):
        return dict(operation=operation,kind='box',size=size,position=position,rotation=rotation,scale=[1,1,1])
    for operation,volume in [('union',5),('subtract',3),('intersect',1)]:
        name=operation.title();recipe=dict(schema=1,brushes=[brush('union',[2,2,1]),brush(operation,[1,1,2],[0,0,.25])])
        cli('csg','new','--project',project,'--name',name,'--data',json.dumps(recipe))
        path='/Game/Geometry/'+name+'.kbcsg'
        assert json.loads(cli('csg','show','--project',project,'--source',path))==recipe
        out=cli('csg','build','--project',project,'--source',path,'--output','/Game/Compiled/'+name)
        assert 'volume: '+str(volume) in out,out
        source=project/('Assets/Geometry/'+name+'.kbcsg');before=source.read_bytes()
        bad=json.loads(json.dumps(recipe));bad['brushes'][1]['size'][0]=0
        cli('csg','set','--project',project,'--source',path,'--data',json.dumps(bad),expected=1)
        assert source.read_bytes()==before
    entity('Camera','0,0,-10','Camera',look_at='0,0,0');component('Camera','Camera','primary=true','projection=1','orthographicHeight=5')
    entity('NativeSubtract',preset='Cube');entity('NativeIntersect','2.7,0,0','Cube');entity('LuaUnion','-2.7,0,0','Cube')
    entity('Ambient');component('Ambient','Ambient Radiance',action='add');component('Ambient','Ambient Radiance','mode=0','color=1,1,1')
    materials=project/'Assets/Materials';materials.mkdir(exist_ok=True)
    for name,color in [('Union','1 0.02 0.02'),('Subtract','0.02 1 0.02'),('Intersect','0.02 0.02 1')]:
        (materials/(name+'.kbmat')).write_text('version 1\nmaterialType builtin.pbr\nmaterialTypeVersion 1\nbaseColor '+color+' 1\nemissiveColor '+color+'\nemissiveStrength 1\n',encoding='utf-8')
        host='LuaUnion' if name=='Union' else 'Native'+name
        component(host,'MeshRenderer','material=/Game/Materials/'+name+'.kbmat')
    cli('post-process-new','--project',project,'--name','Look','--set','autoExposureMetering=Manual','--set','temporalAntiAliasingEnabled=0','--set','bloomEnabled=0')
    cli('scene-post-process','--project',project,'--profile','/Game/PostProcess/Look.kbppfx')
    fixtures=Path(__file__).resolve().parent/'fixtures/csg'
    cli('script-new','--project',project,'--name','NativeCsgProbe','--language','cpp')
    (project/'Source/NativeScripts/NativeCsgProbe/NativeCsgProbe.cpp').write_text((fixtures/'Probe.cpp').read_text(encoding='utf-8'),encoding='utf-8')
    for host in ('NativeSubtract','NativeIntersect'):
        cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node',host,'--script','/Game/Scripts/NativeCsgProbe.native')
    descriptor=project/'Assets/Scripts/NativeCsgProbe.native'
    references=''.join('# @asset /Game/Compiled/'+name+'/Geometry.'+ext+'\n' for name in ('Subtract','Intersect') for ext in ('21kb','kbcollision'))
    descriptor.write_text(references+descriptor.read_text(encoding='utf-8'),encoding='utf-8')
    cli('script-build','--project',project,'--script','/Game/Scripts/NativeCsgProbe.native')
    cli('script-new','--project',project,'--name','LuaCsgProbe','--language','lua')
    (project/'Assets/Scripts/LuaCsgProbe.lua').write_text((fixtures/'Probe.lua').read_text(encoding='utf-8'),encoding='utf-8')
    cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node','LuaUnion','--script','/Game/Scripts/LuaCsgProbe.lua')
    initial=root/'EditorSource';shutil.copytree(project,initial,ignore=shutil.ignore_patterns('Saved'))
    scenario=root/'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',initial_project=str(next(initial.glob('*.21kbproject'))),steps=[
        dict(op='configure_runtime_rendering',width=640,height=360),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='save_scene'),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='play'),dict(op='step',frames=80),dict(op='capture_runtime',checkpoint='boolean',require_non_uniform=True),
        dict(op='assert_console',contains='native-csg PASS'),dict(op='assert_console',contains='lua-csg PASS'),dict(op='assert_no_errors'),dict(op='stop')])),encoding='utf-8')
    run(args.editor,'--selftest-scenario',scenario,'--selftest-root',root/'Editor','--selftest-task','Csg')
    sys.path.insert(0,str(args.engine_root.resolve()/'scripts'))
    from png_readback import read_png
    image=read_png(root/'Editor/Csg/automation/screenshots/boolean.png')
    # Orthographic height 5 => 72 pixels/metre. Sample interior away from edges.
    def color(x,y,channel):
        pixel=image[y][x];return pixel[channel]>100 and pixel[channel]>max(pixel[(channel+1)%3],pixel[(channel+2)%3])*1.6
    centers=[126,320,514]
    assert color(centers[0],180,0),'union center missing on GPU'
    assert color(centers[1]+55,180,1),'subtraction outer face missing on GPU'
    assert not color(centers[1],180,1),'subtraction did not leave visible hole'
    assert color(centers[2],180,2),'intersection center missing on GPU'
    assert not color(centers[2]+55,180,2),'intersection did not trim outer face'
    gpu_counts=[sum(color(x,y,c) for y in range(90,270) for x in range(max(0,center-80),min(640,center+80))) for c,center in enumerate(centers)]
    assert all(n>1000 for n in gpu_counts),gpu_counts
    output=run(args.player,'--project='+str(next(project.glob('*.21kbproject'))),'--headless','--frames=100','--uncapped','--profile-fixed-step','--profile-file='+str(root/'source.csv'))
    assert 'native-csg PASS' in output and 'lua-csg PASS' in output and 'errors=0' in output,output
    package=root/'Export'
    run(Path(sys.executable),args.engine_root/'scripts/package_game.py','--project',next(project.glob('*.21kbproject')),'--target','Windows.x64',
        '--configuration','Development','--output',package,'--engine-root',args.engine_root,'--build-root',args.player.resolve().parents[1],
        '--product-name','SampleScene','--publisher','Engine Samples','--version','0.1.0','--executable-name','SamplePlayer')
    assert (package/'phonon.dll').is_file()
    sbom=json.loads((package/'sbom.cdx.json').read_text(encoding='utf-8'));assert any(c['name']=='Manifold' for c in sbom['components'])
    isolated=root.parent/(root.name+'Package');assert isolated.resolve().parent==args.test_root.resolve();shutil.copytree(package,isolated)
    unavailable=root.parent/(root.name+'SourceUnavailable');assert root.parent==args.test_root.resolve() and unavailable.resolve().parent==args.test_root.resolve()
    original=root;root.rename(unavailable);root=unavailable;assert not original.exists()
    output=run(isolated/'SamplePlayer.exe','--headless','--frames=100','--uncapped')
    assert 'native-csg PASS' in output and 'lua-csg PASS' in output and 'errors=0' in output and 'shutdown=clean' in output,output
    (root/'result.json').write_text(json.dumps(dict(result='PASS',nativeAndLua=True,analyticVolumes=[5,3,1],rotatedAndMeshBoolean=True,
        invalidSourcePreservesFile=True,invalidCompileAndAttachPreserveOutput=True,joltHoleAndBounds=True,gpuColoredPixels=gpu_counts,sourceUnavailable=True),indent=2),encoding='utf-8')
    print(root)

if __name__=='__main__':main()
