"""Stage the pinned editor interpreter and stdlib without host Python dependencies."""
import argparse
from pathlib import Path
import shutil
import zipfile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();runtime=args.output/'Python';runtime.mkdir(parents=True,exist_ok=True)
    def copy(source,destination):
        if not destination.exists() or source.read_bytes()!=destination.read_bytes():shutil.copy2(source,destination)
    for source in [*args.sdk.glob('*.dll'),*args.sdk.glob('*.pyd'),*args.sdk.glob('DLLs/*.dll'),*args.sdk.glob('DLLs/*.pyd')]:
        copy(source,args.output/source.name if source.name=='python314.dll' else runtime/source.name)
    copy(args.sdk/'LICENSE.txt',runtime/'LICENSE.txt')
    archive=runtime/'python314.zip'
    if not archive.exists():
        with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as output:
            for source in sorted((args.sdk/'Lib').rglob('*')):
                relative=source.relative_to(args.sdk/'Lib')
                if source.is_file() and not any(p in ('site-packages','test','tests','__pycache__','idlelib','tkinter','turtledemo') for p in relative.parts):output.write(source,relative.as_posix())
    (runtime/'version.txt').write_text('CPython 3.14.10\n',encoding='utf-8')
if __name__=='__main__':main()
