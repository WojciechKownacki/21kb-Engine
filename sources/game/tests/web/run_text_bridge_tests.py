"""Exercise production browser text handlers without accessing the system clipboard.

Keyboard and composition are delivered by DevTools. Clipboard events use isolated
DataTransfer objects; native callbacks are recorded, not a packaged engine runtime.
"""
import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import time
import urllib.request

from run_input_tests import DevTools


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[2] / 'src/web/WebGameHost.cpp'
    source_text = source.read_text(encoding='utf-8')
    functions = []
    for name, parameters in [('RegisterBrowserTextInput', 'host'), ('SetBrowserTextInputActive', 'active'),
                             ('DestroyBrowserTextInput', '')]:
        match = re.search(r'EM_JS\(void, ' + name + r', \([^)]*\), \{(.*?)^\}\);', source_text, re.S | re.M)
        if not match:
            raise RuntimeError('Production text handler not found: ' + name)
        functions.append('function ' + name + '(' + parameters + ') {' + match[1] + '}')
    harness = '''
const Module = {}, commits = [], replacements = [], freed = [];
const inputEvents = [];
for (const kind of ['input', 'compositionstart', 'compositionupdate', 'compositionend'])
    document.addEventListener(kind, event => inputEvents.push({type:event.type, trusted:event.isTrusted}), true);
let selected = '', allocation = 10;
const memory = new Map();
function _kb_web_append_committed_text(host, scalar) { commits.push(String.fromCodePoint(scalar)); return 1; }
function _kb_web_commit_text(host, pointer, length) { commits.push(memory.get(pointer)); return 1; }
function _kb_web_selected_text() { memory.set(1, selected); return selected ? 1 : 0; }
function _kb_web_replace_text(host, pointer, length) { replacements.push(pointer ? memory.get(pointer) : ''); return 1; }
function UTF8ToString(pointer) { return memory.get(pointer); }
function lengthBytesUTF8(text) { return new TextEncoder().encode(text).length; }
function stringToNewUTF8(text) { memory.set(++allocation, text); return allocation; }
function _free(pointer) { freed.push(pointer); memory.delete(pointer); }
function clipboard(kind, text = '', denied = false) {
    const data = new DataTransfer(); data.setData('text/plain', text);
    const event = new ClipboardEvent(kind, {clipboardData:data, bubbles:true, cancelable:true});
    if (denied) Object.defineProperty(event, 'clipboardData', {value:{setData(){throw Error('denied');}}});
    Module.kbTextInput.control.dispatchEvent(event);
    return {text:data.getData('text/plain'), prevented:event.defaultPrevented};
}
function require(condition, message) { if (!condition) throw Error(message); }
'''
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    client = None
    process = None
    report = dict(result='FAIL', scope=__doc__, checks=[])
    try:
        with (args.output / 'browser.log').open('wb') as log:
            process = subprocess.Popen([str(args.browser), '--headless=new', '--no-first-run',
                '--disable-extensions', '--disable-background-networking', '--disable-component-update',
                '--no-default-browser-check', f'--remote-debugging-port={port}',
                f'--user-data-dir={args.output.resolve() / "profile"}', 'about:blank'],
                stdout=log, stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            until = time.monotonic() + 30
            while client is None:
                if process.poll() is not None or time.monotonic() > until:
                    raise RuntimeError('Owned browser did not start')
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=2) as response:
                        targets = json.load(response)
                    page = next(x for x in targets if x['type'] == 'page')
                    client = DevTools(page['webSocketDebuggerUrl'])
                except (OSError, StopIteration):
                    time.sleep(.1)
            client.call('Page.enable')
            client.call('Page.bringToFront')
            client.evaluate(harness + '\n'.join(functions) + '\nRegisterBrowserTextInput(7); SetBrowserTextInputActive(1);')
            client.call('Input.insertText', text='Aż😀')
            assert client.evaluate("commits.join('')") == 'Aż😀'
            client.call('Input.imeSetComposition', text='仮', selectionStart=1, selectionEnd=1)
            assert client.evaluate("commits.join('')") == 'Aż😀'
            client.call('Input.insertText', text='確定')
            assert client.evaluate("commits.join('')") == 'Aż😀確定'
            client.call('Input.insertText', text='確定')
            assert client.evaluate("commits.join('')") == 'Aż😀確定確定'
            report['nativeInputEvents'] = client.evaluate('inputEvents')
            if not all(event['trusted'] for event in report['nativeInputEvents'] if event['type'] != 'compositionend'):
                raise RuntimeError('Typing and composition start/update must use browser input delivery')
            # DevTools can end composition with isTrusted=false; retain the actual values.
            report['checks'].append('Trusted typing and native composition commit exactly once; repeated text survives')
            client.evaluate("""
selected = 'ż😀\u0301';
const beforePasteFreed = freed.length;
require(clipboard('copy').text === selected && replacements.length === 0, 'Unicode copy');
require(clipboard('cut', '', true).prevented && replacements.length === 0, 'Denied cut must not delete');
require(clipboard('cut').text === selected && replacements.length === 1 && replacements[0] === '', 'Cut after copy');
require(clipboard('paste', 'line 1\\n😀').prevented && replacements[1] === 'line 1\\n😀', 'Atomic multiline paste');
require(freed.length === beforePasteFreed + 1, 'Paste allocation must be freed');
clipboard('paste', 'x'.repeat(16385));
clipboard('paste', '😀'.repeat(5000));
require(replacements.length === 2, 'Oversized paste must be refused');
selected = '';
require(clipboard('copy').prevented && clipboard('cut').text === '' && replacements.length === 2, 'Protected or empty selection');
""")
            report['checks'].append('Unicode copy/cut/paste, failed write, empty/protected selection, multiline, size bounds and cleanup')
            client.call('Input.imeSetComposition', text='private', selectionStart=7, selectionEnd=7)
            client.evaluate("""
require(clipboard('copy').prevented && clipboard('cut').text === '', 'Composition must not leak through clipboard');
clipboard('paste', 'ignored');
require(replacements.length === 2, 'Paste must not replace an in-flight composition');
SetBrowserTextInputActive(0); SetBrowserTextInputActive(1);
const before = commits.join('');
const control = Module.kbTextInput.control;
control.dispatchEvent(new CompositionEvent('compositionend', {data:'late'}));
control.dispatchEvent(new InputEvent('input', {inputType:'insertCompositionText', data:'late'}));
require(commits.join('') === before, 'Canceled composition must not reach a new field');
DestroyBrowserTextInput(); RegisterBrowserTextInput(7); SetBrowserTextInputActive(1);
control.dispatchEvent(new InputEvent('input', {inputType:'insertText', data:'retired'}));
require(commits.join('') === before, 'Retired field event target must reject delayed plain text');
SetBrowserTextInputActive(0);
clipboard('paste', 'inactive');
require(replacements.length === 2, 'Inactive editor must not accept paste');
SetBrowserTextInputActive(1);
const latestControl = Module.kbTextInput.control;
latestControl.dispatchEvent(new CompositionEvent('compositionstart'));
DestroyBrowserTextInput();
latestControl.dispatchEvent(new CompositionEvent('compositionend', {data:'stale'}));
require(commits.join('') === before && !Module.kbTextInput && !latestControl.isConnected, 'Destroyed editor cannot commit');
""")
            report['checks'].append('Composition confidentiality, focus cancellation, stale final events and destruction')
            report.update(result='PASS', browser=client.call('Browser.getVersion')['product'])
    finally:
        if client:
            try:
                client.call('Browser.close')
            except (OSError, RuntimeError):
                pass
            client.close()
        if process:
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)
        (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
