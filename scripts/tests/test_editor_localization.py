from __future__ import annotations

import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

import editor_localization  # noqa: E402


SOURCE = (
    'auto a = EditorText("panel.close", "Close");\n'
    'auto b = EditorLocalization::Format("panel.saved", "Saved {scene}", {});\n'
    'auto c = EditorLocalization::Plural("panel.files", n, "{count} file", "{count} files");\n'
)

GOOD = (
    'schema 1\nfallback "en"\nlanguage "pl"\n'
    'text "panel.close" "Zamknij"\n'
    'text "panel.saved" "Zapisano {scene}"\n'
    'plural "panel.files" "one" "{count} plik"\n'
    'plural "panel.files" "few" "{count} pliki"\n'
    'plural "panel.files" "many" "{count} plików"\n'
    'plural "panel.files" "other" "{count} pliku"\n'
)


class EditorLocalizationGateTests(unittest.TestCase):
    """check() is green for a whole catalog and red for each kind of damage."""

    def run_check(self, catalog: str) -> int:
        saved = editor_localization.ROOT, editor_localization.SOURCES, editor_localization.CATALOGS, \
            editor_localization.LANGUAGES
        with tempfile.TemporaryDirectory() as temp:
            sources = os.path.join(temp, 'src')
            catalogs = os.path.join(temp, 'Localization')
            os.makedirs(sources)
            os.makedirs(catalogs)
            Path(sources, 'Panel.cpp').write_text(SOURCE, encoding='utf-8')
            Path(catalogs, 'pl.kbloc').write_text(catalog, encoding='utf-8')
            editor_localization.ROOT, editor_localization.SOURCES = temp, sources
            editor_localization.CATALOGS, editor_localization.LANGUAGES = catalogs, ['pl']
            try:
                with redirect_stdout(io.StringIO()):
                    return editor_localization.check()
            finally:
                (editor_localization.ROOT, editor_localization.SOURCES, editor_localization.CATALOGS,
                 editor_localization.LANGUAGES) = saved

    def test_whole_catalog_passes(self) -> None:
        self.assertEqual(self.run_check(GOOD), 0)

    def test_missing_text_fails(self) -> None:
        self.assertEqual(self.run_check(GOOD.replace('text "panel.close" "Zamknij"\n', '')), 1)

    def test_changed_placeholder_fails(self) -> None:
        self.assertEqual(self.run_check(GOOD.replace('Zapisano {scene}', 'Zapisano {map}')), 1)

    def test_text_the_editor_dropped_fails(self) -> None:
        self.assertEqual(self.run_check(GOOD + 'text "panel.old" "Stary"\n'), 1)

    def test_missing_other_plural_fails(self) -> None:
        self.assertEqual(self.run_check(GOOD.replace('plural "panel.files" "other" "{count} pliku"\n', '')), 1)

    def test_shipped_catalogs_cover_every_editor_text(self) -> None:
        with redirect_stdout(io.StringIO()) as output:
            result = editor_localization.check()
        self.assertEqual(result, 0, output.getvalue()[-4000:])


if __name__ == '__main__':
    unittest.main()
