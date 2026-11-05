"""Qualify external editor SDK install, panel/action dispatch, rollback and unload."""
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
    root = Path(tempfile.mkdtemp(prefix='EditorExtension', dir=args.test_root)).resolve()
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

    fixtures=Path(__file__).resolve().parent/'fixtures/editor_extension'
    cli('new','--parent',root,'--name',project.name,'--template','Empty')
    entity('Camera','0,0,-8','Camera',look_at='0,0,0');component('Camera','Camera','primary=true','projection=1','orthographicHeight=5')
    entity('NativeHost');entity('LuaHost');entity('Ambient')
    component('Ambient','Ambient Radiance',action='add');component('Ambient','Ambient Radiance','mode=0','color=1,1,1')
    cli('post-process-new','--project',project,'--name','Look','--set','autoExposureMetering=Manual','--set','temporalAntiAliasingEnabled=0','--set','bloomEnabled=0')
    cli('scene-post-process','--project',project,'--profile','/Game/PostProcess/Look.kbppfx')
    external=root/'ExternalModule';shutil.copytree(args.engine_root/'examples/EditorExtension',external)
    shutil.copy2(fixtures/'RejectedExtension.cpp',external/'RejectedExtension.cpp')
    with (external/'CMakeLists.txt').open('a',encoding='utf-8') as cmake_file:
        cmake_file.write('\nforeach(mode IN ITEMS 1 2 3)\n'
            '  add_library(RejectedExtension${mode} SHARED RejectedExtension.cpp)\n'
            '  target_compile_definitions(RejectedExtension${mode} PRIVATE KB_REJECT_MODE=${mode})\n'
            '  target_include_directories(RejectedExtension${mode} PRIVATE "${KB_EDITOR_SDK_INCLUDE}")\n'
            '  set_target_properties(RejectedExtension${mode} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KB_EXTENSION_OUTPUT}")\n'
            'endforeach()\n')
    import re
    cache=(args.player.resolve().parents[1]/'CMakeCache.txt').read_text(encoding='utf-8')
    cmake=Path(re.search(r'^CMAKE_COMMAND:INTERNAL=(.+)$',cache,re.M)[1])
    run(cmake,'-S',external,'-B',root/'ExtensionBuild','-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        '-DKB_EDITOR_SDK_ROOT='+str(args.engine_root.resolve()),'-DKB_EXTENSION_OUTPUT='+str(project/'Binaries/EditorExtensions'))
    run(cmake,'--build',root/'ExtensionBuild','--target','SampleExtension','RejectedExtension1','RejectedExtension2','RejectedExtension3','--parallel','2')
    folder=project/'Editor/Extensions';folder.mkdir(parents=True)
    shutil.copy2(external/'Sample.kbeditorext',folder/'Sample.kbeditorext')
    for mode,name in [(1,'bad'),(2,'failure'),(3,'duplicate')]:
        (project/('Editor/'+name+'.kbeditorext')).write_text(json.dumps(dict(schema=1,id='sample.'+name,module='Binaries/EditorExtensions/RejectedExtension'+str(mode)+'.dll')),encoding='utf-8')
    for name,language,host in [('NativeExtensionProbe','cpp','NativeHost'),('LuaExtensionProbe','lua','LuaHost')]:
        cli('script-new','--project',project,'--name',name,'--language',language)
        origin=Path(__file__).resolve().parent/'fixtures/python_authoring'/('Probe.cpp' if language=='cpp' else 'Probe.lua')
        text=origin.read_text(encoding='utf-8').replace('GeneratedCube','ExtensionCube').replace('1.25','1.5').replace('NativePythonProbe','NativeExtensionProbe').replace('python-content','extension-content')
        destination=project/('Source/NativeScripts/'+name+'/'+name+'.cpp' if language=='cpp' else 'Assets/Scripts/'+name+'.lua')
        destination.write_text(text,encoding='utf-8')
        if language=='cpp':cli('script-build','--project',project,'--script','/Game/Scripts/'+name+'.native')
        cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene','--node',host,'--script','/Game/Scripts/'+name+('.native' if language=='cpp' else '.lua'))
    initial=root/'EditorSource';shutil.copytree(project,initial,ignore=shutil.ignore_patterns('Saved'))
    steps=[dict(op='configure_runtime_rendering',width=640,height=360),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene')]
    for mode,name,message in [(1,'bad','incompatible'),(2,'failure','entry rejected'),(3,'duplicate','already exists')]:
        steps += [dict(op='extension_install',path=str(initial/('Editor/'+name+'.kbeditorext')),expect_error=message),
            dict(op='assert_extensions',modules=0,actions=0,panels=0),dict(op='assert_extension_module_unmapped',module='RejectedExtension'+str(mode)+'.dll')]
    steps += [dict(op='extension_load_project'),dict(op='assert_extensions',modules=1,actions=1,panels=1),
        dict(op='extension_install',path=str(initial/'Editor/Extensions/Sample.kbeditorext'),expect_error='already installed'),
        dict(op='capture',panel='extension:sample.blockout.panel',checkpoint='before'),
        dict(op='extension_click',id='sample.blockout.panel'),dict(op='assert_extension_panel',id='sample.blockout.panel',contains='actions=1'),
        dict(op='capture',panel='extension:sample.blockout.panel',checkpoint='after'),dict(op='assert_extension_workspace'),
        dict(op='save_scene'),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='python_script',path=str(fixtures/'Verify.py')),
        dict(op='extension_click',id='sample.blockout.panel'),dict(op='assert_extension_panel',id='sample.blockout.panel',contains='actions=2'),
        dict(op='extension_unload',id='sample.blockout'),dict(op='assert_extensions',modules=0,actions=0,panels=0),dict(op='assert_extension_module_unmapped',module='SampleExtension.dll'),
        dict(op='extension_invoke',id='sample.blockout.create',expect_error='not found')]
    for cycle in range(2):
        steps += [dict(op='extension_install',path=str(initial/'Editor/Extensions/Sample.kbeditorext')),dict(op='extension_click',id='sample.blockout.panel'),
            dict(op='assert_extensions',modules=1,actions=1,panels=1),dict(op='extension_unload',id='sample.blockout'),
            dict(op='assert_extensions',modules=0,actions=0,panels=0),dict(op='assert_extension_module_unmapped',module='SampleExtension.dll')]
    steps += [dict(op='assert_console',contains='sample-extension resources released'),dict(op='assert_console',contains='rejected-extension resources released'),
        dict(op='save_scene'),dict(op='open_scene',path='Assets/Scenes/Main.21kbscene'),dict(op='python_script',path=str(fixtures/'Verify.py')),
        dict(op='play'),dict(op='step',frames=30),dict(op='capture_runtime',checkpoint='authored',require_non_uniform=True),
        dict(op='assert_console',contains='native-extension-content PASS'),dict(op='assert_console',contains='lua-extension-content PASS'),dict(op='assert_no_errors'),dict(op='stop')]
    scenario=root/'scenario.json';scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',initial_project=str(next(initial.glob('*.21kbproject'))),steps=steps)),encoding='utf-8')
    run(args.editor,'--selftest-scenario',scenario,'--selftest-root',root/'Editor','--selftest-task','EditorExtension')
    import struct
    def bmp(path):
        data=path.read_bytes();offset=struct.unpack_from('<I',data,10)[0];width,height=struct.unpack_from('<ii',data,18);bits=struct.unpack_from('<H',data,28)[0];assert bits in (24,32)
        stride=((width*bits+31)//32)*4
        return [[tuple(data[offset+(abs(height)-1-y if height>0 else y)*stride+x*(bits//8)+c] for c in (2,1,0)) for x in range(width)] for y in range(abs(height))]
    before,after=[bmp(root/('Editor/EditorExtension/automation/screenshots/'+name+'.bmp')) for name in ('before','after')]
    changed=sum(max(abs(a-b) for a,b in zip(before[y][x],after[y][x]))>8 for y in range(48,124) for x in range(16,800))
    button=sum(min(after[y][x])>90 and max(after[y][x])-min(after[y][x])<5 for y in range(140,168) for x in range(20,250))
    assert changed>100 and button>1000,('module panel did not paint/update',changed,button)
    sys.path.insert(0,str(args.engine_root.resolve()/'scripts'))
    from png_readback import read_png
    image=read_png(root/'Editor/EditorExtension/automation/screenshots/authored.png')
    visible=sum(min(image[y][x][:3])>65 for y in range(125,235) for x in range(380,480));assert visible>2000,visible
    (root/'result.json').write_text(json.dumps(dict(result='PASS',externalCppModule=True,publicHeaderOnly=True,projectInstall=True,
        actualPanelPixels=button,panelChangedPixels=changed,buttonInvokesAction=True,authoringSaveReload=True,nativeAndLua=True,gpuAuthoredPixels=visible,
        badVersionRejected=True,failedInstallRollback=True,duplicateRegistrationRejected=True,unloadRemovesCallbacksAndPanels=True,
        actualDllUnmapped=True,moduleResourcesReleased=True,reloadCycles=3,workspaceRestoreReset=True),indent=2),encoding='utf-8')
    print(root)

if __name__=='__main__':main()
