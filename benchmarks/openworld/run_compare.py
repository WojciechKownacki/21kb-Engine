"""Run comparable Windows headless measurements sequentially and preserve baselines."""
import argparse, csv, json, subprocess, time
from pathlib import Path
from benchmark_validation import validate_camera_path
p=argparse.ArgumentParser()
p.add_argument('--build',type=Path,required=True)
p.add_argument('--project',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--suite',choices=['compare','cpu','capacity','foliage','stream','render','render-stream','production'],default='compare')
p.add_argument('--repetition',type=int,choices=[1,2,3],help='Repeat just one measurement series')
p.add_argument('--fixed-step',action='store_true',help='Identical 1/60 s simulation per headless profiled frame')
a=p.parse_args()
a.build=a.build.resolve(); a.project=a.project.resolve(); a.output=a.output.resolve()
binary=a.build/'bin'
a.output.mkdir(parents=True,exist_ok=True)
raw=a.output/'Results/raw'; raw.mkdir(parents=True,exist_ok=True)
logs=a.output/'Results/logs'; logs.mkdir(parents=True,exist_ok=True)
events=json.loads((a.output/'Results/runs.json').read_text()) if a.suite!='compare' and (a.output/'Results/runs.json').exists() else []
def run(name,args,timeout=300):
    print(name,flush=True)
    start=time.perf_counter()
    with (logs/(name+'.log')).open('w') as f:
        result=subprocess.run([str(x) for x in args],cwd=binary,stdout=f,stderr=subprocess.STDOUT,
            timeout=timeout,creationflags=subprocess.CREATE_NO_WINDOW)
    events[:]=[event for event in events if event['name']!=name]
    events.append({'name':name,'exit':result.returncode,'seconds':time.perf_counter()-start})
    (a.output/'Results/runs.json').write_text(json.dumps(events,indent=2))
    if result.returncode: raise RuntimeError(f'{name} failed; see {logs}')
for rep in ([a.repetition] if a.repetition else range(1,4)):
    for case in (['colliders_100k'] if a.suite=='capacity' else
                 ['static_100k','physics_idle_world_100k','physics_4096','dense_4096','batch_dirty_100k'] if a.suite in ['compare','cpu'] else []):
        run(f'{case}_{rep}',[binary/'kb_openworld_perf.exe','cpu',a.output,case,rep])
        rows=list(csv.DictReader((raw/f'{case}_cpu_{rep}.csv').open()))
        if len(rows)!=360:
            raise RuntimeError(f'{case}_{rep}: incomplete CPU capture')
        if case in ('physics_4096','dense_4096'):
            if any(int(r['awake_bodies'])!=4096 or int(r['fixed_steps'])!=1 for r in rows):
                raise RuntimeError(f'{case}_{rep}: active physics workload changed')
            if 'collision_events' not in rows[0] or sum(int(r['collision_events']) for r in rows)==0:
                raise RuntimeError(f'{case}_{rep}: collision events were not consumed')
        if case=='colliders_100k' and any(int(r['fixed_steps'])!=1 or int(r['hits'])!=1000 for r in rows):
            raise RuntimeError(f'{case}_{rep}: resident collider/raycast workload changed')

    render_cases = ([] if a.suite in ['stream','cpu','capacity'] else ['foliage_100k','foliage_1m'] if a.suite=='foliage'
        else ['city_dense_50k','geometry_10k','lights_512','shadow_50k','mixed_stream_10k'] if a.suite=='production'
        else ['world_100k','city_dense_50k','culling_far_10k','culling_side_10k'])
    for case in render_cases:
        name=f'{case}_{"fixed_" if a.fixed_step else ""}gpu_{rep}'
        profile=raw/(name+'.csv')
        frames=3420 if case=='mixed_stream_10k' else 600
        run(name,[binary/'kb_game.exe',f'--project={a.project}',f'--scene=/Game/Scenes/{case}.21kbscene',
            '--headless','--uncapped','--width=1920','--height=1080',f'--frames={frames}',
            '--profile-lighting=ForwardPlus',f'--profile-file={profile}']+(['--profile-fixed-step'] if a.fixed_step else []),600)
        rows=list(csv.DictReader(profile.open()))
        if len(rows)!=frames or any(float(r['dropped']) or float(r['missing_resources']) for r in rows):
            raise RuntimeError(f'{name}: incomplete or invalid submissions')
        if a.fixed_step and any(int(r['fixed_steps'])!=1 for r in rows):
            raise RuntimeError(f'{name}: simulation work was not constant')
        validate_camera_path(rows, case)
        if case=='mixed_stream_10k':
            fixture=json.loads((a.project/'Benchmarks/mixed_manifest.json').read_text())
            base=fixture['base_entities']; full=base+fixture['streamed_entities']
            completed=unloaded=0; was_full=False; awaiting_unload=False
            for r in rows:
                count=int(r['scene_entities'])
                if not base<=count<=full or int(r['streaming_operations'])>256:
                    raise RuntimeError(f'{name}: streaming lost its entity/operation bounds')
                if count==full and not was_full:
                    completed+=1; awaiting_unload=True
                if count==base and awaiting_unload:
                    unloaded+=1; awaiting_unload=False
                was_full=count==full
            if completed!=3 or unloaded!=3 or int(rows[-1]['scene_entities'])!=base:
                raise RuntimeError(f'{name}: expected 3 complete load/unload cycles, got {completed}/{unloaded}')
            print(f'{name}: 3 verified streaming cycles, {base}->{full}->{base} entities',flush=True)
    if a.suite in ['compare','stream','render-stream']:
        run(f'stream_100k_{rep}',[binary/'kb_openworld_perf.exe','stream',a.output,'static_100k',rep],600)
print('All comparison runs completed',flush=True)
