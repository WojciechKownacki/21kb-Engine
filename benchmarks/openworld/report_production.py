"""Summarize three repeated native measurements; never infer competitor parity."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--before', type=Path, required=True)
p.add_argument('--after', type=Path, required=True)
a = p.parse_args()


def describe(values):
    ordered = sorted(values)
    return {'mean': statistics.mean(ordered),
            'p99': ordered[max(0, math.ceil(len(ordered) * .99) - 1)],
            'max': ordered[-1], 'min': ordered[0]}


def read(root, case, kind):
    runs = []
    for rep in range(1, 4):
        path = root / 'Results/raw' / f'{case}_{kind}_{rep}.csv'
        rows = list(csv.DictReader(path.open()))
        if kind == 'cpu':
            assert len(rows) == 360, path
            if case in ('physics_4096', 'dense_4096', 'physics_idle_world_100k'):
                assert all(int(r['fixed_steps']) == 1 for r in rows), path
            if case in ('physics_4096', 'dense_4096'):
                assert all(int(r['awake_bodies']) == 4096 for r in rows), path
        elif kind.endswith('gpu'):
            assert len(rows) == (3420 if case == 'mixed_stream_10k' else 600), path
            assert all(int(r['dropped']) == int(r['missing_resources']) == 0 for r in rows), path
            if kind == 'fixed_gpu':
                assert all(int(r['fixed_steps']) == 1 for r in rows), path
            if case != 'mixed_stream_10k':
                rows = rows[120:]
        assert rows, path
        runs.append(rows)
    common = set(runs[0][0])
    metrics = {}
    for column in sorted(common):
        if column in ('frame', 'phase', 'cycle'):
            continue
        per_run = [[float(r[column]) for r in rows if float(r[column]) >= 0] for rows in runs]
        if all(per_run):
            metrics[column] = describe([v for values in per_run for v in values])
            metrics[column]['run_means'] = [statistics.mean(values) for values in per_run]
    return metrics


cases = [('static_100k', 'cpu', 'runtime_ms', .1, None),
         ('physics_idle_world_100k', 'cpu', 'runtime_ms', .1, None),
         ('physics_4096', 'cpu', 'runtime_ms', 5, 8),
         ('dense_4096', 'cpu', 'runtime_ms', None, None),
         ('batch_dirty_100k', 'cpu', 'runtime_ms', None, None),
         ('world_100k', 'gpu', 'wall_frame_ms', 12, 16.67),
         ('city_dense_50k', 'gpu', 'wall_frame_ms', 16.67, 25),
         ('culling_far_10k', 'gpu', 'wall_frame_ms', None, None),
         ('culling_side_10k', 'gpu', 'wall_frame_ms', None, None),
         ('foliage_100k', 'gpu', 'wall_frame_ms', None, None),
         ('foliage_1m', 'gpu', 'wall_frame_ms', 16.67, None),
         ('city_dense_50k', 'fixed_gpu', 'wall_frame_ms', 16.67, 25),
         ('geometry_10k', 'fixed_gpu', 'wall_frame_ms', None, None),
         ('lights_512', 'fixed_gpu', 'wall_frame_ms', None, None),
         ('shadow_50k', 'fixed_gpu', 'wall_frame_ms', None, None),
         ('mixed_stream_10k', 'fixed_gpu', 'wall_frame_ms', None, None),
         ('static_100k', 'async', 'update_ms', None, 8)]
report = []
lines = ['21kb: pomiary produkcyjnych ścieżek CPU/GPU',
         'Release; i7-13700F, RTX 4070, 64 GB; D3D11 1920x1080.',
         'Trzy powtórki. GPU: 120 klatek rozgrzewki, 480 mierzonych; mieszany: wszystkie 3420.',
         'CPU: 180 rozgrzewki, 360 mierzonych; streaming: wszystkie klatki.',
         'P99: percentyl z połączonych próbek. GPU timer jest opóźniony; -1 pominięto.',
         'Liczniki renderowanych instancji i świateł są sumą pasów, nie liczbą unikalnych obiektów.',
         'Roślinność: cztery trójkąty, bez alpha, wiatru i cieni. Wynik nie certyfikuje gier AAA.', '',
         'Scenariusz | Przed średnia | Po średnia | Po P99 | Po maksimum | Zmiana | Bramka',
         '--- | ---: | ---: | ---: | ---: | ---: | ---']
for case, kind, metric, mean_limit, p99_limit in cases:
    before, after = read(a.before, case, kind), read(a.after, case, kind)
    old, new = before[metric], after[metric]
    change = (new['mean'] / old['mean'] - 1) * 100
    gates = []
    if mean_limit is not None:
        gates.append(new['mean'] <= mean_limit)
    if p99_limit is not None:
        gates.append(new['p99'] <= p99_limit)
    if kind == 'async':
        gates.append(new['max'] <= 16.67)
        assert after['operations']['max'] <= 256
    status = ('PASS' if all(gates) else 'FAIL') if gates else 'pomiar'
    report.append({'case': case, 'kind': kind, 'before': before, 'after': after,
                   'change_percent': change, 'gate': status, 'regression_over_5_percent': change > 5})
    lines.append(f"{case} ({kind}) | {old['mean']:.4f} | {new['mean']:.4f} | "
                 f"{new['p99']:.4f} | {new['max']:.4f} | {change:+.1f}% | {status}")
lines.extend(['', 'Szczegółowe metryki i średnie każdej powtórki: production_summary.json.',
              'Nieudane bramki i regresje są zachowane w raporcie.'])
destination = a.after / 'Results'
destination.mkdir(parents=True, exist_ok=True)
(destination / 'production_summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
(destination / 'Raport_produkcyjny.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('\n'.join(lines))
