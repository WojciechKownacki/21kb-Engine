"""Author animated and streamed 3D projects through the CLI and verify their packages."""
from __future__ import annotations
import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import wave


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8', newline='\n') as output:
        json.dump(value, output, ensure_ascii=False, indent=2)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    assert json.loads(temporary.read_text(encoding='utf-8')) == value
    temporary.replace(path)


def make_sources(folder):
    """Standard authoring inputs; engine documents are created only by CLI commands."""
    folder.mkdir()
    payload, views, accessors = bytearray(), [], []
    def data(values, fmt, kind, count, component=5126):
        while len(payload) % 4:
            payload.append(0)
        blob = struct.pack('<' + fmt * len(values), *values)
        views.append(dict(buffer=0, byteOffset=len(payload), byteLength=len(blob)))
        payload.extend(blob)
        accessors.append(dict(bufferView=len(views)-1, componentType=component, count=count, type=kind))
        return len(accessors)-1
    position = data([-1,0,0, 1,0,0, -1,2,0, 1,2,0], 'f', 'VEC3', 4)
    normal = data([0,0,-1]*4, 'f', 'VEC3', 4)
    uv = data([0,0,1,0,0,1,1,1], 'f', 'VEC2', 4)
    joint = data([0,0,0,0]*2 + [1,0,0,0]*2, 'H', 'VEC4', 4, 5123)
    weight = data([1,0,0,0]*4, 'f', 'VEC4', 4)
    index = data([0,2,1,2,3,1], 'H', 'SCALAR', 6, 5123)
    identity = [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
    bind = data(identity + identity[:13] + [-1,0,1], 'f', 'MAT4', 2)
    time = data([0,1], 'f', 'SCALAR', 2)
    pose = data([0,1,0,1,1,0], 'f', 'VEC3', 2)
    gltf = dict(asset={'version':'2.0'}, buffers=[dict(uri='SampleRig.bin', byteLength=len(payload))],
        bufferViews=views, accessors=accessors,
        meshes=[dict(primitives=[dict(attributes={'POSITION':position,'NORMAL':normal,'TEXCOORD_0':uv,
            'JOINTS_0':joint,'WEIGHTS_0':weight}, indices=index)])],
        nodes=[dict(name='Root',children=[1]), dict(name='Tip',translation=[0,1,0]), dict(name='Skin',mesh=0,skin=0)],
        skins=[dict(joints=[0,1],inverseBindMatrices=bind)],
        animations=[dict(name='Sway',samplers=[dict(input=time,output=pose,interpolation='LINEAR')],
            channels=[dict(sampler=0,target=dict(node=1,path='translation'))])])
    (folder/'SampleRig.bin').write_bytes(payload)
    write_json(folder/'SampleRig.gltf', gltf)
    # A shared external mesh gives the world builder real geometry for its proxies.
    (folder/'CellShape.obj').write_text('''v -1 0 -1
v 1 0 -1
v 1 2 -1
v -1 2 -1
v -1 0 1
v 1 0 1
v 1 2 1
v -1 2 1
f 1 3 2
f 1 4 3
f 5 6 7
f 5 7 8
f 1 2 6
f 1 6 5
f 4 8 7
f 4 7 3
f 1 5 8
f 1 8 4
f 2 3 7
f 2 7 6
''', encoding='utf-8')
    with wave.open(str(folder/'Tone.wav'), 'wb') as output:
        output.setparams((1,2,48000,0,'NONE','not compressed'))
        output.writeframes(struct.pack('<48000h', *[round(8192*math.sin(2*math.pi*440*i/48000)) for i in range(48000)]))


ANIMATED_SCRIPT = '''local frames, rms, prior, elapsed, verified = 0, 0, false, 0, false
local function call(name, args)
 local value, diagnostic = CallFunction(name, args or {})
 assert(value ~= nil, diagnostic)
 return value
end
function Ready(self)
 local loaded = call("Save.Read", {slot="reference"})
 assert(loaded.loaded or loaded.status == "FileNotFound", loaded.diagnostic)
 prior = loaded.loaded
 assert(call("Save.SetInt", {key="marker",value=42}))
 assert(call("Save.Write", {slot="reference"}))
 assert(call("Save.Clear"))
 assert(call("Save.Read", {slot="reference"}).loaded)
 local marker = call("Save.GetInt", {key="marker"})
 assert(marker.found and marker.value == 42)
 assert(Audio.ConfigureOffline())
 Log("reference-save " .. tostring(prior))
end
function Tick(self, dt)
 frames = frames + 1
 elapsed = elapsed + dt
 if frames == 3 then
  Audio.RenderOffline(8192)
  local pcm = Audio.RenderOffline(4096)
  Log(string.format('reference-pcm frames=%d rms=%.6f', pcm.frames, pcm.rms))
  assert(pcm.frames == 4096 and pcm.rms > 0.01 and pcm.rms < 0.3, "audio PCM missing")
  rms = pcm.rms
 end
 if frames == 15 or frames == 90 then
  local tip = call("Animator.Socket", {socket="Tip"})
  local body = Transform.GetPosition(World.FindByName("DropBody"))
  Log(string.format('reference-pose {"frame":%d,"tipX":%.6f,"bodyY":%.6f,"rms":%.6f}', frames, tip.x, body.y, rms))
 end
 if elapsed >= 1.5 and not verified then
  local body = Transform.GetPosition(World.FindByName("DropBody"))
  assert(math.abs(body.y - 0.5) < 0.2, "rigid body did not settle on the floor")
  local hit = call("Physics.Raycast", {originX=0,originY=4,originZ=0,directionX=0,directionY=-1,directionZ=0,distance=8})
  assert(hit.hit and hit.entity == World.FindByName("Floor"), "floor query failed")
  assert(rms > 0.01)
  Log("reference-animated PASS")
  verified = true
 end
end
'''

WORLD_SCRIPT = '''local phase, frames = 0, 0
local function call(name, args)
 local value, diagnostic = CallFunction(name, args or {})
 assert(value ~= nil, diagnostic)
 return value
end
function Tick(self, dt)
 frames = frames + 1
 local near = World.FindByName("RegionNear")
 local far = World.FindByName("RegionFar")
 if phase == 0 and near ~= 0 then
  local hit = call("Physics.Raycast", {originX=8,originY=8,originZ=8,directionX=0,directionY=-1,directionZ=0,distance=10})
  if not hit.hit or hit.entity ~= near then return end
  Log("reference-world near-loaded")
  Transform.SetPosition(self.entity, 136, 3, 0)
  phase = 1
 elseif phase == 1 and far ~= 0 and near == 0 then
  local hit = call("Physics.Raycast", {originX=136,originY=8,originZ=8,directionX=0,directionY=-1,directionZ=0,distance=10})
  if not hit.hit or hit.entity ~= far then return end
  Log("reference-world far-loaded-near-unloaded")
  Transform.SetPosition(self.entity, 8, 3, 0)
  phase = 2
 elseif phase == 2 and near ~= 0 and far == 0 then
  assert(call("Save.SetInt", {key="streamPhase",value=3}))
  assert(call("Save.Write", {slot="stream"}))
  assert(call("Save.Clear"))
  assert(call("Save.Read", {slot="stream"}).loaded)
  local value = call("Save.GetInt", {key="streamPhase"})
  assert(value.found and value.value == 3)
  Log("reference-world returned-near PASS")
  phase = 3
 end
 if frames == 360 then assert(phase == 3, "streaming cycle did not finish") end
end
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cli','cooker','player','engine-root','shaderc','test-root'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--plugin',type=Path,action='append',default=[])
    parser.add_argument('--runtime-dll',type=Path,action='append',default=[])
    args = parser.parse_args()
    for executable in (args.cli,args.cooker,args.player,args.shaderc,*args.plugin,*args.runtime_dll):
        assert executable.is_file(),f'Required binary is missing: {executable}'
    args.test_root.mkdir(parents=True,exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='Reference3D',dir=args.test_root)).resolve()
    history = []
    environment = os.environ.copy()
    environment['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'
    def run(executable,*tail,expected=0,cwd=None):
        command = list(map(str,[executable.resolve(),*tail]))
        result = subprocess.run(command,cwd=cwd or executable.resolve().parent,env=environment,
            capture_output=True,encoding='utf-8',errors='replace',timeout=600)
        history.append(dict(command=command,exitCode=result.returncode,stdout=result.stdout,stderr=result.stderr))
        write_json(root/'commands.json',history)
        assert result.returncode==expected,history[-1]
        return result.stdout+result.stderr
    def cli(*words,expected=0):return run(args.cli,*words,expected=expected)
    inputs = root/'SourceInputs'
    make_sources(inputs)
    suffix = root.name[-8:].replace('-','_')
    animated, world = root/('AnimatedScene_'+suffix), root/('StreamedWorld_'+suffix)
    def create(project):cli('new','--parent',root,'--name',project.name,'--template','Empty')
    def entity(project,name,create=None,position=None,scene=None):
        words=['entity','add','--project',project,'--name',name]
        if create:words+=['--create',create]
        if position:words+=['--position',position]
        if scene:words+=['--scene',scene]
        cli(*words)
    def add(project,name,kind):cli('component','add','--project',project,'--entity',name,'--type',kind)
    def set_fields(project,name,kind,*fields,expected=0):
        words=['component','set','--project',project,'--entity',name,'--type',kind]
        for field in fields:words+=['--set',field]
        return cli(*words,expected=expected)
    def script(project,name,text,node):
        cli('script-new','--project',project,'--name',name,'--language','lua')
        # Behaviour code is an authored source input, not a serialized engine document.
        (project/'Assets/Scripts'/f'{name}.lua').write_text(text,encoding='utf-8')
        cli('validate','--project',project,project/'Assets/Scripts'/f'{name}.lua')
        cli('scene-attach','--project',project,'--scene','/Game/Scenes/Main.21kbscene',
            '--node',node,'--script',f'/Game/Scripts/{name}.lua')
    def lighting(project,camera_position):
        entity(project,'MainCamera','Camera',camera_position)
        entity(project,'Sun','Directional Light')
        entity(project,'Environment','Environment')
    create(animated)
    cli('import','--project',animated,'--destination','/Game/Characters',inputs/'SampleRig.gltf','--skeletal','yes')
    cli('animator','new','--project',animated,'--name','Motion','--clip','/Game/Characters/SampleRig_Sway.kbanim')
    cli('animator','check','--project',animated,'--controller','/Game/Animation/Motion.kbanimcontroller',
        '--character','/Game/Characters/SampleRig.kbskeletalmesh')
    entity(animated,'Character')
    add(animated,'Character','MeshRenderer')
    set_fields(animated,'Character','MeshRenderer','mesh=/Game/Characters/SampleRig.kbskeletalmesh')
    add(animated,'Character','Animator')
    set_fields(animated,'Character','Animator','controller=/Game/Animation/Motion.kbanimcontroller')
    lighting(animated,'0,1.5,-6')
    entity(animated,'Floor','Cube','0,-0.5,0')
    set_fields(animated,'Floor','Transform','localScale=20,1,20')
    entity(animated,'DropBody','Cube','2,3,0')
    add(animated,'DropBody','Rigidbody')
    entity(animated,'Listener','Audio Listener')
    cli('import','--project',animated,'--destination','/Game/Audio',inputs/'Tone.wav')
    entity(animated,'Tone')
    add(animated,'Tone','AudioSource')
    set_fields(animated,'Tone','AudioSource','clipAssetId=/Game/Audio/Tone.21kb','loop=true','autoplay=true','volume=0.5','spatial=false')
    script(animated,'AnimatedProbe',ANIMATED_SCRIPT,'Character')
    create(world)
    cli('import','--project',world,'--destination','/Game/Meshes',inputs/'CellShape.obj')
    for name,x in [('RegionNear',8),('RegionMiddle',72),('RegionFar',136)]:
        entity(world,name,'Cube',f'{x},0,8')
        set_fields(world,name,'MeshRenderer','mesh=/Game/Meshes/CellShape.21kb')
        set_fields(world,name,'Collider','boxSize=2,2,2','center=0,1,0')
    cli('world','migrate','--project',world,'--scene',world/'Assets/Scenes/Main.21kbscene',
        '--out','Assets/Worlds/Stream.21kbworld','--cell-size','32')
    built = cli('world','build','--project',world,'--world','Assets/Worlds/Stream.21kbworld')
    assert re.search(r'built [1-9]\d* cells and [1-9]\d* HLOD',built),built
    for name in ('RegionNear','RegionMiddle','RegionFar'):
        cli('entity','remove','--project',world,'--name',name)
    lighting(world,'8,3,0')
    entity(world,'World')
    add(world,'World','ContentInstance')
    scene = world/'Assets/Scenes/Main.21kbscene'
    before = scene.read_bytes()
    set_fields(world,'World','ContentInstance','kind=3','assetId=/Game/Meshes/CellShape.21kb',expected=1)
    assert scene.read_bytes()==before,'Rejected content type changed the scene'
    set_fields(world,'World','ContentInstance','kind=3','assetId=/Game/Worlds/Stream.21kbworld')
    add(world,'MainCamera','StreamFocus')
    set_fields(world,'MainCamera','StreamFocus','outerRadius=40','innerRadius=24')
    script(world,'StreamProbe',WORLD_SCRIPT,'MainCamera')
    sys.path.insert(0,str(args.engine_root.resolve()/'scripts'))
    from png_readback import read_png
    def playback(executable,project,frames,image,profile):
        words=['--headless','--frames='+str(frames),'--uncapped','--width=640','--height=360',
            '--profile-fixed-step','--profile-file='+str(profile)]
        if project:words.insert(0,'--project='+str(project))
        if image:words+=['--screenshot-frame='+str(frames),'--screenshot-file='+str(image)]
        output=run(executable,*words,cwd=executable.resolve().parent)
        assert 'shutdown=clean' in output and 'errors=0' in output,output
        return output
    packages,results = [],[]
    for project,frames,marker in [(animated,120,'reference-animated PASS'),(world,360,'reference-world returned-near PASS')]:
        project_file=next(project.glob('*.21kbproject'))
        image=root/(project.name+'-source.png')
        output=playback(args.player,project_file,frames,image,root/(project.name+'-source-profile.json'))
        assert marker in output,output
        first=None
        if project==animated:
            source_poses = [json.loads(line) for line in re.findall(r'reference-pose (\{[^\r\n]+\})',output)]
            assert [pose['frame'] for pose in source_poses]==[15,90],source_poses
            assert abs(source_poses[0]['tipX']-source_poses[1]['tipX'])>0.15,'Imported skeletal animation did not advance'
            first=root/'animated-first.png'
            playback(args.player,project_file,15,first,root/'animated-first-profile.json')
            assert read_png(first)!=read_png(image),'Skinned mesh and physics scene did not change over time'
        package=root/(project.name+'-Package');package.mkdir()
        run(args.cooker,'--project',project,'--target','Windows.x64','--output',package/'Game.kbpack',
            '--engine-root',args.engine_root,'--shaderc',args.shaderc,'--cache',args.test_root/'CookCache')
        player=package/(project.name+'.exe');shutil.copy2(args.player,player)
        for dll in [*args.plugin,*args.runtime_dll]:shutil.copy2(dll,package/dll.name)
        packages.append((project,package,player,frames,marker,image,first,output))
    assert inputs.resolve().is_relative_to(root)
    inputs.rename(root/'InputsUnavailable')
    for project,package,player,frames,marker,source_image,first,source_output in packages:
        assert project.resolve().is_relative_to(root)
        project.rename(root/(project.name+'-SourceUnavailable'))
        # A shipped player uses real time and refuses development profile/file options.
        shipped=run(player,'--headless','--frames=360','--width=640','--height=360',cwd=package)
        assert marker in shipped and 'shutdown=clean' in shipped and 'errors=0' in shipped,shipped
        captured_image=root/(project.name+'-packaged.png')
        captured=playback(args.player,package/'Game.kbpack',frames,captured_image,root/(project.name+'-packaged-profile.json'))
        assert marker in captured,captured
        assert read_png(source_image)==read_png(captured_image),'Source/package frame differs: '+project.name
        if first:
            assert 'reference-save true' in captured,'Packaged save did not persist into the second process'
            poses=lambda text:[json.loads(line) for line in re.findall(r'reference-pose (\{[^\r\n]+\})',text)]
            assert poses(source_output)==poses(captured),'Skeletal pose/physics/audio differs after cooking'
            early=root/'animated-packaged-first.png'
            playback(args.player,package/'Game.kbpack',15,early,root/'animated-packaged-first-profile.json')
            assert read_png(first)==read_png(early),'Early animation frame differs after cooking'
        else:
            for line in ('near-loaded','far-loaded-near-unloaded','returned-near PASS'):
                assert 'reference-world '+line in shipped
        results.append(dict(project=project.name,result='PASS',sourceUnavailable=True,shippedPlayerPassed=True,
            sourcePackageImageDifference=0,animated=bool(first),streamedWorld=not bool(first)))
    write_json(root/'result.json',dict(result='PASS',workflows=results,projectDocumentsAuthoredBy='public CLI',
        sourceInputs='standard glTF/OBJ/WAV and Lua behaviour code',sourceInputsUnavailable=True))
    print('3D reference workflows passed:',root)


if __name__=='__main__':
    main()
