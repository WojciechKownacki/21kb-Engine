"""A kb_cli process maps the plugin copies an earlier process made instead of copying every plugin again."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='PluginCopyReuse ') as scratch:
        scratch = Path(scratch)
        # The copies go to the temporary folder, so this run owns it: nothing else writes there.
        temp = scratch / 'temp'
        temp.mkdir()
        environment = dict(os.environ, TEMP=str(temp), TMP=str(temp))

        def run(*words):
            result = subprocess.run([str(cli), *map(str, words)], capture_output=True, encoding='utf-8',
                                    timeout=60, env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
            assert result.returncode == 0, (words, result.returncode, result.stdout[:300], result.stderr[:300])

        def copies():
            return {path.relative_to(temp).as_posix(): (path.stat().st_size, path.stat().st_mtime_ns)
                    for path in temp.rglob('*') if path.is_file()}

        run('new', '--parent', scratch, '--name', 'Reuse', '--template', 'Empty')
        project = scratch / 'Reuse'
        run('entity', 'add', '--project', project, '--name', 'Probe')
        first = copies()
        assert first, 'a project with plugins must leave copies of them in the temporary folder'
        for _ in range(2):
            run('entity', 'show', '--project', project, '--name', 'Probe')
            assert copies() == first, ('a later process copied or removed plugin copies', sorted(first), sorted(copies()))
        print('plugin copies reused:', len(first), 'file(s)')


main()
