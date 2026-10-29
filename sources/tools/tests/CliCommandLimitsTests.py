"""Exercise the real CLI supervisor and external native-build process tree."""
from __future__ import annotations

import argparse
import ctypes
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--test-root', type=Path, required=True)
    args = parser.parse_args()
    assert os.name == 'nt', 'This process supervision suite targets Windows'
    cli = args.cli.resolve(strict=True)
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='CommandLimits ż ', dir=args.test_root))
    checks = []

    def run(*words, expected=0):
        result = subprocess.run([str(cli), *map(str, words)], capture_output=True,
                                encoding='utf-8', timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
        assert result.returncode == expected, (words, result.returncode, result.stdout[:300], result.stderr[:300])
        return result

    def result_json(result, code, allow_stderr=False):
        value = json.loads(result.stdout)
        assert value['schema'] == '21kb.cli-result/v1'
        assert value['exitCode'] == code and value['ok'] == (code == 0)
        assert allow_stderr or not result.stderr, result.stderr[:300]
        return value

    normal = run('--json', '--command-timeout-ms', '10000', 'help')
    assert 'command-timeout-ms' in result_json(normal, 0)['output']
    unknown = run('--command-timeout-ms', '10000', 'unknown-command', '--json', expected=1)
    assert result_json(unknown, 1)['command'] == 'unknown-command'
    checks.append('completed JSON and child failure preserve exit codes')
    for invalid in ['0', '-1', '86400001', 'nan', '1x', '9999999999999999999999']:
        assert result_json(run('--json', '--command-timeout-ms', invalid, 'help', expected=125), 125)['reason'] == 'supervisor-error'
    for words in [('--command-timeout-ms', '1', '--command-timeout-ms', '2', 'help'),
                  ('--command-cancel-file', ''), ('--command-timeout-ms', '1'),
                  ('--command-cancel-file',)]:
        result_json(run('--json', *words, expected=125), 125)
    checks.append('invalid limits, duplicate values and missing command fail before execution')

    project_name = 'SampleScene'
    run('--command-timeout-ms', '10000', 'new', '--parent', root, '--name', project_name, '--template', 'Empty')
    project = root / project_name
    run('--json', '--command-timeout-ms', '10000', 'entity', 'add', '--project', project,
        '--name', 'quoted "name" with trailing\\')
    names = result_json(run('--json', '--command-timeout-ms', '10000', 'scene-list', '--project', project), 0)
    assert 'quoted "name" with trailing\\' in names['output']
    checks.append('UTF-8, spaces, quotes and trailing backslashes reach actual command')

    # This is an actual NativeScriptBuildPipeline command, not a mock CLI
    # dispatcher. It starts another process and emits enough data to fill pipes.
    helper = root / 'external tool.py'
    helper.write_text('''import json,os,subprocess,sys,time
from pathlib import Path
mode,pids,marker=sys.argv[1:]
if mode == "child":
    time.sleep(30)
    Path(marker).write_text("orphan",encoding="utf-8")
else:
    child=subprocess.Popen([sys.executable,__file__,"child",pids,marker])
    Path(pids).write_text(json.dumps([os.getpid(),child.pid]),encoding="utf-8")
    sys.stdout.write("native stdout\\n"+"x"*200000);sys.stdout.flush()
    sys.stderr.write("native stderr\\n"+"y"*200000);sys.stderr.flush()
    if mode == "flood":
        for _ in range(20):
            sys.stderr.write("z"*1048576);sys.stderr.flush()
    if mode != "background": time.sleep(30)
''', encoding='utf-8')
    scripts = project / 'Assets' / 'Scripts'
    scripts.mkdir(parents=True, exist_ok=True)
    module = project / 'Binaries' / 'NativeScripts' / 'Probe.dll'
    module.parent.mkdir(parents=True, exist_ok=True)
    module.write_bytes(b'not loaded by script-build')
    pids = root / 'pids.json'
    marker = root / 'orphan.txt'
    descriptor = scripts / 'Probe.native'

    def configure(mode):
        pids.unlink(missing_ok=True)
        command = subprocess.list2cmdline([sys.executable, str(helper), mode, str(pids), str(marker)])
        descriptor.write_text('name = Probe\nsymbol = project.Probe\n'
                              'module = ../../Binaries/NativeScripts/Probe.dll\nbuild = ' + command + '\n', encoding='utf-8')

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]

    def stopped():
        assert pids.exists(), 'external tool did not start'
        for pid in json.loads(pids.read_text(encoding='utf-8')):
            handle = kernel.OpenProcess(0x00100000, False, pid)
            if handle:
                try:
                    assert kernel.WaitForSingleObject(handle, 5000) == 0, ('orphaned external tool', pid)
                finally:
                    kernel.CloseHandle(handle)
            else:
                assert ctypes.get_last_error() == 87, ('cannot inspect external tool', pid, ctypes.get_last_error())
        assert not marker.exists(), 'a descendant survived its command'

    command = ['script-build', '--project', str(project), '--script', '/Game/Scripts/Probe.native']
    cancel = root / 'cancel flag'
    configure('wait')
    cancel.write_text('stop', encoding='utf-8')
    pre = result_json(run('--json', '--command-cancel-file', cancel, *command, expected=130), 130)
    assert pre['reason'] == 'cancelled' and not pids.exists() and cancel.exists()
    cancel.unlink()
    checks.append('existing cancellation prevents starting and preserves caller file')

    for json_mode in [False, True]:
        configure('wait')
        result = run(*(['--json'] if json_mode else []), '--command-timeout-ms', '2000', *command, expected=124)
        if json_mode:
            value = result_json(result, 124)
            assert value['reason'] == 'timeout' and 'native stderr' in '\n'.join(value['errors'])
        else:
            assert 'native stdout' in result.stderr and 'native stderr' in result.stderr
        stopped()
    checks.append('deadline interrupts actual external build, drains both pipes and kills descendants in text/JSON modes')

    configure('wait')
    process = subprocess.Popen([str(cli), '--json', '--command-cancel-file', str(cancel),
                                '--command-timeout-ms', '10000', *command], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        deadline = time.monotonic() + 7
        while not pids.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        assert pids.exists(), 'external build did not reach its cancellation point'
        cancel.write_text('stop', encoding='utf-8')
        stdout, stderr = process.communicate(timeout=7)
        assert process.returncode == 130 and not stderr
        assert json.loads(stdout)['reason'] == 'cancelled'
        stopped()
        assert cancel.exists()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=7)
    cancel.unlink()
    checks.append('live cancellation stops external build tree and retains valid JSON')

    configure('background')
    completed = run('--json', '--command-timeout-ms', '10000', *command)
    assert 'Built /Game/Scripts/Probe.native' in result_json(completed, 0, allow_stderr=True)['output']
    assert 'native stdout' in completed.stderr and 'native stderr' in completed.stderr
    stopped()
    checks.append('normal completion preserves command JSON and cleans background descendants')

    configure('flood')
    limited = result_json(run('--json', '--command-timeout-ms', '10000', *command, expected=125), 125)
    assert limited['reason'] == 'supervisor-error' and '16 MiB' in '\n'.join(limited['errors'])
    stopped()
    checks.append('bounded JSON capture rejects excessive external output and cleans process tree')

    run('--json', '--command-cancel-file', cancel, '--command-timeout-ms', '10000', 'help')
    checks.append('next invocation does not inherit cancellation')

    # A long-lived protocol must retain live stdout and inherited stdin rather
    # than waiting for the bounded command to finish before returning a reply.
    protocol = subprocess.Popen([str(cli), '--json', '--command-timeout-ms', '2000', 'mcp'],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        protocol.stdin.write(b'{"jsonrpc":"2.0","id":7,"method":"ping"}\n')
        protocol.stdin.flush()
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
            reply = executor.submit(protocol.stdout.readline).result(timeout=1.5)
        assert json.loads(reply)['id'] == 7 and protocol.poll() is None
        assert protocol.wait(timeout=5) == 124
        assert not protocol.stdout.read() and b'deadline exceeded' in protocol.stderr.read()
    finally:
        if protocol.poll() is None:
            protocol.kill()
            protocol.wait(timeout=5)
        protocol.stdin.close()
        protocol.stdout.close()
        protocol.stderr.close()
    checks.append('bounded MCP keeps interactive stdin/stdout and its protocol without JSON wrapping')

    isolated = root / 'Isolated'
    isolated.mkdir()
    relocated = isolated / cli.name
    shutil.copyfile(cli, relocated)
    environment = {'SystemRoot': os.environ['SystemRoot'], 'PATH': str(Path(os.environ['SystemRoot']) / 'System32'),
                   'TEMP': str(isolated), 'TMP': str(isolated), 'USERPROFILE': str(isolated)}
    clean = subprocess.run([str(relocated), '--json', '--command-timeout-ms', '10000', 'help'],
                           cwd=isolated, env=environment, capture_output=True, encoding='utf-8', timeout=15,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    assert clean.returncode == 0 and 'command-timeout-ms' in result_json(clean, 0)['output']
    checks.append('relocated executable supervises help without developer PATH or user configuration')
    proof = {'result': 'PASS', 'checks': checks, 'directory': str(root)}
    temporary = root / 'result.tmp'
    temporary.write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    assert json.loads(temporary.read_text(encoding='utf-8')) == proof
    temporary.replace(root / 'result.json')
    print(json.dumps(proof, indent=2))


if __name__ == '__main__':
    main()
