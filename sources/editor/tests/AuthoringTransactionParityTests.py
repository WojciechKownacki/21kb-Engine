"""Compare complete GUI/CLI documents and verify atomic dependent authoring."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cli', 'editor', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='AuthoringParity', dir=args.test_root)).resolve()
    history = []

    def run(exe, *tail, expected=0):
        command = list(map(str, [exe.resolve(), *tail]))
        result = subprocess.run(command, cwd=exe.resolve().parent, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=180)
        history.append(dict(command=command, exitCode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        (root / 'commands.json').write_text(json.dumps(history, indent=2), encoding='utf-8')
        assert result.returncode == expected, history[-1]
        return result.stdout

    run(args.cli, 'new', '--parent', root, '--name', 'Sample', '--template', 'Empty')
    project = root / 'Sample'
    commands = [
        'scene-new --name Parity --template Empty --default yes',
        'entity add --name Holder --position 1,2,3',
        'entity add --name Lamp --parent Holder',
        'component add --entity Holder/Lamp --type Light --set intensity=2.5',
        'entity add --name Camera --create Camera',
        'entity add --name Temporary',
        'entity remove --name Temporary',
        'material-new --name NewMaterial',
        'material-set --material /Game/Materials/NewMaterial.kbmat --set Roughness=0.25',
        'material-set --material /Game/Materials/NewMaterial.kbmat --set Metallic=0.5',
    ]
    batch = root / 'batch.txt'
    batch.write_text('\n'.join(commands) + '\n', encoding='utf-8')

    def fingerprint():
        excluded = {'Saved', 'Library', 'Intermediate', 'Build', 'Binaries'}
        return {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in project.rglob('*') if p.is_file()
                and not excluded.intersection(p.relative_to(project).parts)}

    before = fingerprint()
    dry = json.loads(run(args.cli, 'batch', '--project', project, '--file', batch, '--dry-run', '--json'))
    assert dry['ok'] and dry['rolledBack'] and not dry['applied'] and dry['changes']
    assert fingerprint() == before
    bad = root / 'bad.txt'
    bad.write_text(batch.read_text() + 'component add --entity Holder/Lamp --type Light\n', encoding='utf-8')
    failed = json.loads(run(args.cli, 'batch', '--project', project, '--file', bad, '--json', expected=1))
    assert failed['rolledBack'] and failed['failedOperation'] == len(commands) + 1
    assert fingerprint() == before
    applied = json.loads(run(args.cli, 'batch', '--project', project, '--file', batch, '--json'))
    assert applied['ok'] and applied['applied'] and not applied['rolledBack']
    steps = [
        dict(op='create_scene', name='Parity', template='Empty'),
        dict(op='create_entity', id='Holder', name='Holder'),
        dict(op='set_entity_position', entity='Holder', x=1, y=2, z=3),
        dict(op='create_entity', id='Lamp', name='Lamp'),
        dict(op='reparent_entity', entity='Lamp', parent='Holder'),
        dict(op='add_component', entity='Lamp', component='Light'),
        dict(op='component_inspector_field', entity='Lamp', component='Light', property='intensity', text='2.5'),
        dict(op='create_entity', id='Camera', name='Camera', preset='Camera'),
        dict(op='create_entity', id='Temporary', name='Temporary'),
        dict(op='delete_entity', entity='Temporary'),
        dict(op='create_asset', id='Material', type='material', folder='/Game/Materials'),
        dict(op='set_material', asset='Material', property='roughness', value=0.25),
        dict(op='set_material', asset='Material', property='metallic', value=0.5),
        dict(op='save_scene'),
        dict(op='open_scene', path='Assets/Scenes/Parity.21kbscene'),
        dict(op='assert_property', entity='Lamp', component='Light', property='intensity', value=2.5),
        dict(op='assert_no_errors'),
    ]
    scenario = root / 'scenario.json'
    scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1', steps=steps)), encoding='utf-8')
    run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', 'GUI')
    gui = root / 'GUI/workspace'

    def scene_payload(path):
        data = path.read_bytes()
        assert data[:8] == b'21KBSCN\0'
        # Only the independently created document GUID differs. Compare every
        # other serialized field, including stable node IDs and component data.
        guid_length = struct.unpack_from('<I', data, 12)[0]
        return data[:12] + data[16 + guid_length:]

    left = scene_payload(project / 'Assets/Scenes/Parity.21kbscene')
    right = scene_payload(gui / 'Assets/Scenes/Parity.21kbscene')
    assert left == right, ('Scene parity', len(left), len(right),
                           next((i for i, (a, b) in enumerate(zip(left, right)) if a != b), None))
    left_material = (project / 'Assets/Materials/NewMaterial.kbmat').read_bytes()
    right_material = (gui / 'Assets/Materials/NewMaterial.kbmat').read_bytes()
    assert left_material == right_material, 'Material parity'
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', operations=len(commands),
        rollback=True, dryRun=True, diff=True, fullSceneParity=True, fullMaterialParity=True), indent=2))
    print(root)


if __name__ == '__main__':
    main()
