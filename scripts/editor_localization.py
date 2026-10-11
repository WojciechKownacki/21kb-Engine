"""The editor's text in 33 languages: collects every EditorText / EditorLocalization / ParticleText
call of the editor sources and checks the catalogs in sources/editor/content/EditorShell/Localization.

  python scripts/editor_localization.py extract   writes source.kbloc (every key with its English text)
  python scripts/editor_localization.py check     fails when a catalog misses a key, has a key the
                                                  editor no longer uses, or loses a {placeholder}

English is the source and has no catalog of its own; source.kbloc is what translators start from.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The editor and the particle editor (a plugin library the editor links), which installs the
# editor's catalogs as its text source (ParticleText).
SOURCES = [os.path.join(ROOT, 'sources', 'editor', 'src'),
           os.path.join(ROOT, 'sources', 'plugins', '21kb_particle', 'editor')]
CATALOGS = os.path.join(ROOT, 'sources', 'editor', 'content', 'EditorShell', 'Localization')
LANGUAGES = ['pl', 'de', 'fr', 'es', 'it', 'ru', 'ja', 'zh-Hans', 'ko', 'pt-PT', 'pt-BR', 'nl', 'cs', 'hu',
             'ro', 'uk', 'sv', 'tr', 'hi', 'el', 'da', 'nb', 'fi', 'sk', 'bg', 'sr', 'hr', 'ar', 'id', 'th',
             'vi', 'ms']

STRING = r'"((?:[^"\\]|\\.)*)"'
# An argument may be several adjacent literals the compiler joins: "part one " "part two".
LITERALS = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'

TEXT_CALL = re.compile(r'\b(?:EditorText|EditorLocalization::Text|EditorLocalization::Format|ParticleText|ParticleFormat)\s*\(\s*' + STRING +
                       r'\s*,\s*' + LITERALS, re.S)
PLURAL_CALL = re.compile(r'\bEditorLocalization::Plural\s*\(\s*' + STRING + r'\s*,[^,]+,\s*' + LITERALS + r'\s*,\s*' +
                         LITERALS, re.S)
PLACEHOLDER = re.compile(r'\{[A-Za-z_][A-Za-z0-9_]*\}')
KEY = re.compile(r'^[A-Za-z0-9._-]{1,256}$')


def unescape(text):
    """The C++ text of one literal or of adjacent literals ("a " "b" -> "a b")."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', text) if text.lstrip().startswith('"') else [text]
    joined = ''.join(parts)
    return bytes(joined, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')


def collect():
    """key -> {'text': english} or {'one': ..., 'other': ...}, plus the problems found."""
    messages = {}
    problems = []
    for directory, _, names in (entry for base in SOURCES for entry in os.walk(base)):
        for name in names:
            if not name.endswith(('.cpp', '.hpp', '.h')):
                continue
            path = os.path.join(directory, name)
            source = open(path, encoding='utf-8').read()
            found = [(m.group(1), {'text': unescape(m.group(2))}) for m in TEXT_CALL.finditer(source)]
            found += [(m.group(1), {'one': unescape(m.group(2)), 'other': unescape(m.group(3))})
                      for m in PLURAL_CALL.finditer(source)]
            for key, message in found:
                where = os.path.relpath(path, ROOT)
                if not KEY.match(key):
                    problems.append(f'{where}: key "{key}" may only use letters, digits, ".", "_" and "-"')
                elif key in messages and messages[key] != message:
                    problems.append(f'{where}: key "{key}" has two English texts: {messages[key]} / {message}')
                else:
                    messages[key] = message
    return messages, problems


def quote(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def read_catalog(path):
    """language -> key -> {'text': ...} or {category: ...}, read like LocalizationCatalogIO."""
    tokens = re.findall(r'"(?:[^"\\]|\\.)*"|\S+', open(path, encoding='utf-8').read())
    catalog = {}
    language = None
    index = 0
    unq = lambda t: t[1:-1].replace('\\"', '"').replace('\\\\', '\\')
    while index < len(tokens):
        token = tokens[index]
        if token == 'schema' or token == 'fallback':
            index += 2
        elif token == 'language':
            language = unq(tokens[index + 1])
            catalog[language] = {}
            index += 2
        elif token == 'text':
            catalog[language][unq(tokens[index + 1])] = {'text': unq(tokens[index + 2])}
            index += 3
        elif token == 'plural':
            catalog[language].setdefault(unq(tokens[index + 1]), {})[unq(tokens[index + 2])] = unq(tokens[index + 3])
            index += 4
        else:
            raise ValueError(f'{path}: unexpected "{token}"')
    return catalog


def extract():
    messages, problems = collect()
    for problem in problems:
        print('error:', problem)
    os.makedirs(CATALOGS, exist_ok=True)
    lines = ['schema 1', 'fallback "en"', 'language "en"']
    for key in sorted(messages):
        message = messages[key]
        if 'text' in message:
            lines.append(f'text {quote(key)} {quote(message["text"])}')
        else:
            lines.append(f'plural {quote(key)} "one" {quote(message["one"])}')
            lines.append(f'plural {quote(key)} "other" {quote(message["other"])}')
    open(os.path.join(CATALOGS, 'source.kbloc'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(f'{len(messages)} editor texts written to source.kbloc')
    return 1 if problems else 0


def check():
    messages, problems = collect()
    for language in LANGUAGES:
        path = os.path.join(CATALOGS, language + '.kbloc')
        if not os.path.exists(path):
            problems.append(f'{language}: catalog {os.path.relpath(path, ROOT)} is missing')
            continue
        entries = read_catalog(path).get(language)
        if entries is None:
            problems.append(f'{language}: the catalog has no "language \\"{language}\\"" section')
            continue
        for key, message in messages.items():
            translated = entries.get(key)
            if translated is None:
                problems.append(f'{language}: "{key}" is not translated')
                continue
            english = set(PLACEHOLDER.findall(' '.join(message.values())))
            for category, text in translated.items():
                if set(PLACEHOLDER.findall(text)) != english:
                    problems.append(f'{language}: "{key}" ({category}) changes the placeholders {sorted(english)}')
            if ('text' in message) != ('text' in translated):
                problems.append(f'{language}: "{key}" must be {"a text" if "text" in message else "a plural"}')
            elif 'other' in message and 'other' not in translated:
                problems.append(f'{language}: "{key}" has no "other" plural form')
        for key in entries:
            if key not in messages:
                problems.append(f'{language}: "{key}" is no longer used by the editor')
    for problem in problems:
        print('error:', problem)
    print(f'{len(messages)} editor texts, {len(LANGUAGES)} languages, {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    command = sys.argv[1] if len(sys.argv) > 1 else 'check'
    sys.exit(extract() if command == 'extract' else check())
