"""Run the particle acceptance suite and retain commands, seeds, binaries and artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time


def sha256(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('ctest', 'build', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    tests = [
        ('kb_21kb_particle_cpu_backend_tests', 'CPU', 'fixed fixture seeds; golden, fallback and events'),
        ('kb_21kb_particle_cpu_performance', 'CPU', 'shared seed 0; distinct seeds 0..255'),
        ('kb_21kb_particle_renderer_tests', 'renderer contracts', 'snapshot fixtures'),
        ('kb_21kb_particle_editor_tests', 'CPU and Noop renderer', 'preview restart and 100 lifecycle cycles'),
        ('kb_renderer_particle_motion', 'D3D11', 'authored seed 0x5EEDF11E; compute field fixtures'),
        ('kb_renderer_particle_contact_death', 'D3D11', 'authored seed 0x5EEDF11E'),
        ('kb_renderer_particle_depth_collision', 'D3D11', 'authored seed 0x5EEDF11E'),
        ('kb_renderer_particle_sort_modes', 'D3D11', 'authored seed 0x5EEDF11E; fixed overlapping births'),
        ('kb_renderer_particle_blend_goldens', 'D3D11', 'fixed snapshot, no random sampling'),
        ('kb_game_particle_gpu_package', 'D3D11 and CPU', 'component deterministicSeed=1'),
        ('kb_editor_particle_authoring_package', 'headless GUI and packaged CPU', 'authored seed retained in project asset'),
        ('kb_editor_particle_authoring_headless', 'headless GUI', 'authored seed retained in project asset'),
    ]
    binaries = [args.build / 'bin/kb_renderer_tests.exe', args.build / 'bin/kb_editor.exe',
                args.build / '21kb_particle/kb_21kb_particle_cpu_backend_tests.exe',
                args.build / '21kb_particle/kb_21kb_particle_editor_tests.exe',
                args.build / '21kb_particle/kb_21kb_particle_plugin.dll']
    report = dict(schema='21kb.particle-semantic-suite/v1', result='RUNNING',
                  binaries=[dict(path=str(path.resolve()), sha256=sha256(path)) for path in binaries], tests=[])
    destination = args.output / 'report.json'
    for name, backend, seeds in tests:
        command = [str(args.ctest), '--test-dir', str(args.build), '-R', '^' + re.escape(name) + '$', '-V', '--no-tests=error']
        start = time.monotonic()
        result = subprocess.run(command, capture_output=True, encoding='utf-8', errors='replace', timeout=600)
        log = args.output / (name + '.log')
        log.write_text(result.stdout + result.stderr, encoding='utf-8')
        report['tests'].append(dict(name=name, backend=backend, seedDescription=seeds,
            command=command, exitCode=result.returncode, seconds=time.monotonic() - start,
            log=str(log.resolve()), sha256=sha256(log)))
        destination.write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(name, result.returncode, flush=True)
    artifacts = []
    for directory in ('particle_package_tests', 'particle_authoring_tests'):
        parent = args.build / directory
        runs = [path for path in parent.iterdir() if path.is_dir()] if parent.exists() else []
        if not runs:
            continue
        latest = max(runs, key=lambda path: path.stat().st_mtime_ns)
        for path in sorted(latest.rglob('*')):
            if path.is_file() and path.suffix in ('.json', '.jsonl', '.kbvfx'):
                entry = dict(path=str(path.resolve()), sha256=sha256(path))
                if path.suffix == '.kbvfx':
                    entry['seed'] = next((line.split()[-1] for line in path.read_text(encoding='utf-8').splitlines()
                                          if line.startswith('effect.determinismSeed ')), None)
                artifacts.append(entry)
    report['artifacts'] = artifacts
    source = Path(__file__).resolve().parents[3]
    report['fixtureSources'] = [dict(path=str(path.relative_to(source)), sha256=sha256(path)) for path in (
        source / 'sources/renderer/tests/RendererRuntimeSubmitTests.cpp',
        source / 'sources/renderer/tests/ParticleGpuMotionTests.cpp',
        source / 'sources/plugins/21kb_particle/tests/CpuParticleBackendTests.cpp',
        source / 'sources/game/tests/ParticlePackagedGpuTests.py')]
    report['result'] = 'PASS' if all(test['exitCode'] == 0 for test in report['tests']) else 'FAIL'
    destination.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(destination)
    raise SystemExit(0 if report['result'] == 'PASS' else 1)


if __name__ == '__main__':
    main()
