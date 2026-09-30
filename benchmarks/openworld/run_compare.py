"""Run comparable Windows headless measurements sequentially and preserve baselines."""
import argparse, csv, json, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--build',type=Path,required=True)
p.add_argument('--project',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--suite',choices=['compare','foliage','stream','render','render-stream'],default='compare')
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
for rep in range(1,4):
    for case in (['static_100k','physics_idle_world_100k','physics_4096','dense_4096','batch_dirty_100k'] if a.suite=='compare' else []):
        run(f'{case}_{rep}',[binary/'kb_openworld_perf.exe','cpu',a.output,case,rep])
    for case in ([] if a.suite=='stream' else ['foliage_100k','foliage_1m'] if a.suite=='foliage' else ['world_100k','city_dense_50k','culling_far_10k','culling_side_10k']):
        run(f'{case}_gpu_{rep}',[binary/'kb_game.exe',f'--project={a.project}',f'--scene=/Game/Scenes/{case}.21kbscene',
            '--headless','--uncapped','--width=1920','--height=1080','--frames=600',
            '--profile-lighting=ForwardPlus',f'--profile-file={raw/(case+"_gpu_"+str(rep)+".csv")}'])
    if a.suite in ['compare','stream','render-stream']:
        run(f'stream_100k_{rep}',[binary/'kb_openworld_perf.exe','stream',a.output,'static_100k',rep],600)
print('All comparison runs completed',flush=True)
