"""kb_cli mcp: a capture comes back as an image block under its text summary; --images off keeps the text alone."""
from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def png_size(data: bytes):
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'the image block is not a PNG'
    return struct.unpack('>II', data[16:24])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cli', type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve(strict=True)
    assert (cli.parent / 'kb_game.exe').is_file(), 'kb_game must be built beside kb_cli'

    with tempfile.TemporaryDirectory(prefix='Capture Mcp ') as scratch:
        scratch = Path(scratch)
        subprocess.run([str(cli), 'new', '--parent', str(scratch), '--name', 'Probe', '--template', 'Basic'], check=True,
                       capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        project = scratch / 'Probe'

        def call(arguments, images='on'):
            requests = [
                {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {}},
                {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call', 'params': {'name': 'kb_cli', 'arguments': arguments}},
            ]
            result = subprocess.run([str(cli), 'mcp', '--project', str(project), '--images', images],
                                    input='\n'.join(json.dumps(item) for item in requests) + '\n', capture_output=True,
                                    encoding='utf-8', timeout=300, creationflags=subprocess.CREATE_NO_WINDOW)
            assert result.returncode == 0, result.stderr[-800:]
            return json.loads(result.stdout.strip().splitlines()[-1])['result']

        shot = call({'args': ['capture', '--frames', '5', '--view', 'overview']})
        types = [block['type'] for block in shot['content']]
        assert not shot.get('isError') and types == ['image', 'text'], (types, shot['content'][-1].get('text'))
        image = shot['content'][0]
        assert image['mimeType'] == 'image/png'
        width, height = png_size(base64.b64decode(image['data']))
        assert width == 640 and 300 <= height <= 400, (width, height)
        text = shot['content'][1]['text']
        assert 'captured frame 5 to Saved/Agent/Captures/capture-1.png' in text and 'dominant:' in text and 'mean #' in text, text
        assert str(project) not in text, 'the answer names no folder of this machine'
        assert (project / 'Saved' / 'Agent' / 'Captures' / 'capture-1.png').is_file()

        flat = call({'args': ['capture', '--frames', '5']}, images='off')
        assert [block['type'] for block in flat['content']] == ['text'] and 'dominant:' in flat['content'][0]['text']

        two = call({'commands': [['capture', '--frames', '5'], ['capture', '--frames', '5', '--view', 'top']]})
        assert [block['type'] for block in two['content']] == ['image', 'image', 'text'], two['content'][-1].get('text')

        wrong = call({'args': ['capture', '--frames', '5', '--camera', 'Nope']})
        assert wrong['isError'] and "no camera named 'Nope'" in wrong['content'][-1]['text'], wrong['content'][-1]['text']
        print('capture over mcp ok: image block', width, 'x', height)


main()
