"""Summarize comparable CSV runs; keep timings and rendering counters separate."""
import argparse
import csv
import html
import json
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--before', type=Path, required=True)
p.add_argument('--after', type=Path, required=True)
a = p.parse_args()
before, after = a.before/'Results/raw', a.after/'Results/raw'

def read(paths, warmup=0):
    result = []
    for path in paths:
        with path.open() as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) <= warmup:
            raise ValueError(f'Incomplete run: {path}')
        result.extend(rows[warmup:])
    return result

def percentile(values, fraction):
    ordered = sorted(values)
    index = (len(ordered)-1)*fraction
    lower = int(index)
    upper = min(lower+1, len(ordered)-1)
    return ordered[lower] + (ordered[upper]-ordered[lower])*(index-lower)

def stats(rows, field):
    values = [float(row[field]) for row in rows]
    return {'mean': statistics.mean(values), 'p50': percentile(values, .5),
            'p99': percentile(values, .99), 'max': max(values), 'samples': len(values)}

comparison = []
def compare(case, field, old, new, category):
    b, n = stats(old, field), stats(new, field)
    comparison.append({'case': case, 'category': category, 'metric': field,
        'before_mean_ms': b['mean'], 'after_mean_ms': n['mean'],
        'before_p99_ms': b['p99'], 'after_p99_ms': n['p99'],
        'change_percent': 100*(n['mean']/b['mean']-1) if b['mean'] else None,
        'before_samples': b['samples'], 'after_samples': n['samples']})

cpu_cases = ['static_100k', 'physics_idle_world_100k', 'physics_4096', 'dense_4096', 'batch_dirty_100k']
setup = []
for case in cpu_cases:
    old = read([before/f'{case}_cpu_{rep}.csv' for rep in range(1, 4)])
    new = read([after/f'{case}_cpu_{rep}.csv' for rep in range(1, 4)])
    for field in ['total_ms', 'runtime_ms', 'fixed_capture_ms']:
        compare(case, field, old, new, 'CPU')
    old = read([before/f'{case}_setup_{rep}.csv' for rep in range(1, 4)])
    new = read([after/f'{case}_setup_{rep}.csv' for rep in range(1, 4)])
    setup.append({'case': case, 'before': {key: stats(old, key) for key in old[0]},
                  'after': {key: stats(new, key) for key in new[0]}})

render = []
gpu_cases = ['world_100k', 'city_dense_50k', 'culling_far_10k', 'culling_side_10k', 'foliage_100k', 'foliage_1m']
for case in gpu_cases:
    new = read([after/f'{case}_gpu_{rep}.csv' for rep in range(1, 4)], 120)
    assert all(int(row['dropped']) == 0 and int(row['missing_resources']) == 0 for row in new), case
    metrics = ['wall_frame_ms', 'simulation_ms', 'begin_submit_ms', 'end_frame_ms', 'gpu_delayed_ms',
               'visible_meshes', 'culled_instances', 'draws', 'scene_lights', 'submitted_lights',
               'skipped_lights', 'light_capacity', 'instance_upload_bytes']
    render.append({'case': case, 'resident_instances': 1000000 if case == 'foliage_1m' else 100000 if case == 'foliage_100k' else None,
                   'after': {key: stats(new, key) for key in metrics}})
    if case.startswith('foliage_') and (a.after/'Results/foliage_before').exists():
        old = read([a.after/'Results/foliage_before'/f'{case}_gpu_{rep}.csv' for rep in range(1, 4)], 120)
        for field in ['wall_frame_ms', 'begin_submit_ms', 'gpu_delayed_ms']:
            compare(case, field, old, new, 'Vegetation acceleration')
    if case.startswith('culling_'):
        assert all(int(row['visible_meshes']) == 0 and int(row['culled_instances']) == 20000 for row in new), case
    if not case.startswith('foliage_'):
        old = read([before/f'{case}_ForwardPlus_1920x1080_headless_{rep}.csv' for rep in range(1, 4)], 120)
        for field in ['wall_frame_ms', 'simulation_ms', 'begin_submit_ms', 'gpu_delayed_ms']:
            compare(case, field, old, new, 'Renderer')

stream = []
for rep in range(1, 4):
    rows = read([after/f'static_100k_async_{rep}.csv'])
    assert all(int(row['operations']) <= 256 for row in rows)
    for cycle in range(3):
        for phase in range(2):
            selected = [row for row in rows if int(row['cycle']) == cycle and int(row['phase']) == phase]
            assert int(selected[-1]['entities']) == (0 if phase else 100002)
    for phase in range(2):
        selected = [row for row in rows if int(row['phase']) == phase]
        stream.append({'rep': rep, 'phase': 'unload' if phase else 'load', 'frames': len(selected),
            'update_ms': stats(selected, 'update_ms'), 'stream_ms': stats(selected, 'stream_ms'),
            'max_operations': max(int(row['operations']) for row in selected),
            'frames_over_2ms_stream': sum(float(row['stream_ms']) > 2 for row in selected)})

result = {'baseline_commit': '48ddd0164f84c2f60d9dd047e839865e5ac70d86',
          'hardware': 'i7-13700F, 64 GiB RAM, RTX 4070 12 GiB, driver 591.74',
          'comparison': comparison, 'setup': setup, 'render': render, 'stream': stream}
directory = a.after/'Results'
(directory/'comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
with (directory/'comparison.csv').open('w', newline='', encoding='utf-8') as output:
    writer = csv.DictWriter(output, fieldnames=list(comparison[0]))
    writer.writeheader()
    writer.writerows(comparison)

def table(headers, rows):
    return '<table><thead><tr>'+''.join(f'<th>{html.escape(str(value))}</th>' for value in headers)+\
        '</tr></thead><tbody>'+''.join('<tr>'+''.join(f'<td>{html.escape(str(value))}</td>' for value in row)+'</tr>' for row in rows)+'</tbody></table>'

def fmt(value):
    return f'{value:.3f}'

parts = ['''<!doctype html><html lang="pl"><meta charset="utf-8"><title>21kb — pomiary po poprawkach</title>
<style>body{font:16px/1.6 system-ui;background:#101827;color:#e8edf5;max-width:1200px;margin:40px auto;padding:24px}h1,h2{color:#92d9c4}table{border-collapse:collapse;width:100%;margin:24px 0;font-size:14px}th,td{text-align:left;padding:10px;border-bottom:1px solid #344256}th{background:#203047}code{color:#e8c986}a{color:#92d9c4}.limit{border-left:4px solid #e8c986;padding:12px;background:#1d293b}</style>
<h1>21kb: duże światy — pomiary po poprawkach</h1>
<p>30.09.2026 · gałąź 1.0 · Release / MSVC · i7-13700F · 64 GiB RAM · RTX 4070 12 GiB · sterownik 591.74.</p>
<p class="limit">Potwierdzono skalę poniższych scen syntetycznych. Ten test nie dowodzi gotowości do produkcji gry klasy Wiedźmin, Cyberpunk czy GTA. Szczególnie milion instancji roślinności nie oznacza miliona aktywnych obiektów fizycznych.</p>
<h2>Porównanie przed / po</h2><p>CPU: 180 klatek rozgrzewki + 360 pomiarowych, trzy procesy. Renderer: trzy procesy po 600 klatek, pierwsze 120 odrzucone, D3D11, 1920×1080, ForwardPlus, uncapped, ukryte okno. Te same sceny i zasoby co w bazowym projekcie. P99 jest percentylem połączonych próbek, nie średnią percentyli. Bazą jest commit 48ddd016; dla nowych scen roślinności bazą są trzy pomiary tego samego projektu przed dodaniem cullingu grup (foliage_before/), już po wcześniejszych poprawkach. Komputer pozostawał dostępny użytkownikowi; duża zmienność i P99 nie są kontrolowanym laboratoryjnie limitem sprzętu.</p>''']
parts.append(table(['Scenariusz / metryka', 'Przed średnia ms', 'Po średnia ms', 'Przed P99 ms', 'Po P99 ms', 'Zmiana czasu'],
    [[f"{row['case']} / {row['metric']}", fmt(row['before_mean_ms']), fmt(row['after_mean_ms']), fmt(row['before_p99_ms']), fmt(row['after_p99_ms']), f"{row['change_percent']:+.1f}%"]
     for row in comparison if row['metric'] in ['runtime_ms', 'wall_frame_ms', 'total_ms']]))
parts.append('''<p class="limit">Regresje również są wynikiem testu: physics_4096, dense_4096, batch_dirty_100k i city_dense_50k mają w tych powtórkach większy średni koszt. Poprawa statycznego świata nie dowodzi przyspieszenia aktywnej fizyki. Do oddzielenia kosztu nowego cache interpolacji od obciążenia systemu potrzebne są kolejne kontrolowane profile; ten raport nie przypisuje całej różnicy jednej funkcji.</p>''')
parts.append('<h2>Koszt tworzenia dokumentu 100 tys. obiektów</h2>')
parts.append(table(['Scenariusz', 'Przed ms', 'Po ms', 'Mnożnik'],
    [[row['case'], fmt(row['before']['document_ms']['mean']), fmt(row['after']['document_ms']['mean']), fmt(row['before']['document_ms']['mean']/row['after']['document_ms']['mean'])]
     for row in setup if '100k' in row['case']]))
parts.append('''<h2>Renderowanie i roślinność</h2><p>Licznik widoczności i odrzuceń sumuje przebiegi renderera; może przekraczać liczbę unikalnych obiektów. Roślinność to cztery proste trójkąty na instancję, jeden GeometrySwarm pod właścicielem ECS, bez cieni, wiatru, tekstur i alpha overdraw. Widoczna jest część pola w granicach kamery, nie cały milion. GPU ma opóźnione znaczniki czasu. 1000 / wall_frame_ms oznacza przepustowość poza ekranem, nie FPS prezentowane graczowi.</p>''')
parts.append(table(['Scena', 'Średnia klatka ms', 'P99 ms', 'GPU średnia ms', 'Widoczne / przebiegi', 'Odrzucone / przebiegi', 'Drawy', 'Upload instancji / klatka'],
    [[row['case'], fmt(row['after']['wall_frame_ms']['mean']), fmt(row['after']['wall_frame_ms']['p99']), fmt(row['after']['gpu_delayed_ms']['mean']),
      round(row['after']['visible_meshes']['mean']), round(row['after']['culled_instances']['mean']), round(row['after']['draws']['mean']), round(row['after']['instance_upload_bytes']['mean'])] for row in render]))
parts.append('''<h2>Streaming 100 tys. obiektów</h2><p>Trzy uruchomienia × trzy pełne cykle, 100002 encje po załadowaniu, zero po usunięciu. Potwierdza to sprzątanie encji, nie brak wszystkich możliwych wycieków pamięci. Scena statyczna bez renderera i fizyki. Limit 256 operacji na Update, miękki budżet 2 ms, porcja bulk do 32 encji. Mierzone są zarówno Pump, jak i cały Runtime.Update. Dekodowanie, walidacja i zwalnianie dokumentu odbywają się w tle. Aktywacja jest stopniowa. Między Update jest Sleep(1 ms); test nie symuluje rzeczywistego harmonogramu 60 Hz ani pracy GPU. Przy 60 Hz liczba klatek tworzenia całej sceny oznacza znacznie dłuższy czas oczekiwania: dziel świat na mniejsze komórki, nie traktuj całych 100 tys. obiektów jako jednej porcji streamingu.</p>''')
parts.append(table(['Próba / faza', 'Klatki / 3 cykle', 'Update średnia', 'Update P99', 'Update max', 'Streamer P99', 'Streamer max', 'Klatki stream >2 ms'],
    [[f"{row['rep']} / {row['phase']}", row['frames'], fmt(row['update_ms']['mean']), fmt(row['update_ms']['p99']), fmt(row['update_ms']['max']), fmt(row['stream_ms']['p99']), fmt(row['stream_ms']['max']), row['frames_over_2ms_stream']] for row in stream]))
parts.append('''<h2>Co zmieniono i co pozostaje</h2><ul>
<li>MeshPipelineVisibility: właściwa dolna granica clip-space i normalizacja małych płaszczyzn reverse-Z; obiekty za farClip są odrzucane.</li>
<li>ScenePrefab: liniowe dodawanie automatycznych stableId, z zachowaniem poprawności zatrzymanych mutable pointers.</li>
<li>SceneRuntime / SceneState: trwały cache interpolacji; aktualizacja zmienionych transformacji, przebudowa po zmianach topologii.</li>
<li>SceneStreamingService / SceneLoadedContent: istniejący worker zasobów oraz przygotowanie w tle, bulk ECS, bounded tworzenie i usuwanie, referencje między porcjami, anulowanie, diagnostyka i właściciel.</li>
<li>SceneContentInstanceService: StreamFocus korzysta z async ścieżki; indeks właścicieli usuwa kwadratowe wyszukiwanie.</li>
<li>GeometrySwarmVisibilityClusters / RenderScene / MeshPassProcessor: konserwatywne grupy po 128 instancji, z kontrolą zgodności accepted IDs, shear, ruchu kamery i wzrostu. SceneMeshSubmitter pomija transparent pass dla potwierdzonych opaque slots.</li>
<li>GameProjectRuntime: runtime respektuje lightingPath z ustawień projektu. Benchmark posiada headless CLI i mierzy rzeczywisty renderer.</li>
</ul><p class="limit">Następne wąskie gardła: koszt przygotowania renderowania dużej liczby instancji na CPU; globalne przetwarzanie ECS i hierarchii przy zmianach strukturalnych; aktywna gęsta fizyka; ograniczony budżet lokalnych świateł ForwardPlus; brak dowodu na kompletne HLOD, streaming GPU i produkcyjną roślinność. Miękkie 2 ms nie gwarantuje 2 ms pełnego Update. Nie wykonano porównania FPS z gotowymi grami na identycznym materiale artystycznym.</p>
<h2>Weryfikacja</h2><p>Release build silnika, gry i edytora. Pełne testy silnika i renderera oraz testy streamingu, referencji, anulowania, własności i pakietu bez luźnych źródeł. Renderer zgłasza osobny pominięty test WebGPU fallback; nie jest to pomiar WebGPU. Pełny game-core cooker suite zatrzymał się na braku shaderc w tej konfiguracji; nie jest zaliczony. Surowe pomiary znajdują się w raw/, statusy procesów w runs.json. Analizator sprawdza brak dropped/missing_resources, odrzucenie far/side oraz liczby encji po dziewięciu cyklach.</p>
<p><a href="comparison.csv">Porównanie CSV</a> · <a href="comparison.json">Statystyki JSON</a> · <a href="../../OpenWorldPerf_20260930/Results/Raport.html">Raport bazowy z szerszym zestawem scen</a></p></html>''')
(directory/'Raport_po_poprawkach.html').write_text(''.join(parts), encoding='utf-8')
print(json.dumps({'comparison_rows': len(comparison), 'render_scenes': len(render), 'stream_phases': len(stream)}, indent=2))
