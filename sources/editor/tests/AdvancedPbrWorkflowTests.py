"""Observe builtin PBR lobes through native/Lua gameplay, editor GPU images and cook."""
import argparse
import json
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
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='AdvancedPbr', dir=args.test_root)).resolve()
    project = root / 'SampleScene'
    history = []
    environment = os.environ.copy()
    environment['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'

    def run(exe, *tail, expected=0):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=Path(command[0]).parent, env=environment,
            capture_output=True, encoding='utf-8', errors='replace', timeout=240)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    def cli(*tail, **options):
        return run(args.cli, *tail, **options)

    cli('new', '--parent', root, '--name', project.name, '--template', 'Empty')
    def entity(name, preset, position):
        cli('entity', 'add', '--project', project, '--name', name, '--create', preset, '--position', position)
    entity('MainCamera', 'Camera', '0,1,-9')
    cli('component', 'set', '--project', project, '--entity', 'MainCamera', '--type', 'Camera', '--set', 'verticalFovDegrees=45')
    entity('Sun', 'Directional Light', '0,4,-4')
    entity('SpecularLamp', 'Point Light', '3.2,2.5,-3')
    cli('component', 'set', '--project', project, '--entity', 'SpecularLamp', '--type', 'Light',
        '--set', 'intensity=30', '--set', 'range=15', '--set', 'castsShadow=false')
    cli('component', 'set', '--project', project, '--entity', 'Sun', '--type', 'Light', '--set', 'castsShadow=false')
    for name, x in [('Sheen', -3.2), ('Transmission', 0), ('Anisotropy', 3.2)]:
        # Axis-aligned faces keep source/cook normal quantization out of this BRDF check.
        entity(name, 'Cube', f'{x},1,0')
    entity('Background', 'Cube', '0,1,3')
    cli('component', 'set', '--project', project, '--entity', 'Background', '--type', 'Transform', '--set', 'localScale=7,4,0.2')
    cli('post-process-new', '--project', project, '--name', 'Look', '--set', 'autoExposureMetering=Manual',
        '--set', 'temporalAntiAliasingEnabled=0', '--set', 'bloomEnabled=0')
    cli('scene-post-process', '--project', project, '--profile', '/Game/PostProcess/Look.kbppfx')
    folder = project / 'Assets/Materials'
    folder.mkdir(parents=True, exist_ok=True)
    header = 'version 1\nmaterialType builtin.pbr\nmaterialTypeVersion 1\n'
    # Keep zero-weight controls on the same forward surface path as the active lobes.
    common = 'baseColor 0.5 0.5 0.5 1\nroughnessFactor 0.4\nsheenRoughnessFactor 0.4\n'
    variants = {
        'Sheen': ['sheenColor 0 0 0\n', 'sheenColor 1 0.04 0.02\nsheenRoughnessFactor 0.8\n',
                  'sheenColor 1 0.04 0.02\nsheenRoughnessFactor 0.15\n'],
        'Transmission': ['transmissionFactor 0\n', 'transmissionFactor 1\n',
                         'transmissionFactor 1\nthicknessFactor 1\nattenuationColor 0.05 0.8 0.2\nattenuationDistance 0.5\n'],
        'Anisotropy': ['metallicFactor 1\nanisotropyStrength 0\n', 'metallicFactor 1\nanisotropyStrength 0.9\n',
                       'metallicFactor 1\nanisotropyStrength 0.9\nanisotropyRotation 0.9\n']}
    for name, values in variants.items():
        for index, value in enumerate(values):
            (folder / f'{name}{index}.kbmat').write_text(header + common + value, encoding='utf-8')
    (folder / 'Background.kbmat').write_text(header + 'baseColor 0.05 0.2 0.8 1\nemissiveColor 0.05 0.2 0.8\nemissiveStrength 1\n', encoding='utf-8')
    for material, setting in [('Sheen1', 'sheenRoughnessFactor=0.8'),
                              ('Transmission1', 'transmissionFactor=1'), ('Anisotropy2', 'anisotropyRotation=0.9')]:
        cli('material-set', '--project', project, '--material', f'/Game/Materials/{material}.kbmat', '--set', setting)
    saved = (folder / 'Anisotropy2.kbmat').read_bytes()
    cli('material-set', '--project', project, '--material', '/Game/Materials/Anisotropy2.kbmat',
        '--set', 'anisotropyStrength=nan', expected=1)
    assert (folder / 'Anisotropy2.kbmat').read_bytes() == saved, 'Rejected nonfinite parameter damaged the material'
    for name in [*variants, 'Background']:
        material = name + ('0' if name in variants else '')
        cli('component', 'set', '--project', project, '--entity', name, '--type', 'MeshRenderer',
            '--set', f'material=/Game/Materials/{material}.kbmat')
    cli('script-new', '--project', project, '--name', 'MaterialProbe', '--language', 'cpp')
    native = project / 'Source/NativeScripts/MaterialProbe/MaterialProbe.cpp'
    native.write_text('''#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include <array>
#include <string>
extern "C" void ecs_os_set_api_defaults(void);
static int frames = 0;
static void Assign(kb::script::ScriptExecutionContext* context, int variant) {
    const std::array args{kb::script::ScriptFunctionArgument{"material", kb::script::ScriptValue{
        std::string{"/Game/Materials/Sheen"} + std::to_string(variant) + ".kbmat"}}};
    static_cast<void>(context->CallFunction("MeshRenderer.SetMaterial",args));
}
static void Ready(kb::script::ScriptExecutionContext* context) { frames = 0; Assign(context,0); }
static void Tick(kb::script::ScriptExecutionContext* context) {
    ++frames;
    if(frames == 121) Assign(context,1);
    if(frames == 151) Assign(context,2);
    if(frames == 181) Assign(context,0);
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api) {
    ecs_os_set_api_defaults();
    return api && api->version == kb::script::kNativeScriptPluginApiVersion && api->registerLifecycle &&
        api->registerLifecycle(api->user,"project.MaterialProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready) &&
        api->registerLifecycle(api->user,"project.MaterialProbe",kb::script::ScriptLifecycleEvent::Tick,&Tick);
}
''', encoding='utf-8')
    cli('script-build', '--project', project, '--script', '/Game/Scripts/MaterialProbe.native')
    cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene', '--node', 'Sheen', '--script', '/Game/Scripts/MaterialProbe.native')
    cli('script-new', '--project', project, '--name', 'LuaMaterialProbe', '--language', 'lua')
    annotations = ''.join(f'-- @asset /Game/Materials/{name}{index}.kbmat\n' for name in variants for index in range(3))
    (project / 'Assets/Scripts/LuaMaterialProbe.lua').write_text(annotations + '''local frames = 0
local function apply(variant)
 for _, name in ipairs({"Transmission", "Anisotropy"}) do
  assert(MeshRenderer.SetMaterial("/Game/Materials/" .. name .. variant .. ".kbmat", World.FindByName(name)))
 end
end
function Ready(self)
 frames = 0
 local ok, diagnostic = MeshRenderer.SetMaterial("/Game/Materials/Transmission0.kbmat", "invalid-target")
 assert(ok == nil and diagnostic ~= nil)
 apply(0)
end
function Tick(self, dt)
 frames = frames + 1
 if frames == 121 then apply(1) end
 if frames == 151 then apply(2) end
 if frames == 181 then apply(0) Log("advanced-pbr reset PASS") end
end
''', encoding='utf-8')
    # The native callback's dynamic material references are also explicit cook roots.
    cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene', '--node', 'Transmission', '--script', '/Game/Scripts/LuaMaterialProbe.lua')
    steps = [dict(op='configure_runtime_rendering', width=640, height=360), dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'),
             dict(op='save_scene'), dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='play')]
    for checkpoint in ['zero', 'on', 'varied', 'reset']:
        steps += [dict(op='step', frames=120 if checkpoint == 'zero' else 30), dict(op='capture_runtime', checkpoint=checkpoint, require_non_uniform=True)]
    steps += [dict(op='assert_console', contains='advanced-pbr reset PASS'), dict(op='assert_no_errors'), dict(op='stop')]
    # Authoring copy excludes CMake caches, which refer to the original absolute source tree.
    initial = root / 'EditorSource'
    shutil.copytree(project, initial, ignore=shutil.ignore_patterns('Saved'))
    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', initial_project=str(next(initial.glob('*.21kbproject'))), steps=steps), indent=2), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root / 'Editor', '--selftest-task', 'Materials')
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    captures = root / 'Editor/Materials/automation/screenshots'
    images = {name: read_png(captures / (name + '.png')) for name in ['zero', 'on', 'varied', 'reset']}
    assert images['zero'] == images['reset'], 'Returning all parameters to zero changed the baseline'
    regions = {}
    for index, name in enumerate(variants):
        def changed(first, second):
            return sum(max(abs(a-b) for a,b in zip(images[first][y][x],images[second][y][x])) > 3
                for y in range(60,300) for x in range(index*640//3,(index+1)*640//3))
        regions[name] = dict(zeroToOn=changed('zero','on'), parameterChange=changed('on','varied'))
        assert min(regions[name].values()) > 12, (name, regions[name])
    run(args.player, '--project=' + str(next(project.glob('*.21kbproject'))), '--headless', '--frames=150', '--uncapped',
        '--width=640', '--height=360', '--profile-fixed-step', '--profile-file=' + str(root / 'source.csv'),
        '--screenshot-frame=150', '--screenshot-file=' + str(root / 'source.png'))
    package = root / 'Package'
    package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', root / 'CookCache',
        '--runtime-modules-output', package / 'RuntimeModules')
    for dll in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(dll, package / dll.name)
    project.rename(root / 'SourceUnavailable')
    initial.rename(root / 'EditorSourceUnavailable')
    output = run(args.player, '--project=' + str(package / 'Game.kbpack'), '--headless', '--frames=150', '--uncapped',
        '--width=640', '--height=360', '--profile-fixed-step', '--profile-file=' + str(root / 'package.csv'),
        '--screenshot-frame=150', '--screenshot-file=' + str(root / 'packaged.png'))
    assert 'errors=0' in output and 'shutdown=clean' in output
    assert read_png(root / 'packaged.png') == read_png(root / 'source.png'), 'Cooked advanced material frame differs from source runtime'
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', regions=regions, nativeAndLua=True,
        editorGpu=True, restoredZeroExactly=True, cookedImageEqual=True, sourceUnavailable=True), indent=2), encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
