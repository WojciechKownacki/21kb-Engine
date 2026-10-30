"""Round-trip real editor layouts through the public headless CLI."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('editor', 'cli', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='Workspace', dir=args.test_root)).resolve()
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

    run(args.editor, '--selftest-scenario', Path(__file__).with_name('HeadlessWorkspaceScenario.json').resolve(),
        '--selftest-root', root, '--selftest-task', 'Author')
    project = root / 'Author/workspace'
    descriptor = next(project.glob('*.21kbproject'))

    def cli(*words, expected=0):
        output = run(args.cli, 'layout', *words, '--project', project, '--json', expected=expected)
        return json.loads(output)

    cli('save', '--name', 'CLI')
    assert (project / 'Config/Layouts/CLI.kblayout').read_bytes() == (project / 'Config/Layouts/arranged.kblayout').read_bytes()

    def inspect(name, floating):
        steps = [dict(op='workspace_restore_current'),
                 dict(op='assert_workspace', visible=['Hierarchy', 'Scene View', 'Inspector', 'Console'],
                      floating=['Console'] if floating else [],
                      hidden=['Project Files'] if floating else [], fits_width=1024, fits_height=600),
                 dict(op='workspace_save', slot='Observed'), dict(op='assert_no_errors')]
        if not floating:
            steps[1]['visible'].append('Project Files')
        scenario = root / (name + '.json')
        scenario.write_text(json.dumps(dict(schema='21kb.editor-automation/v1',
            initial_project=str(descriptor), steps=steps)), encoding='utf-8')
        run(args.editor, '--selftest-scenario', scenario, '--selftest-root', root, '--selftest-task', name)
        observed = (root / name / 'workspace/Config/Layouts/Observed.kblayout').read_text()
        assert ('Panel.6=Floating 1' in observed) == floating, observed

    cli('reset')
    inspect('Reset', False)
    cli('restore', '--name', 'CLI')
    inspect('Restored', True)
    settings = project / 'Config/EditorSettings.ini'
    before = hashlib.sha256(settings.read_bytes()).hexdigest()
    cli('restore', '--name', 'Missing', expected=1)
    cli('save', '--name', '../Outside', expected=1)
    assert hashlib.sha256(settings.read_bytes()).hexdigest() == before
    cli('list')
    (root / 'result.json').write_text(json.dumps(dict(result='PASS', cliSave=True,
        cliReset=True, cliRestore=True, guiRoundTrip=True, invalidNamesPreserveWorkspace=True), indent=2))
    print(root)


if __name__ == '__main__':
    main()
