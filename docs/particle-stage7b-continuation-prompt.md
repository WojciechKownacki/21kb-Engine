# /goal

Kontynuuj produkcyjną implementację **21kb Particle System** w `H:\23kb\21kb-Engine` od aktualnego stanu zapisanego w `docs/particle-system-implementation-ledger.md`. To jest zadanie implementacyjne, nie audyt. Kanoniczną specyfikacją pozostaje `docs/particli-kanku-audit-plan.md`; obowiązują wszystkie niezmienne reguły z `docs/particli-sol-xhigh-autonomous-prompt.md` (read-only `E:\VerthEngineProd`, zakaz nazw referencyjnych, branch `1.0`, zakaz pełnych buildów jako domyślnej weryfikacji, ledger jako źródło stanu). Nie czytaj poniższego jako zamiennika tamtego kontraktu — to jego kontynuacja.

## Stan wejścia (zweryfikuj, nie zakładaj)

1. Przeczytaj cały ledger, `git status`, `git log --oneline -5` i diff ostatniego commita `2e588553` („Add explicit Sub Emitter target selection and default ColorOverLife gradient").
2. Etapy 0–6 i 7A mają status `accepted`. Etap 7B jest `in_progress` jako zwalidowany snapshot sesji.
3. Commit `2e588553` implementuje naprawę obu otwartych defektów 7B wypisanych w ledgerze (domyślny gradient `ColorOverLife`, jawny wybór targetu `SubEmitter`, kontrakt dodania wszystkich dziewięciu modułów z testami), ale **został zacommitowany bez buildu i bez testów** — ledger tego nie potwierdza.

## Pakiet 0 — domknięcie naprawy 7B (warunek wstępny dalszej pracy)

- Zbuduj najmniejsze właściwe targety obejmujące zmianę (testy edytora particle oraz `kb_editor`, bo zmiana dotyka routingu wskaźnika hosta) i uruchom skupioną macierz CTest z ledgera (asset / CPU / editor core / authoring / headless).
- Jeżeli weryfikacja obali naprawę: napraw przyczynę, nie osłabiaj testów, i powtórz macierz.
- Po przejściu zaktualizuj ledger: zamknij otwarty kontrakt „all-nine-module add", dopisz komendy, exit codes i wyniki do sekcji dowodów. Dopiero wtedy etap 7B może iść dalej.

## Pakiety 1+ — brama Stage 7B

Realizuj następującą bramę z ledgera: *typed module and output-property authoring over the accepted emitter workspace, including explicit capability states before curves, gradients, recipes, and dependency navigation*. Zakres rozumiej przez audyt (sekcja 8 — properties, output, diagnostics) i stan zaakceptowanych etapów:

- typowane kontrolki właściwości modułów i outputów nad zaakceptowanym workspace emiterów, z pełnymi stanami capability (explicit unsupported, nigdy ciche ukrywanie ani downgrade);
- każda właściwość widoczna w authoringu ma runtime executor/output — bez martwych pól UI;
- pełne ścieżki błędów, undo/redo jako pojedyncze transakcje historii dokumentu, rollback przy nieudanej publikacji preview;
- focused testy komend, walidacji i layoutu plus najmniejsza regresja hosta; zmiany wizualne wymagają inspekcji DPI/rozdzielczości jak w 7A.

Krzywe, gradienty edytowalne, recipes i nawigacja po zależnościach to kolejne bramy 7B — nie rozpoczynaj ich przed przyjęciem poprzedniej i nie rozmywaj ich w jeden diff.

## Warunek zakończenia celu

Cel jest ukończony, gdy jednocześnie:

- naprawa z `2e588553` ma w ledgerze niezależnie potwierdzone dowody (build + macierz focused CTest, exit 0);
- brama Stage 7B ma status `accepted` w ledgerze wraz z kompletem dowodów, albo — jeśli okaze się zbyt duża na jedną sesję — jest podzielona na zaakceptowane checkpointy z jawnie wypisanym następnym gatem;
- finalny diff nie zawiera stubów, TODO, martwego API, cichych fallbacków, zakazanych tokenów ani zmian poza zakresem; `git diff --check` przechodzi;
- każde twierdzenie w raporcie ma dowód z komendy i exit code'u zapisany w ledgerze.

Limit czasu ani długość kontekstu nie są warunkiem stopu — zrób checkpoint w ledgerze i podejmij następny krok. Po kompakcji odtwarzaj stan z ledgera, audytu i `git status`, nie z pamięci.

Raport końcowy: maksymalnie sześć krótkich punktów — wykonane zmiany, wyniki weryfikacji z exit codes, pozostałe ryzyko lub blocker.
