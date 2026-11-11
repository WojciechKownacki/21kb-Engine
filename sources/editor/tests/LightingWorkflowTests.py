"""Observe local reflection sampling, a saved static bake and focus depth on a real GPU."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
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
    root = Path(tempfile.mkdtemp(prefix='Lighting', dir=args.test_root)).resolve()
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

    def entity(name, preset=None, position='0,0,0', scale='1,1,1'):
        cli('entity', 'add', '--project', project, '--name', name,
            *(['--create', preset] if preset else []), '--position', position, '--scale', scale)

    def component(name, kind, *settings, action='set'):
        cli('component', action, '--project', project, '--entity', name, '--type', kind,
            *[item for value in settings for item in ('--set', value)])

    cli('new', '--parent', root, '--name', project.name, '--template', 'Empty')
    entity('GlobalAmbient')
    component('GlobalAmbient', 'Ambient Radiance', action='add')
    component('GlobalAmbient', 'Ambient Radiance', 'mode=0', 'color=0.04,0.04,0.04')
    entity('MainCamera', 'Camera', '0,1,-10')
    component('MainCamera', 'Camera', 'verticalFovDegrees=45')
    entity('NearPattern', 'Cube', '-3.2,1,0', '1.8,2.4,0.2')
    entity('FarPattern', 'Cube', '3.2,1,4', '3,4,0.2')
    entity('Mirror', 'Cube', '0,2,1', '2,1.5,1')
    entity('BakedBoard', 'Quad', '0,-1,2', '3,1.8,1')
    entity('BakeBlocker', 'Cube', '-0.3,-1,1', '0.45,0.6,0.4')
    entity('BakeLight', 'Point Light', '-1,0,-1')
    component('BakeLight', 'Light', 'mobility=1', 'intensity=10', 'range=15')
    folder = project / 'Assets/Lighting'
    folder.mkdir(parents=True)
    header = 'version 1\nmaterialType builtin.pbr\nmaterialTypeVersion 1\n'
    (folder / 'Mirror.kbmat').write_text(header + 'baseColor 0.8 0.8 0.8 1\nmetallicFactor 1\nroughnessFactor 0.1\nsheenRoughnessFactor 0.2\n', encoding='utf-8')
    (folder / 'Pattern.kbmat').write_text(header + 'bakedLightmap true\nemissiveTexture /Game/Lighting/Pattern.tga\n', encoding='utf-8')
    # Ordinary author texture, deliberately detailed enough to measure the focus plane.
    tga = bytearray(18)
    tga[2] = 2
    struct.pack_into('<HHBB', tga, 12, 64, 64, 32, 0x28)
    for y in range(64):
        for x in range(64):
            value = 255 if (x // 2 + y // 2) % 2 else 12
            tga.extend((value, value, value, 255))
    (folder / 'Pattern.tga').write_bytes(tga)

    def cube(path, blue):
        # DDS uncompressed RGBA, six complete faces, one mip. Face gradients expose parallax.
        mips = 1 if blue else 5
        fields = [124, 0x100f | (0x20000 if mips > 1 else 0), 16, 16, 64, 0, mips] + [0] * 11
        fields += [32, 0x41, 0, 32, 0xff, 0xff00, 0xff0000, 0xff000000, 0x1008 | (0x400000 if mips > 1 else 0), 0xfe00, 0, 0, 0]
        data = bytearray(b'DDS ' + struct.pack('<31I', *fields))
        for face in range(6):
            for lod in range(mips):
                edge = 16 >> lod
                for y in range(edge):
                    for x in range(edge):
                        main = 35 + x * 192 // edge
                        data.extend((20 if blue else main, 15 + y * 160 // edge, main if blue else 20, 255))
        path.write_bytes(data)

    for index in range(2):
        cube(folder / f'Room{index}.dds', index == 1)
        name = f'Probe{index}'
        entity(name, position=f'{-1.5 if index == 0 else 1.5},1,1')
        component(name, 'Ambient Radiance', action='add')
        component(name, 'RegionShape', action='add')
        component(name, 'Ambient Radiance', 'mode=4', f'environmentAssetId=/Game/Lighting/Room{index}.dds', 'intensity=0')
        component(name, 'RegionShape', 'kind=3', 'size=10,8,10')
    for name in ('NearPattern', 'FarPattern'):
        component(name, 'MeshRenderer', 'material=/Game/Lighting/Pattern.kbmat')
    component('Mirror', 'MeshRenderer', 'material=/Game/Lighting/Mirror.kbmat')
    baked = cli('scene-lightmap', '--project', project, '--entity', 'BakedBoard', '--out', '/Game/Lighting/Board.tga', '--resolution', '64')
    match = re.search(r'baked (\d+) texels, (\d+) occluded', baked)
    assert match and int(match[1]) > 100 and int(match[2]) > 20, baked
    assert (folder / 'Board.tga').is_file() and 'bakedLightmap true' in (folder / 'Board.kbmat').read_text()
    before = (folder / 'Board.tga').read_bytes()
    cli('scene-lightmap', '--project', project, '--entity', 'BakedBoard', '--out', '/Game/Lighting/Board.tga', expected=1)
    assert (folder / 'Board.tga').read_bytes() == before, 'Rejected overwrite changed the bake'
    # A repeated-UV receiver must fail without leaving a partial asset.
    cli('scene-lightmap', '--project', project, '--entity', 'BakeBlocker', '--out', '/Game/Lighting/Invalid.tga', '--resolution', '32', expected=1)
    assert not (folder / 'Invalid.tga').exists() and not (folder / 'Invalid.kbmat').exists()
    cli('entity', 'remove', '--project', project, '--name', 'BakeBlocker')
    cli('entity', 'remove', '--project', project, '--name', 'BakeLight')
    cli('post-process-new', '--project', project, '--name', 'Look', '--set', 'autoExposureMetering=Manual',
        '--set', 'temporalAntiAliasingEnabled=0', '--set', 'bloomEnabled=0')
    cli('scene-post-process', '--project', project, '--profile', '/Game/PostProcess/Look.kbppfx')
    profile = project / 'Assets/PostProcess/Look.kbppfx'
    previous = profile.read_bytes()
    cli('post-process-set', '--project', project, '--profile', '/Game/PostProcess/Look.kbppfx', '--set', 'depthOfFieldFocusRange=0', expected=1)
    assert profile.read_bytes() == previous
    # Exercise the backend-neutral bake from an ordinary generated C++ SDK module.
    entity('NativeBakeHost')
    cli('script-new', '--project', project, '--name', 'NativeBakeProbe', '--language', 'cpp')
    (project / 'Source/NativeScripts/NativeBakeProbe/NativeBakeProbe.cpp').write_text(r'''#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/render/StaticLightmapBaker.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include <array>
extern "C" void ecs_os_set_api_defaults(void);
static void Ready(kb::script::ScriptExecutionContext* context) {
    kb::render::LightmapTriangle triangle;
    triangle[0]={{-1,-1,2},{0,0,-1},0,0};
    triangle[1]={{1,-1,2},{0,0,-1},1,0};
    triangle[2]={{-1,1,2},{0,0,-1},0,1};
    const std::array<kb::render::LightmapTriangle,1> receiver{triangle};
    const std::array lights{kb::render::LightmapLight{.position={0,0,-1},.intensity=3}};
    const auto result=kb::render::BakeStaticLightmap(receiver,{},lights,{.resolution=16});
    auto transform=context->GetScene().Transforms().Get(context->Self());
    transform.localPosition.x=result.Succeeded()&&result.shadowedSamples==0&&result.rgba[0]>10?1.0F:-1.0F;
    context->GetScene().Transforms().Set(context->Self(),transform);
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api) {
    ecs_os_set_api_defaults();
    return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle&&
        api->registerLifecycle(api->user,"project.NativeBakeProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready);
}
''', encoding='utf-8')
    cli('script-build', '--project', project, '--script', '/Game/Scripts/NativeBakeProbe.native')
    cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene', '--node', 'NativeBakeHost', '--script', '/Game/Scripts/NativeBakeProbe.native')
    # Lua consumes the generated baked asset through the same runtime material path.
    cli('script-new', '--project', project, '--name', 'LightingProbe', '--language', 'lua')
    (project / 'Assets/Scripts/LightingProbe.lua').write_text('''-- @asset /Game/Lighting/Board.kbmat
function Ready(self)
 assert(MeshRenderer.SetMaterial("/Game/Lighting/Board.kbmat"))
 Log("saved-lightmap Lua PASS")
end
''', encoding='utf-8')
    cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene', '--node', 'BakedBoard', '--script', '/Game/Scripts/LightingProbe.lua')
    sys.path.insert(0, str(args.engine_root.resolve() / 'scripts'))
    from png_readback import read_png
    images = {}

    def capture(name, **config):
        initial = root / ('Source_' + name)
        shutil.copytree(project, initial, ignore=shutil.ignore_patterns('Saved'))
        steps = [dict(op='configure_runtime_rendering', width=640, height=360, **config),
                 dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='save_scene'),
                 dict(op='open_scene', path='Assets/Scenes/Main.21kbscene'), dict(op='bind_entity', id='native', name='NativeBakeHost'),
                 dict(op='play'), dict(op='step', frames=120),
                 dict(op='assert_property', entity='native', component='Transform', property='localPosition.x', value=1),
                 dict(op='capture_runtime', checkpoint=name, require_non_uniform=True),
                 dict(op='assert_console', contains='saved-lightmap Lua PASS'), dict(op='assert_no_errors'), dict(op='stop')]
        scenario = root / (name + '.json')
        scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', initial_project=str(next(initial.glob('*.21kbproject'))), steps=steps)), encoding='utf-8')
        run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root / 'Editor', '--selftest-task', name)
        images[name] = read_png(root / f'Editor/{name}/automation/screenshots/{name}.png')

    def difference(a, b, box):
        x0, y0, x1, y1 = box
        return sum(max(abs(p-q) for p,q in zip(images[a][y][x], images[b][y][x])) > 3
                   for y in range(y0, y1) for x in range(x0, x1))

    capture('zero')
    for index in range(2):
        component(f'Probe{index}', 'Ambient Radiance', 'intensity=1')
    capture('probes')
    capture('noParallax', probe_parallax=False)
    capture('noBlend', local_probe_blending=False)
    mirror = (270, 100, 370, 180)
    observations = {name: difference('probes', name, mirror) for name in ('zero', 'noParallax', 'noBlend')}
    assert min(observations.values()) > 30, observations
    cli('entity', 'set', '--project', project, '--name', 'Probe0', '--position', '-3,2,-1')
    capture('moved')
    observations['movement'] = difference('probes', 'moved', mirror)
    assert observations['movement'] > 30, observations
    # Disable only the baked runtime mode; the source light and blocker are already absent.
    # The Lua assignment remains the same saved material throughout this control.
    cli('material-set', '--project', project, '--material', '/Game/Lighting/Board.kbmat', '--set', 'bakedLightmap=false')
    capture('unbaked')
    observations['savedBake'] = difference('moved', 'unbaked', (260, 215, 380, 295))
    assert observations['savedBake'] > 100, observations
    cli('material-set', '--project', project, '--material', '/Game/Lighting/Board.kbmat', '--set', 'bakedLightmap=true')
    for name, focus in [('nearFocus', 10), ('farFocus', 14)]:
        cli('post-process-set', '--project', project, '--profile', '/Game/PostProcess/Look.kbppfx',
            '--set', 'depthOfFieldEnabled=1', '--set', f'depthOfFieldFocusDistance={focus}',
            '--set', 'depthOfFieldFocusRange=1', '--set', 'depthOfFieldMaxRadiusPixels=12')
        capture(name)

    def detail(name, box):
        x0, y0, x1, y1 = box
        return sum(abs(images[name][y][x][0] - images[name][y][x+1][0])
                   for y in range(y0, y1) for x in range(x0, x1-1))

    near = (155, 145, 205, 215)
    far = (395, 145, 445, 215)
    focus = {key: [detail(name, box) for name in ('nearFocus', 'farFocus')] for key, box in [('near', near), ('far', far)]}
    assert focus['near'][0] > focus['near'][1] * 1.5 and focus['far'][1] > focus['far'][0] * 1.5, focus
    (root / 'observations.json').write_text(json.dumps(dict(probes=observations, focus=focus), indent=2), encoding='utf-8')
    # Scene-wide bake: a static floor (a cube: no usable lightmap UV, so one is generated), a static lamp
    # and a movable wall that only bounces light.
    entity('WideFloor', 'Cube', '0,-3.5,3', '14,1,14')
    component('WideFloor', 'MeshRenderer', 'mobility=1')
    entity('WideLamp', 'Point Light', '3,1,3')
    component('WideLamp', 'Light', 'mobility=1', 'intensity=10', 'range=15')
    entity('BounceWall', 'Cube', '5,0,3', '0.3,8,8')
    wide_texture = project / 'Assets/Baked/Main/WideFloor.tga'
    wide = cli('scene-bake-lighting', '--project', project, '--resolution', '64', '--bounces', '1')
    assert re.search(r'baked 1 receivers \((\d+) texels, 1 generated', wide) and wide_texture.is_file(), wide
    assert 'bakedLightmapUv1 true' in (project / 'Assets/Baked/Main/WideFloor.kbmat').read_text()
    first_wide = wide_texture.read_bytes()
    # Baking again overwrites the previous bake; the generated mesh is reused.
    assert ', 0 generated' in cli('scene-bake-lighting', '--project', project, '--resolution', '64', '--bounces', '1')
    assert wide_texture.read_bytes() == first_wide, 'A repeated bake must reproduce the same lightmap'
    cli('scene-bake-lighting', '--project', project, '--resolution', '64', '--bounces', '0')
    assert wide_texture.read_bytes() != first_wide, 'The bounce must change the lightmap'
    cli('scene-bake-lighting', '--project', project, '--resolution', '64', '--bounces', '1')
    assert wide_texture.read_bytes() == first_wide
    cli('scene-bake-lighting', '--project', project, '--bounces', '5', expected=1)
    run(args.player, '--project=' + str(next(project.glob('*.21kbproject'))), '--headless', '--frames=120', '--uncapped',
        '--width=640', '--height=360', '--profile-fixed-step', '--profile-file=' + str(root / 'source.csv'), '--screenshot-frame=120', '--screenshot-file=' + str(root / 'source.png'))
    package = root / 'Package'
    package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', root / 'CookCache', '--runtime-modules-output', package / 'RuntimeModules')
    for dll in args.plugin:
        shutil.copy2(dll, package / dll.name)
    project.rename(root / 'SourceUnavailable')
    for initial in root.glob('Source_*'):
        destination = root / ("Unavailable_" + initial.name)
        assert initial.resolve().parent == root and destination.resolve().parent == root
        initial.rename(destination)
    output = run(args.player, '--project=' + str(package / 'Game.kbpack'), '--headless', '--frames=120', '--uncapped',
        '--width=640', '--height=360', '--profile-fixed-step', '--profile-file=' + str(root / 'package.csv'), '--screenshot-frame=120', '--screenshot-file=' + str(root / 'packaged.png'))
    assert 'errors=0' in output and 'shutdown=clean' in output, output
    source, cooked = read_png(root / 'source.png'), read_png(root / 'packaged.png')
    # Texture cooking may compress the lightmap/pattern. Bound the final-frame error rather
    # than demanding lossless identity from an explicitly lossy texture codec.
    average = sum(abs(a-b) for row1,row2 in zip(source,cooked) for p,q in zip(row1,row2) for a,b in zip(p[:3],q[:3])) / (640*360*3)
    assert average < 2, average

    def brightness(image, cx, cy):
        return sum(sum(image[y][x][:3]) for y in range(cy - 2, cy + 3) for x in range(cx - 2, cx + 3)) / 75
    # The packaged scene-wide bake lights the floor near the lamp and leaves its far corner dark.
    lit, dark = brightness(cooked, 420, 313), brightness(cooked, 110, 330)
    assert lit > dark + 40 and abs(lit - brightness(source, 420, 313)) < 12, (lit, dark)
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', probes=observations, focus=focus,
        bakedTexels=int(match[1]), occludedSamples=int(match[2]), sourceUnavailable=True, cookedMeanError=average), indent=2), encoding='utf-8')
    print(root)


if __name__ == '__main__':
    main()
