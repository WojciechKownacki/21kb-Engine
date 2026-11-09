"""Author a UDP client, run its cooked player, and reconnect to a restarted server."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


FLOW = '''local connected, disconnected, spawned, despawned, replies = 0, 0, 0, 0, 0
local replica, frames, sent = 0, 0, false
function Ready(self)
 assert(CallFunction("Net.RegisterRpc", {name="MoveObject",direction="toServer",args="string(32)"}))
 assert(CallFunction("Net.RegisterRpc", {name="MoveReply",direction="toClients",args="string(32)"}))
end
function OnNetConnected(self,event) connected=connected+1;sent=false end
function OnNetDisconnected(self,event) disconnected=disconnected+1;sent=false end
function OnNetSpawned(self,event) spawned=spawned+1;replica=event.args.entity end
function OnNetDespawned(self,event) despawned=despawned+1;replica=0 end
function OnRpc_MoveObject(self,event)
 assert(CallFunction("Net.IsServer",{}),"movement must execute on the server")
 local object=World.FindByName("Net_Object")
 assert(object~=0,"authoritative object missing")
 local p=Transform.GetPosition(object)
 Transform.SetPosition(object,p.x+1,p.y,p.z)
 assert(CallFunction("Net.Call",{rpc="MoveReply",target=event.args.sender,s1=event.args.s1}))
end
function OnRpc_MoveReply(self,event) if event.args.s1=="native" then replies=replies+1 end end
function Tick(self,dt)
 if CallFunction("Net.IsServer",{}) then return end
 frames=frames+1
 if connected>disconnected and not sent then sent=CallFunction("Net.Call",{rpc="MoveObject",s1="native"}) end
 local x=-10
 if replica~=0 then
  local p=Transform.GetPosition(replica)
  local proxy=World.FindByName("VisualProxy")
  assert(proxy~=0,"visual proxy missing")
  Transform.SetPosition(proxy,p.x,p.y,p.z)
  local shown=Transform.GetPosition(proxy)
  assert(math.abs(shown.x-p.x)<0.00001 and math.abs(shown.y-p.y)<0.00001 and math.abs(shown.z-p.z)<0.00001,
   "visual proxy did not follow the replica")
  x=p.x
 end
 if frames%15==0 then
  Log(string.format('native-network {"connected":%d,"disconnected":%d,"spawned":%d,"despawned":%d,"replies":%d,"x":%.4f}',
   connected,disconnected,spawned,despawned,replies,x))
 end
end
'''


def atomic(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2); stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    assert json.loads(temporary.read_text(encoding='utf-8')) == value
    os.replace(temporary, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cli', 'cooker', 'player', 'server', 'engine-root', 'shaderc', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    parser.add_argument('--runtime-dll', type=Path, action='append', default=[])
    args = parser.parse_args()
    for name, value in vars(args).items():
        if isinstance(value, Path): setattr(args, name, value.resolve())
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='NativeNetwork', dir=args.test_root))
    history, owned = [], []
    environment = {**os.environ, 'CMAKE_BUILD_PARALLEL_LEVEL': '2'}

    def run(executable, *tail, expected=0, cwd=None):
        command = list(map(str, [executable, *tail]))
        result = subprocess.run(command, cwd=cwd or args.player.parent, env=environment,
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=600)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        atomic(root/'commands.json', history)
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    def cli(*tail): return run(args.cli, *tail)
    server_project, client_project = root/'ServerProject', root/'ClientProject'
    for project, template in ((server_project, 'Empty'), (client_project, 'Basic')):
        cli('new', '--parent', root, '--name', project.name, '--template', template)
        cli('entity', 'add', '--project', project, '--name', 'NetworkFlow')
        cli('script-new', '--project', project, '--name', 'NetworkFlow', '--language', 'lua')
        (project/'Assets/Scripts/NetworkFlow.lua').write_text(FLOW, encoding='utf-8')
        cli('validate', '--project', project, project/'Assets/Scripts/NetworkFlow.lua')
        cli('scene-attach', '--project', project, '--scene', '/Game/Scenes/Main.21kbscene',
            '--node', 'NetworkFlow', '--script', '/Game/Scripts/NetworkFlow.lua')
    cli('entity', 'add', '--project', server_project, '--name', 'Net_Object', '--position', '2,1,0')
    cli('entity', 'add', '--project', client_project, '--create', 'Cube', '--name', 'VisualProxy', '--position', '-2,1,0')

    package = root/'ClientPackage'; package.mkdir()
    run(args.cooker, '--project', client_project, '--target', 'Windows.x64', '--output', package/'Game.kbpack',
        '--engine-root', args.engine_root, '--shaderc', args.shaderc, '--cache', args.test_root/'CookCache')
    player = package/'NetworkSample.exe'; shutil.copy2(args.player, player)
    for library in [*args.plugin, *args.runtime_dll]: shutil.copy2(library, package/library.name)
    assert client_project.resolve().is_relative_to(root.resolve())
    unavailable = root/'ClientSourceUnavailable'
    assert unavailable.resolve().is_relative_to(root.resolve()) and not unavailable.exists()
    client_project.rename(unavailable)
    key = '31' * 32
    keys = root/'keys.txt'; keys.write_text('7 ' + key + '\n', encoding='utf-8')
    stop, port_file, status = root/'stop', root/'port', root/'server-status.json'
    logs = []

    def start_server(port, tag):
        stop.unlink(missing_ok=True); port_file.unlink(missing_ok=True)
        stream = (root/(tag+'.log')).open('w', encoding='utf-8'); logs.append(stream)
        command = list(map(str, [args.server, '--project', server_project, '--keys', keys, '--bind', '127.0.0.1',
            '--port', port, '--replicate', 'prefix:Net_', '--port-file', port_file, '--status-file', status,
            '--stop-file', stop, '--max-seconds', '80']))
        process = subprocess.Popen(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT, env=environment)
        owned.append(process)
        deadline = time.monotonic() + 15
        while True:
            assert process.poll() is None, 'Server exited: ' + tag
            if port_file.exists() and port_file.read_text().strip(): return process, int(port_file.read_text().strip())
            assert time.monotonic() < deadline, 'Server startup timed out'
            time.sleep(.05)

    def observations(log):
        text = log.read_text(encoding='utf-8', errors='replace')
        return [json.loads(value) for value in re.findall(r'native-network (\{[^\r\n]+\})', text)]

    def wait_client(process, log, predicate):
        deadline = time.monotonic() + 22
        while True:
            assert process.poll() is None, 'Client exited before expected network state: ' + log.read_text(errors='replace')
            values = observations(log)
            for value in reversed(values):
                if predicate(value): return value
            assert time.monotonic() < deadline, 'Client network state timed out: ' + str(values[-3:])
            time.sleep(.05)

    report = {'result': 'FAIL', 'sourceUnavailable': True}
    try:
        bad_settings = root/'invalid-session.json'
        atomic(bad_settings, dict(address='127.0.0.1', port=0, clientId='7', key=key))
        diagnostic = run(player, '--headless', '--frames=1', '--network-config='+str(bad_settings), expected=1, cwd=package)
        assert 'invalid network settings' in diagnostic and key not in diagnostic
        missing = run(player, '--headless', '--frames=1', '--network-config='+str(root/'absent.json'), expected=1, cwd=package)
        assert 'network settings file could not be opened' in missing
        server, port = start_server(0, 'server-first')
        config = root/'session.json'
        atomic(config, dict(address='127.0.0.1', port=port, clientId='7', key=key, reconnectAttempts=3))
        # The ordinary shipping executable accepts session configuration without development switches.
        client_log = root/'client.log'
        with client_log.open('w', encoding='utf-8') as output:
            minimal = {**environment, 'PATH': str(Path(os.environ['SystemRoot'])/'System32')}
            client = subprocess.Popen([str(player), '--headless', '--frames=1800', '--width=640', '--height=360',
                '--network-config='+str(config)], cwd=package, env=minimal, stdout=output, stderr=subprocess.STDOUT)
            owned.append(client)
            first = wait_client(client, client_log, lambda s: s['spawned'] == 1 and s['replies'] == 1 and abs(s['x']-3) < .02)
            stop.touch(); assert server.wait(timeout=10) == 0
            atomic(root/'server-first-status.json', json.loads(status.read_text(encoding='utf-8')))
            server, restored_port = start_server(port, 'server-restarted'); assert restored_port == port
            second = wait_client(client, client_log, lambda s: s['connected'] == 2 and s['disconnected'] == 1 and
                s['spawned'] == 2 and s['despawned'] == 1 and s['replies'] == 2 and abs(s['x']-3) < .02)
            assert client.wait(timeout=45) == 0
        text = client_log.read_text(encoding='utf-8')
        assert 'shutdown=clean' in text and 'errors=0' in text, text
        # The development host reads the same final asset pack and captures the rendered replica proxy.
        screenshot = root/'network.png'
        rendered = run(args.player, '--project='+str(package/'Game.kbpack'), '--headless', '--frames=240',
            '--width=640', '--height=360', '--network-config='+str(config), '--screenshot-file='+str(screenshot),
            '--screenshot-frame=220', '--profile-fixed-step', '--profile-file='+str(root/'connected.csv'), cwd=package)
        assert 'network: replicas=1' in rendered and 'shutdown=clean' in rendered and 'errors=0' in rendered, rendered
        assert screenshot.is_file() and screenshot.stat().st_size > 1000
        captured_states = [json.loads(value) for value in re.findall(r'native-network (\{[^\r\n]+\})', rendered)]
        assert any(s['connected'] == 1 and s['replies'] == 1 and abs(s['x']-4) < .02 for s in captured_states), captured_states[-3:]
        stop.touch(); assert server.wait(timeout=10) == 0
        disconnected_image = root/'without-server.png'
        disconnected = run(args.player, '--project='+str(package/'Game.kbpack'), '--headless', '--frames=240',
            '--width=640', '--height=360', '--network-config='+str(config), '--screenshot-file='+str(disconnected_image),
            '--screenshot-frame=220', '--profile-fixed-step', '--profile-file='+str(root/'disconnected.csv'), cwd=package)
        assert 'shutdown=clean' in disconnected and 'errors=0' in disconnected and 'network: replicas=1' not in disconnected, disconnected
        disconnected_states = [json.loads(value) for value in re.findall(r'native-network (\{[^\r\n]+\})', disconnected)]
        assert disconnected_states and all(s['connected'] == 0 and s['spawned'] == 0 for s in disconnected_states), disconnected_states[-3:]
        sys.path.insert(0, str(args.engine_root/'scripts'))
        from png_readback import read_png
        connected_pixels, disconnected_pixels = read_png(screenshot), read_png(disconnected_image)
        assert len(connected_pixels) == len(disconnected_pixels) == 360
        assert all(len(row) == 640 for row in connected_pixels + disconnected_pixels)
        changed_pixels = sum(a != b for row_a, row_b in zip(connected_pixels, disconnected_pixels)
            for a, b in zip(row_a, row_b))
        assert changed_pixels >= 64, 'Replicated proxy movement did not change the rendered frame'
        report.update(result='PASS', first=first, afterRestart=second, shippedPlayerPassed=True,
            screenshot=str(screenshot), disconnectedScreenshot=str(disconnected_image), changedPixels=changed_pixels,
            scope='Cooked Windows UDP client, gameplay RPC and visible replica proxy; real dedicated-server restart')
    finally:
        stop.touch()
        for process in owned:
            if process.poll() is None:
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
        for stream in logs: stream.close()
        atomic(root/'result.json', report)
    print(json.dumps(report))


if __name__ == '__main__': main()
