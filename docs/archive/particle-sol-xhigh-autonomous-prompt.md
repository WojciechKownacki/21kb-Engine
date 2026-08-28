# /goal

Zaimplementuj produkcyjnie plugin **Particle** w `H:\23kb\21kb-Engine`, end to end, aż do pełnego spełnienia Definition of Done z kanonicznego audytu. To jest zadanie implementacyjne, nie kolejny audyt ani roadmapa. Pracuj autonomicznie do przejścia wszystkich etapów 0–11; nie zatrzymuj się po MVP, częściowym rendererze, samym edytorze ani raporcie postępu.

## Konfiguracja wykonania

- Model orkiestratora: `gpt-5.6-sol`, reasoning effort `xhigh` (w interfejsie: Extra High).
- Model implementera: `gpt-5.6-sol`, reasoning effort `xhigh`.
- Jeżeli środowisko udostępnia `reasoning.context`, użyj `all_turns`.
- Pracuj wyłącznie na branchu `1.0`. Na początku i przed finalnym odbiorem potwierdź `git branch --show-current`. Nie przełączaj gałęzi.
- Użyj dokładnie dwóch agentów: bieżącego agenta głównego jako orkiestratora oraz jednego stałego subagenta jako implementera. Nie twórz trzeciego agenta. Implementer nie może delegować ani tworzyć subagentów.
- Jeżeli nie można uruchomić drugiego agenta z wymaganym modelem i poziomem rozumowania, nie zastępuj tego cichą pracą jednoagentową; jest to rzeczywisty blocker wymagający zgłoszenia.

## Role

### Orkiestrator

Jesteś właścicielem celu, jakości, kolejności prac i kontekstu. Nie piszesz kodu produkcyjnego ani testów. Możesz wykonywać dowolne odczyty, niezależnie uruchamiać buildy/testy/benchmarki, analizować diff i aktualizować wyłącznie `docs/particle-implementation-ledger.md`. Twoje obowiązki:

1. Przeczytaj w całości, przed pierwszym zleceniem:
   - `AGENTS.md`;
   - `docs/particle-kanku-audit-plan.md`;
   - aktualny `git status`, branch, konfigurację buildów i tylko kod konieczny do potwierdzenia pierwszego etapu.
2. Traktuj audyt jako kanoniczną specyfikację: sekcje 5–10 definiują architekturę, asset, lifecycle, UI i manifest plików; sekcja 11 kolejność i bramy; sekcja 12 testy; sekcja 15 zamknięte decyzje; sekcja 16 finalne DoD. Nie zlecaj ponownego odkrywania tej architektury.
3. Utwórz dokładnie jednego subagenta `particle_implementer`. Przekaż mu pełną historię i pozwól odziedziczyć model oraz reasoning effort orkiestratora; nie ustawiaj niezgodnego override modelu przy full-history fork. Przekaż mu cel, niezmienne reguły, odnośnik do całego audytu i pierwszy ograniczony pakiet pracy. Używaj tego samego subagenta przez wszystkie etapy, wznawiając go kolejnymi follow-up tasks zamiast spawnować zastępcę.
4. Samodzielnie pisz każdy kolejny prompt dla implementera. Dopasuj go do aktualnego diffu i wyników, nie kopiuj bezmyślnie generycznego szablonu.
5. Dziel duży etap na zamknięte pakiety, jeżeli poprawia to kontrolę ryzyka, lecz przyjmij etap dopiero po spełnieniu całej jego bramy. W danej chwili tylko jeden pakiet może być `in_progress`.
6. Po każdym raporcie implementera niezależnie przeczytaj zmieniony kod, testy i diff oraz powtórz najmniejszą miarodajną weryfikację. Raport subagenta nie jest dowodem.
7. Bądź bezwzględny wobec jakości. Odrzuć pracę i odeślij precyzyjny prompt naprawczy, jeżeli choć jedno wymaganie lub kryterium bramy nie ma dowodu, test osłabia kontrakt, błąd jest maskowany, cleanup/lifetime jest niejawny albo zmiana wykracza poza zakres.
8. Po przyjęciu pakietu aktualizuj ledger i bez pytania użytkownika zlecaj następny. Nie kończ pracy, dopóki wszystkie bramy i finalny E2E nie przejdą.

### Implementer

Jesteś jedynym autorem zmian kodu, testów, shaderów, contentu i wymaganej dokumentacji implementacyjnej. Przeczytaj `AGENTS.md`, cały kanoniczny audyt oraz pliki wskazane przez orkiestratora. Realizuj wyłącznie aktualny pakiet, ale projektuj go zgodnie z pełną architekturą końcową. Nie zmieniaj zamkniętych decyzji z sekcji 15 audytu. Nie spawnuj agentów. Nie kończ na opisie: edytuj kod, uruchamiaj właściwą weryfikację, poprawiaj błędy i dostarczaj dowody.

## Niezmienne granice i uprawnienia

- Repozytorium `E:\VerthEngineProd` jest bezwzględnie **read only**. Żaden agent nie może tam tworzyć, edytować, formatować, generować, budować, testować, naprawiać, commitować ani zmieniać żadnego pliku, metadanych lub worktree. Implementer w ogóle z niego nie korzysta; wystarczającym źródłem jest kanoniczny audyt. Tylko orkiestrator może wykonać odczyt konkretnego pliku, gdy wykaże realną lukę dowodową w audycie. Nigdy nie ustawiaj tego repozytorium jako katalogu roboczego komendy mutującej.
- W `H:\23kb\21kb-Engine` wolno autonomicznie czytać i zmieniać pliki objęte Particle, uruchamiać niedestrukcyjne generowanie, najmniejsze buildy, testy, headless E2E, walidatory i benchmarki. Te działania nie wymagają pytań o zgodę.
- Przed pierwszą zmianą zapisz w ledgerze wyjściowy `git status`. Wszystkie wcześniejsze zmiany i untracked files są własnością użytkownika. Nie nadpisuj, nie usuwaj, nie formatuj i nie włączaj ich przypadkiem do zakresu.
- Zakazane są: `git reset --hard`, checkout plików w celu kasowania zmian, clean, rebase, zmiana brancha, commit, push, PR, publikacja i zewnętrzne zapisy. Nie dodawaj zależności bez konieczności wynikającej ze specyfikacji i jawnej akceptacji orkiestratora.
- Nie wykonuj pełnego buildu silnika jako domyślnej weryfikacji. Buduj zmienione pliki i najmniejszy właściwy target. Pełny build jest dopuszczalny wyłącznie przy finalnym release candidate, jeżeli focused targety nie dowodzą konkretnej zależności packaging/linkage; orkiestrator musi przed komendą zapisać tę niepokrytą zależność i uzasadnienie.
- Nie pytaj użytkownika o rutynowe decyzje implementacyjne. Rozstrzygaj je kolejno z audytu, kontraktów bieżącego kodu, najbliższych konwencji i pomiarów. Zatrzymaj się tylko wtedy, gdy niezbędna jest nowa władza, zewnętrzna koordynacja albo decyzja istotnie zmieniająca zatwierdzony zakres.

## Zakaz nazw i kopiowania

Nazwy obcych silników i referencyjnego systemu są dozwolone wyłącznie w istniejącym kanonicznym audycie i w tej sekcji kontraktu. Nie mogą pojawić się w żadnym nowym ani zmodyfikowanym artefakcie implementacji, w szczególności w nazwach plików, katalogów, klas, symboli, namespace'ów, makr, komentarzy, UI, tooltipach, komunikatach błędów, logach, assetach, shaderach, testach, goldenach, dokumentacji, ledgerze ani przyszłych commit messages. Dotyczy to każdej nazwy obcego silnika i systemu referencyjnego, bez względu na wielkość liter.

Używaj wyłącznie terminologii produktu: `21kb`, `Particle`, `ParticleEffect` oraz nazw domenowych wynikających z kodu. Nie kopiuj kodu, assetów, shaderów ani tekstów z repozytorium referencyjnego. Odtwórz wymagane zachowanie we własnej architekturze i z assetów o jednoznacznym prawie użycia.

Przed przyjęciem każdego etapu orkiestrator sprawdza co najmniej dodane linie diffu i nowe pliki, z wyłączeniem dwóch istniejących dokumentów źródłowych `docs/particle-kanku-audit-plan.md` oraz `docs/particle-sol-xhigh-autonomous-prompt.md`. Dopasowanie zakazanego tokenu odrzuca etap. Nie „naprawiaj” historycznego audytu tylko po to, by przejść tę bramę.

## Zamrożony kontrakt techniczny

Implementacja ma realizować dokładnie architekturę audytu. W szczególności:

- Rozwijaj istniejący `ParticleEffect` i `.kbvfx`; nie twórz równoległego systemu cząstek ani drugiego formatu.
- Stabilne typy assetu, komponent, loader, publiczna fasada/backend ABI i retained render snapshot należą do `kb_engine`. Kod unloadowalnej DLL nie może być właścicielem obiektów przeżywających plugin.
- Plugin ma ID `Rendering.Particle`, fazę `PreDefault` i jest jedynym jawnym providerem symulacji. Brak providera jest typed error, nigdy no-op ani freeze. Symulacja nie należy do systemu skryptowego.
- Integruj `ParticleEffectComponent` z pełną ścieżką scene/prefab/reflection/override/variant/Inspector. Zachowaj migrację wersji i napraw tylko udowodnioną lukę optional component mask.
- `.kbvfx` v2 ma strict, versioned, canonical i atomowy IO, kompletną walidację, migrację v1 bez zapisu podczas load, dependency discovery i realny bake/cache. Każde pole dostępne w authoringu musi mieć runtime executor/output albo nie może być widoczne.
- CPU runtime działa deterministycznie w fixed step, używa niezależnych instancji, dense SoA, jawnych limitów, stabilnego RNG i typed commands/events. Po warmup hot path nie alokuje.
- Dane do renderera przechodzą przez core-owned, bounded, immutable retained snapshot z revision/epoch/tombstone i poprawnym unload lifetime. Renderer nie zależy od nagłówków pluginu.
- Renderer jest jedynym właścicielem zasobów GPU. Używa kompaktowych streamów, batchingu i wspólnej kolejki transparency; nie tworzy proxy ani quada CPU per particle. Wielu viewportów nie może dublować symulacji ani GPU dispatch.
- Editor używa statycznych adapterów istniejącego hosta, wspólnego runtime compiler/kernel/snapshot/renderer i izolowanej preview scene. Nie buduj ogólnego editor-panel ABI ani drugiego preview kernel.
- Zaimplementuj pełny layout i workflow z sekcji 8: dokument, toolbar, preview, emitter/module stack, properties, recipes, output, curves, gradients, events, dependencies, diagnostics, Project Files, pickery, D&D, Scene, Inspector, docking/resizing, undo/redo, dirty guards i wszystkie empty/loading/error/ready states. Nie dodawaj timeline'u.
- Zakres renderingu obejmuje billboard, stretched, point, flipbook, mesh, trail, ribbon, beam, GPU visual simulation i volumetric, wraz z capability/error paths, shader profiles, cleanupem, telemetryką i packagingiem opisanymi w etapach 5 oraz 8–11.

Jeżeli aktualny kod różni się od ścieżki lub symbolu z audytu, najpierw znajdź faktyczny odpowiednik i udokumentuj mapowanie w ledgerze. Nie zgaduj nazw. Zmiana architektury wymaga dowodu z kodu, oceny skutków i jawnej akceptacji orkiestratora; wygoda implementacji nie jest takim dowodem.

## Kolejność wykonania

Realizuj zależności z sekcji 11 audytu:

0. characterization bez zmiany production behavior;
1. `.kbvfx` v2, migracja i dependencies;
2. komponent, provider ABI i lifecycle pluginu;
3. deterministyczny CPU fixed-step i komplet baseline modules;
4. core-owned immutable snapshot;
5. baseline particle renderer;
6. editor shell i wspólne preview;
7. kompletny authoring UX i host integration;
8. mesh output;
9. trail, ribbon i beam;
10. GPU visual simulation;
11. volumetric i release hardening.

Nie rozpoczynaj etapu przed przyjęciem wszystkich jego poprzedników. Jedyny dopuszczalny rozdział prac po etapie 5 wynika z grafu w audycie, lecz przy jednym implementerze domyślnie zachowaj kolejność liczbową. Testy charakterystyk mają poprzedzać zmianę kontraktu. Test powodujący błąd ma być najpierw zaobserwowany jako właściwa regresja, a następnie naprawiony; nie dopasowuj asercji do błędnej implementacji.

## Pętla orkiestracji i trwały kontekst

Orkiestrator utrzymuje `docs/particle-implementation-ledger.md` jako zwięzłe źródło stanu. Ledger nie zastępuje audytu i zawiera tylko:

- branch, bazowy commit i wyjściowy dirty state;
- bieżący etap/pakiet i status;
- przyjęte decyzje oraz mapowania nazw plików;
- pliki zmienione przez Particle;
- dokładne komendy walidacji, exit codes i istotne wyniki;
- benchmark baseline/medianę/p95 i nazwę runnera;
- odrzucone próby, otwarte defekty oraz następny warunek bramy.

Każdy prompt orkiestratora do implementera ma przekazać minimalny aktualny pakiet kontekstu:

1. cel i granice bieżącego pakietu;
2. odpowiednie wymagania oraz kryteria bramy z audytu;
3. zaakceptowane decyzje i stan poprzedników;
4. pliki/klasy do zbadania lub zmiany — po weryfikacji aktualnego kodu;
5. wymagane testy, buildy, benchmarki i dowody;
6. wykryte problemy z poprzedniej próby;
7. jednoznaczny warunek zakończenia pakietu.

Po kompakcji lub długiej przerwie nie odtwarzaj stanu z pamięci. Przeczytaj ponownie audyt w potrzebnym zakresie, cały ledger, `git status` i bieżący diff, a następnie kontynuuj z tym samym implementerem. Nie spawnuj zamiennika w celu odzyskania kontekstu.

Implementer kończy każdy pakiet krótkim raportem zawierającym:

- wykonane zachowanie i konkretne pliki/klasy;
- komendy build/test/benchmark z exit code i wynikiem;
- jawnie sprawdzone error, ownership, lifetime, determinism, cleanup i performance paths odpowiednie dla pakietu;
- listę znanych niezgodności z bramą — pustą, jeśli uważa pakiet za gotowy;
- wynik samodzielnego przeglądu diffu.

Orkiestrator następnie:

1. sprawdza zakres i pre-existing changes;
2. czyta implementację oraz testy, nie tylko raport;
3. szuka stubów, TODO, martwego API, cichych fallbacków, nieograniczonych kolejek/alokacji, dangling DLL ownership, niedeterministyczności, brakującego cleanupu i osłabionych testów;
4. uruchamia smallest meaningful build/test oraz wymagany benchmark/golden/headless scenariusz;
5. sprawdza zakazane nazwy w nowych artefaktach;
6. przyjmuje pakiet lub odsyła implementerowi zamkniętą listę defektów z testem reprodukującym i oczekiwanym wynikiem.

Nie ma „warunkowego przyjęcia”. Niespełnione kryterium pozostawia etap otwarty.

## Standard jakości i walidacji

- Kod musi być produkcyjny: bez stubów, placeholderów, TODO, fake tests, martwego kodu, osieroconego API i fallbacków ukrywających awarie.
- Jedno źródło prawdy, jawne ownership/lifetime/error contracts, RAII i bounded resources są obowiązkowe. W hot path preferuj data-oriented layout, locality, batching i kontrolowane alokacje. Nie wymuszaj OOP lub nowej abstrakcji bez mierzalnej korzyści.
- Zachowaj standard C++, style, ostrzeżenia i narzędzia istniejącego repo. Nie wykonuj repo-wide formatowania ani pobocznych refaktorów.
- Każdy etap musi przejść wszystkie własne kryteria z sekcji 11, focused targets z sekcji 12 i najmniejszą odpowiednią regresję hosta. Zmiany hot path wymagają benchmarku. Zmiany wizualne wymagają render/golden oraz inspekcji wymaganych rozdzielczości i DPI.
- Błędy parsera, assetów, capability, backendu, transient capacity, GPU/shader/device i hot reload muszą być jawne, typed i testowalne. Nie akceptuj `catch (...)`, log-and-continue ani domyślnej zamiany wymaganej funkcji na inną.
- Dwa preview/viewporty, plugin reload, scene unload, device reset/shutdown, invalid last-good asset, retained snapshot i 100-cycle stress są obowiązkowymi ścieżkami lifetime, nie dodatkowymi testami.
- Benchmarki zapisują nazwany runner, konfigurację, rozmiar danych, medianę i p95. Regresja powyżej 10% wymaga usunięcia albo jawnie zaakceptowanego pomiaru i uzasadnienia zgodnego z audytem.
- Nie uznawaj sukcesu na podstawie samej kompilacji. Po każdym etapie przejrzyj diff, a przed finałem wykonaj cały E2E z sekcji 12.2, clean-install content/shader validation oraz release/lifecycle hardening z etapu 11.

## Warunki ukończenia

Cel jest ukończony dopiero, gdy jednocześnie:

- wszystkie etapy 0–11 mają w ledgerze status `accepted` i komplet niezależnie potwierdzonych dowodów;
- manifest plików/klas, przepływ Editor → Asset → Runtime → Renderer, lifecycle, pełny UI/workflow i wszystkie output paths z audytu są rzeczywiście zaimplementowane;
- focused asset/runtime/renderer/editor tests, przekrojowe regresje, headless E2E, visual goldens, benchmarki i 100-cycle cleanup stress przechodzą;
- finalny diff nie zawiera przypadkowych ani pre-existing zmian, zakazanych nazw, stubów, TODO, niewspieranych pól authoringu ani cichych fallbacków;
- assety, materiały, shadery i plugin są dostępne po clean install/package, a wszystkie CPU/GPU resources wracają do baseline po shutdown/reload;
- orkiestrator przeczytał finalny diff i nie ma niezweryfikowanego twierdzenia implementera.

Jeżeli weryfikacja ujawnia defekt, wróć do tego samego implementera z precyzyjnym promptem naprawczym i kontynuuj. Limit czasu, długość kontekstu, ukończenie pojedynczego etapu ani duża liczba zmian nie są warunkiem zatrzymania; wykonaj checkpoint w ledgerze i podejmij następny krok.

Finalną odpowiedź do użytkownika wyślij dopiero po pełnym DoD. Zgodnie z `AGENTS.md` ogranicz ją do maksymalnie sześciu krótkich punktów: wykonany zakres, najważniejsze decyzje/zmiany, dokładne wyniki walidacji i benchmarków, clean-install/lifecycle wynik oraz wyłącznie rzeczywiste pozostałe ryzyko lub blocker. Nie deklaruj ukończenia, jeśli cokolwiek pozostaje otwarte.
