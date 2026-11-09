"""Execute the documented first-scene recipe and verify its exported player."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    if json.loads(temporary.read_text(encoding='utf-8')) != value:
        raise RuntimeError('Tutorial report validation failed')
    os.replace(temporary, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, required=True)
    parser.add_argument('--workspace', type=Path, required=True)
    args = parser.parse_args()
    engine = Path(__file__).resolve().parents[1]
    build, workspace = args.build_root.resolve(), args.workspace.resolve()
    if workspace.exists() and any(workspace.iterdir()):
        parser.error('Use a new or empty workspace; existing tutorial output is never overwritten')
    workspace.mkdir(parents=True, exist_ok=True)
    def binary(name):
        for folder in [build / 'bin/Release', build / 'bin']:
            candidate = folder / (name + '.exe')
            if candidate.is_file():
                return str(candidate)
        parser.error('Build the Release engine tools first: ' + name)
    values = dict(CLI=binary('kb_cli'), PLAYER=binary('kb_game'), PYTHON=sys.executable,
                  ENGINE=str(engine), BUILD=str(build), WORKSPACE=str(workspace),
                  PROJECT=str(workspace / 'SampleScene'))
    recipe = json.loads((engine / 'docs/tutorials/first_scene.json').read_text(encoding='utf-8'))
    environment = os.environ.copy()
    environment.setdefault('CMAKE_BUILD_PARALLEL_LEVEL', '2')
    history = []
    def run(command, cwd=engine):
        print(subprocess.list2cmdline(command), flush=True)
        result = subprocess.run(command, cwd=cwd, env=environment, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=3600)
        log = workspace / f'step-{len(history)+1:02}.log'
        log.write_text(result.stdout + result.stderr, encoding='utf-8')
        history.append(dict(command=command, exitCode=result.returncode, log=log.name))
        write_json(workspace / 'commands.json', history)
        if result.returncode:
            raise RuntimeError(f'Tutorial step failed; see {log}')
        return result.stdout
    for step in recipe['steps']:
        run([arg.format_map(values) for arg in step])
    exports = list((workspace / 'Export').rglob('SampleScene.exe'))
    if len(exports) != 1:
        raise RuntimeError('Export must contain exactly one playable SampleScene.exe')
    # Prove that the packaged player does not depend on the editable project.
    project, unavailable = workspace / 'SampleScene', workspace / 'SourceUnavailable'
    if not project.resolve().is_relative_to(workspace) or not unavailable.resolve().is_relative_to(workspace):
        raise RuntimeError('Tutorial project must remain within its workspace')
    project.rename(unavailable)
    try:
        output = run([str(exports[0]), '--headless', '--frames=30', '--width=640', '--height=360'], cwd=exports[0].parent)
        if 'rendered=30' not in output or 'shutdown=clean' not in output or 'errors=0' not in output:
            raise RuntimeError('Exported player did not render and shut down cleanly')
        if not (workspace / 'preview.png').is_file():
            raise RuntimeError('Headless GUI preview is missing')
    finally:
        if project.exists():
            raise RuntimeError('Cannot restore tutorial source: the destination already exists')
        unavailable.rename(project)
    report = dict(result='PASS', steps=len(history), sourceUnavailableDuringPackagedRun=True,
                  project=values['PROJECT'], exportedPlayer=str(exports[0]), preview=str(workspace / 'preview.png'))
    write_json(workspace / 'result.json', report)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
