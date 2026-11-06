"""Drive one Lua debug protocol through CLI, headless editor, source and cooked player."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('editor', 'cli', 'scenario', 'test-root', 'cooker', 'player', 'shaderc', 'engine-root'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--plugin', type=Path, action='append', default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='LuaDebugger', dir=args.test_root)).resolve()
    history = []

    def run(exe, *tail, expected=0):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, capture_output=True, encoding='utf-8', errors='replace', timeout=180)
        history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == expected, history[-1]
        return result.stdout

    def debug(endpoint, command, *tail, expected=0):
        response = json.loads(run(args.cli, 'debug', '--endpoint', endpoint, '--command', command, *tail, expected=expected))
        assert response['schema'] == '21kb.lua-debug/v1' and response['succeeded'] == (expected == 0), response
        return response

    points = root / 'breakpoints.json'
    points.write_text(json.dumps([dict(chunk='DebugProbe.lua', line=7)]))
    empty = root / 'empty.json'
    empty.write_text('[]')

    def session(process, endpoint, loss=False):
        deadline = time.monotonic() + 30
        while not (endpoint / 'endpoint.json').exists():
            assert process.poll() is None, ('host exited before endpoint', process.returncode)
            assert time.monotonic() < deadline, 'endpoint deadline'
            time.sleep(.02)

        def paused(line=None):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                reply = debug(endpoint, 'status')
                if reply['paused'] and (line is None or reply['line'] == line):
                    return reply
                time.sleep(.02)
            raise AssertionError(('pause deadline', line, reply))

        debug(endpoint, 'connect')
        debug(endpoint, 'breakpoints', '--breakpoints', points)
        first = paused(7)
        assert first['chunk'].endswith('DebugProbe.lua'), first
        assert debug(endpoint, 'stack')['stack'], 'no stack'
        locals_ = debug(endpoint, 'locals')['stack'][0]['locals']
        assert any(v['name'] == 'counter' and v['value'] == '40' for v in locals_), locals_
        assert debug(endpoint, 'evaluate', '--expression', 'counter + 2')['result']['value'] == '42'
        debug(endpoint, 'evaluate', '--expression', 'counter +', expected=1)
        debug(endpoint, 'step_into')
        nested = paused(2)
        assert len(nested['stack']) >= 2, nested
        assert debug(endpoint, 'evaluate', '--frame', '1', '--expression', 'counter')['result']['value'] == '40'
        debug(endpoint, 'step_over')
        paused(3)
        debug(endpoint, 'step_out')
        paused(8)
        debug(endpoint, 'continue')
        paused(7)
        debug(endpoint, 'breakpoints', '--breakpoints', empty)
        debug(endpoint, 'continue')
        debug(endpoint, 'pause')
        paused()
        debug(endpoint, 'disconnect')
        debug(endpoint, 'status', expected=1)
        if loss:
            debug(endpoint, 'connect')
            debug(endpoint, 'breakpoints', '--breakpoints', points)
            paused(7)
            # No client requests: the production 30-second lease must release the coroutine.
            time.sleep(31.5)
            assert process.poll() is None, 'editor stopped after client loss'
            debug(endpoint, 'status', expected=1)

    with (root / 'editor.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen(list(map(str, [args.editor.resolve(), '--selftest-scenario', args.scenario.resolve(),
            '--selftest-root', root, '--selftest-task', 'GUI'])), stdout=log, stderr=subprocess.STDOUT)
        try:
            session(process, root / 'GUI/Debugger', loss=True)
            assert (root / 'GUI/debugger-gui-pass.txt').is_file(), 'paused GUI interactions not verified'
            (root / 'GUI/debugger-done.txt').write_text('done')
            assert process.wait(timeout=15) == 0, (root / 'GUI/report.txt').read_text()
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

    project = root / 'GUI/workspace'
    run(args.cli, 'entity', 'add', '--project', project, '--name', 'Camera', '--create', 'Camera', '--position', '0,0,-4')
    package = root / 'Package'
    package.mkdir()
    run(args.cooker, '--project', project, '--target', 'Windows.x64', '--output', package / 'Game.kbpack',
        '--engine-root', args.engine_root.resolve(), '--shaderc', args.shaderc.resolve(), '--cache', root / 'CookCache')
    shutil.copy2(args.player, package / args.player.name)
    for dll in [*args.player.resolve().parent.glob('*.dll'), *args.plugin]:
        shutil.copy2(dll, package / dll.name)

    def player(label, executable, *extra):
        endpoint = root / (label + 'Debugger')
        with (root / (label + '.log')).open('w', encoding='utf-8') as log:
            process = subprocess.Popen(list(map(str, [executable.resolve(), '--headless', '--frames=1200',
                '--width=256', '--height=256', '--lua-debug-dir=' + str(endpoint), *extra])), stdout=log, stderr=subprocess.STDOUT)
            try:
                session(process, endpoint)
                assert process.wait(timeout=90) == 0, label
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        assert 'errors=0' in (root / (label + '.log')).read_text(encoding='utf-8'), label

    player('source', args.player, '--project=' + str(project))
    hidden = root / 'SourceUnavailable'
    project.rename(hidden)
    try:
        player('package', package / args.player.name)
    finally:
        hidden.rename(project)
    (root / 'result.json').write_text(json.dumps(dict(passed=True, hosts=['editor', 'source', 'package'],
        commands=['breakpoints', 'step_into', 'step_over', 'step_out', 'stack', 'locals', 'evaluate', 'continue', 'pause', 'disconnect'],
        editorGuiWhilePaused=True, editorConnectionLossRecovered=True), indent=2))
    print(root)


if __name__ == '__main__':
    main()
