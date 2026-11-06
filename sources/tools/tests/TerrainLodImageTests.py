"""A hilly terrain seen from the ground, rendered with a strict and with the default screen error of its chunk level of
detail: the pictures must agree, so no crack opens between chunks drawn at different levels. A coarse error shows the
levels at work and still leaves no hole."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

SCRIPTS = Path(__file__).resolve().parents[3] / 'scripts'
sys.path.insert(0, str(SCRIPTS))
from compare_images import compare  # noqa: E402
from png_readback import read_png  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--keep', type=Path, help='copy the pictures here')
    args = parser.parse_args()
    cli = args.cli.resolve(strict=True)

    def run(*words, expect=0, timeout=600):
        result = subprocess.run([str(cli), *map(str, words)], capture_output=True, encoding='utf-8',
                                timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
        assert result.returncode == expect, (words, result.returncode, result.stdout[-400:], result.stderr[-1500:])
        return result.stdout

    with tempfile.TemporaryDirectory(prefix='Terrain Lod ') as scratch:
        scratch = Path(scratch)
        run('new', '--parent', scratch, '--name', 'Ground', '--template', 'Basic')
        project = scratch / 'Ground'
        for floor in ('Floor', 'Ground', 'Plane'):
            result = subprocess.run([str(cli), 'entity', 'remove', '--project', str(project), '--name', floor],
                                    capture_output=True, encoding='utf-8')
            if result.returncode == 0:
                break
        run('terrain', 'new', '--project', project, '--name', 'Hills', '--resolution', 513, '--size', 512,
            '--preset', 'hills', '--seed', 5)
        terrain = '/Game/Terrains/Hills.kbterrain'
        sample = run('terrain', 'sample', '--project', project, '--terrain', terrain, '--x', -230, '--z', 0)
        ground = float(re.search(r'^height (\S+)$', sample, re.MULTILINE).group(1))
        run('entity', 'set', '--project', project, '--name', 'Main Camera', '--position', f'-230,{ground + 6},0',
            '--look-at', '0,0,0')

        pictures = {}
        for name, error in (('fine', 0.25), ('default', 2), ('coarse', 16)):
            run('terrain', 'settings', '--project', project, '--terrain', terrain, '--lod-pixel-error', error)
            picture = scratch / f'{name}.png'
            run('capture', '--project', project, '--out', picture, '--frames', '30', '--width', '960', '--height', '540')
            pictures[name] = read_png(picture)
            if args.keep:
                args.keep.mkdir(parents=True, exist_ok=True)
                (args.keep / picture.name).write_bytes(picture.read_bytes())

        default = compare(pictures['fine'], pictures['default'], pixel_tolerance=24)
        coarse = compare(pictures['fine'], pictures['coarse'], pixel_tolerance=24)
        print('default error against fine:', default)
        print('coarse error against fine:', coarse)
        # A crack shows the sky through the ground; whatever the level, the pictures stay alike.
        assert default['mean'] <= 2.5 and default['differing'] <= 0.04, default
        assert coarse['mean'] <= 6.0 and coarse['differing'] <= 0.12, coarse
        assert default['mean'] > 0.0 or coarse['mean'] > 0.0, 'the levels changed nothing in the picture'


main()
