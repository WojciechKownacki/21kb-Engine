"""kb_cli build cooks, packs and stages a project into a runnable folder; kb_cli capture renders a frame of it headless."""
from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

PROVIDERS = ('kb_physics_jolt_plugin', 'kb_audio_miniaudio_plugin', 'kb_basic_lighting_plugin',
             'kb_21kb_particle_plugin', 'kb_ai_plugin')


def png_size(path: Path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', f'{path} is not a PNG'
    return struct.unpack('>II', data[16:24])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve(strict=True)

    def run(*words, expect=0, timeout=900):
        result = subprocess.run([str(cli), *map(str, words)], capture_output=True, encoding='utf-8',
                                timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
        assert result.returncode == expect, (words, result.returncode, result.stdout[-400:], result.stderr[-1500:])
        return result

    with tempfile.TemporaryDirectory(prefix='Build Capture ') as scratch:
        scratch = Path(scratch)
        run('new', '--parent', scratch, '--name', 'Probe', '--template', 'Basic')
        project = scratch / 'Probe'
        package = scratch / 'package'

        assert 'needs' in run('build', '--project', project, expect=1).stderr
        assert '--project' in run('capture', '--out', scratch / 'x.png', expect=1).stderr
        assert 'not both' in run('capture', '--project', project, '--build', package,
                                 '--out', scratch / 'x.png', expect=1).stderr

        run('build', '--project', project, '--out', package, '--pack-compression-level', '3')
        assert (package / 'Game.kbpack').stat().st_size > 0
        assert (package / 'kb_game.exe').is_file()
        for provider in PROVIDERS:
            assert (package / f'{provider}.dll').is_file(), provider
        assert (package / 'phonon.dll').is_file()
        assert 'not empty' in run('build', '--project', project, '--out', package, expect=1).stderr

        # The staged player starts on its own package, with no project beside it.
        player = subprocess.run([str(package / 'kb_game.exe'), '--headless', '--frames=3', '--width=320', '--height=180'],
                                cwd=package, capture_output=True, encoding='utf-8', timeout=120,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        assert player.returncode == 0 and 'rendered=3' in player.stdout and 'errors=0' in player.stdout, \
            (player.returncode, player.stdout, player.stderr[-800:])

        built = scratch / 'built.png'
        run('capture', '--build', package, '--out', built, '--frames', '5', '--width', '320', '--height', '180')
        assert png_size(built) == (320, 180)
        source = scratch / 'source.png'
        run('capture', '--project', project, '--out', source, '--frames', '5', '--width', '320', '--height', '180')
        assert png_size(source) == (320, 180)
        assert not list(scratch.glob('*.csv')), 'a capture must not leave its frame statistics behind'
        assert 'already exists' in run('capture', '--build', package, '--out', built, expect=1).stderr
        assert 'Game.kbpack' in run('capture', '--build', scratch, '--out', scratch / 'y.png', expect=1).stderr
        print('build and capture ok:', built.stat().st_size, 'and', source.stat().st_size, 'byte PNGs')


main()
