#!/usr/bin/env python3
"""Build and run comparable ECS and worker workloads in a Release build."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import subprocess


def execute(command, cwd, timeout=600):
    result = subprocess.run([str(arg) for arg in command], cwd=cwd, capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=timeout)
    if result.returncode:
        raise RuntimeError(f'{command[0]} exited {result.returncode}: {result.stderr}\n{result.stdout}')
    return result.stdout


def rows(output):
    parsed = {}
    for line in output.splitlines():
        columns = line.split(',')
        if columns and '=' not in columns[0]:
            parsed[columns[0]] = dict(column.split('=', 1) for column in columns[1:] if '=' in column)
    return parsed


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--entities', type=int, default=100000)
    parser.add_argument('--frames', type=int, default=120)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if min(args.entities, args.frames, args.repeats) < 1:
        parser.error('entities, frames and repeats must be positive')
    repo = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    settings = dict(line.split('=', 1) for line in cache.splitlines() if '=' in line and not line.startswith(('#', '//')))
    config = settings.get('CMAKE_BUILD_TYPE:STRING', '')
    if config != 'Release':
        raise RuntimeError('benchmark requires a single-configuration Release build')
    execute(['cmake', '--build', build, '--target', 'kb_ecs_workload_benchmark',
             'kb_ecs_worker_scaling_benchmark', '--parallel', '2'], repo)
    suffix = '.exe' if platform.system() == 'Windows' else ''
    workload = build / 'engine' / ('kb_ecs_workload_benchmark' + suffix)
    scaling = build / 'engine' / ('kb_ecs_worker_scaling_benchmark' + suffix)
    revision = execute(['git', 'rev-parse', 'HEAD'], repo).strip()
    diff = execute(['git', 'diff', '--binary', 'HEAD'], repo)
    dirty = bool(execute(['git', 'status', '--porcelain'], repo).strip())
    compiler = settings.get('CMAKE_CXX_COMPILER:FILEPATH', '')
    version_args = ['/Bv'] if Path(compiler).name.lower() in ('cl', 'cl.exe') else ['--version']
    version = subprocess.run([compiler, *version_args], capture_output=True, text=True, encoding='utf-8',
                             errors='replace', timeout=30) if compiler else None
    report = {'schema': '21kb.ecs-benchmark/v1', 'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'revision': revision, 'dirty': dirty, 'tracked_diff_sha256': hashlib.sha256(diff.encode()).hexdigest(),
              'build_type': config, 'compiler': compiler,
              'compiler_version': ((version.stderr + version.stdout).splitlines() or ['unavailable'])[0] if version else 'unavailable',
              'platform': platform.platform(), 'machine': platform.machine(),
              'workload_binary_sha256': digest(workload), 'scaling_binary_sha256': digest(scaling),
              'entities': args.entities, 'measured_frames': args.frames, 'repeats': args.repeats,
              'notes': ['Process memory is total process usage; allocation counts are requests, not retained bytes.',
                        'Flecs reference and engine use equivalent integration inputs; structural APIs differ.',
                        'Dirty tree revision does not identify untracked contents; binary hashes identify measured executables.'],
              'workloads': [], 'worker_scaling': []}
    expected = None
    for repeat in range(args.repeats):
        for mode, options in [('mirrored', ['direct-add']), ('native-only', ['native-only', 'direct-add']),
                              ('flecs-reference', ['flecs-reference'])]:
            output = execute([workload, args.entities, args.frames, *options], build)
            metrics = rows(output)
            result = metrics['result']
            if expected is None: expected = result
            if result != expected: raise RuntimeError(f'workload result mismatch: {mode}: {result} != {expected}')
            report['workloads'].append({'repeat': repeat, 'mode': mode, 'metrics': metrics})
    for mode in ('uniform', 'uneven'):
        expected_checksum = None
        for workers in (1, 2, 4):
            for repeat in range(args.repeats):
                metrics = rows(execute([scaling, workers, mode], build))
                result = metrics['worker_scaling']
                if result['correct'] != '1': raise RuntimeError('worker workload failed its internal verification')
                if expected_checksum is None: expected_checksum = result['checksum']
                if result['checksum'] != expected_checksum: raise RuntimeError('worker checksum mismatch')
                report['worker_scaling'].append({'repeat': repeat, 'mode': mode, 'workers': workers, 'metrics': metrics})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + '.tmp')
    temporary.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    temporary.replace(args.output)
    print(args.output)


if __name__ == '__main__':
    main()
