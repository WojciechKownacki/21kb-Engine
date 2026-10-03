# Mapa zadań: jakość silnika (CPU, GPU, fizyka, shadery, shading)

Cel: 21kb ma być konkurencyjnym silnikiem komercyjnym — jakość obrazu i wydajność na poziomie
wiodących silników rynkowych. Pracujemy małymi partiami (jedno zadanie = jeden lub kilka małych commitów),
każda partia ma test, który przechodzi bez okna (headless).

Źródła: raport z benchmarku (`E:\EngineBenchmarks\FiveEngine\engines\21kb\NOTES.md`), pomiary i profile z
bieżącej pracy. Priorytety poniżej to **ocena inżynierska**, nie zatwierdzony plan produktu — kolejność można zmienić.

## Stan na dziś (zrobione i zmergowane do `Release`, PR #77)

| Obszar | Zrobione | Dowód |
| --- | --- | --- |
| Fizyka | Spirala kroków stałych, uśpione ciała, zdarzenia kolizji tylko dla konsumenta | test fizyki 4000 ciał: średnia 2,6 → 1,3 ms |
| ECS/CPU | Szybki lookup typu komponentu, pominięcie skanu paneli „facing" | profil: −12% czasu klatki |
| Cząstki CPU | `LiveParticleCount` O(1) | scena 500 emiterów ~65 → ~368 FPS |
| Cienie | 4 kaskady, cienie punktowe (4 światła), poprawka atlasu | testy pikselowe D3D11 (headless) |
| GI | Screen-space GI (tryb `SsGi`) | test pikselowy: odbicie koloru ściany |
| Cząstki GPU | Symulacja emiterów na GPU do 1 048 576 cząstek/emiter | test pikselowy, 2,4 ms/klatkę dla 1 mln |

## Zadania do wykonania

Format: **ID — tytuł** · priorytet · rozmiar (S ≤ pół dnia, M ≈ dzień, L > dnia) · status.
Kryterium „zrobione" = test, który musi przejść; test ma być headless (bez widocznego okna).

### A. Jakość obrazu

- **A1 — Wygładzanie GI w czasie** · P1 · M · **zrobione** (szum podłogi 27,1 → 7,2 w teście pikselowym)
  Dziś GI jest ziarniste (6 promieni na piksel, losowanie co klatkę). Dodać akumulację czasową (reprojekcja
  historii + odrzucenie przy zmianie), parametry intensywności i zasięgu w `SceneRenderLightingConfig`.
  Zrobione gdy: test pikselowy pokazuje wyraźnie mniejszą wariancję jasności podłogi po N klatkach niż przed zmianą,
  a istniejący test odbicia koloru nadal przechodzi.
- **A2 — Cienie reflektorów (spot)** · P1 · M · **zrobione** (jasność pod płytą 527 → 281)
  Użyć istniejącego atlasu cieni punktowych (jedna ściana zamiast sześciu). Zrobione gdy: test pikselowy
  „z cieniem / bez cienia" dla światła spot.
- **A3 — Cienie punktowe poza ograniczeniami** · P2 · M · **zrobione częściowo** (materiały z grafu odbierają cienie punktowe, kaskady płynnie przechodzą; limit 4 świateł bez zmian)
  Materiały z edytora grafów nie odbierają cieni punktowych; limit 4 światła; brak płynnego przejścia kaskad.
  Zrobione gdy: test dla materiału grafowego oraz test przejścia między kaskadami bez widocznego szwu.
- **A4 — Ambient occlusion i odbicia w przestrzeni ekranu** · P2 · L · **zrobione** (AO: 149 → 127,5 przy ścianie; SSR: czerwień odbicia −9,3 → 21,9)
  Zrobione gdy: testy pikselowe dla obu efektów + przełączniki w konfiguracji.
- **A5 — GI poza ekranem (sondy lub voxele)** · P3 · L · **zrobione** (siatka wokseli z pudełek OBB; czerwień z obiektu poza kadrem −12,2 → +12,2)
  Zrobione gdy: oświetlenie pośrednie z obiektów spoza kadru widoczne w teście pikselowym.

### B. Cząstki GPU

- **B1 — Kolizje cząstek GPU z głębią sceny** · P1 · M · **zrobione** (płaszczyzna i głębia, forward i deferred)
  Dziś emitery z kolizjami wracają na CPU. Zrobione gdy: test, że cząstka odbija się od płaszczyzny widocznej
  w buforze głębi, oraz że emitery z `CollisionPlane` mogą działać na GPU.
- **B2 — Sortowanie cząstek przezroczystych na GPU** · P2 · M · do zrobienia
- **B3 — Przestrzeń lokalna, podążanie za transformacją, wyjście mesh/trail** · P2 · L · do zrobienia
  Dziś kwalifikują się tylko emitery w przestrzeni świata z wyjściem billboard/stretched.
- **B4 — Pod-emitery i zdarzenia na GPU** · P3 · L · do zrobienia
- **B5 — Test „od pliku do piksela"** · P1 · S · do zrobienia
  Jeden test łączący plugin cząstek, kolejkę i renderer w jednym procesie (dziś sprawdzone w częściach).
- **B6 — Warianty shaderów dla macOS (Metal)** · P3 · wymaga komputera Mac · zablokowane
  Bez nich `cs_particle_gpu_emit` jest opcjonalny w manifeście, a emitery wracają na CPU.

### C. Wydajność CPU i fizyka

- **C1 — Skoki kroku fizyki (Jolt)** · P1 · M · do zrobienia
  Pojedyncze kroki do ~20 ms przy 4000 ciał. Profilować, rozważyć nakładanie kroku z pracą renderera lub
  porcjowanie. Zrobione gdy: p99 kroku 4000 ciał poniżej połowy dzisiejszej wartości (pomiar w teście headless).
- **C2 — Zapis transformacji 30 tys. obiektów** · P1 · M · do zrobienia
  ~6 ms na 30 tys. `Transform.Set` + synchronizacja hierarchii. Ścieżka wsadowa/równoległa.
  Zrobione gdy: pomiar na tym samym scenariuszu spada co najmniej o 40%.
- **C3 — Budowa komend rysowania** · P2 · M · do zrobienia
  `BuildCommandsInto` + test frustum ~1,3 ms na 10 tys. obiektów; ścieżka rysowania ~0,35 ms.
- **C4 — Równoległe pętle ECS** · P2 · L · do zrobienia
- **C5 — Test obciążenia agentów (raycasty + ruch)** · P2 · S · do zrobienia
  Dodać do zestawu testów powtarzalny pomiar, żeby regresje były widoczne.

### D. Jakość procesu i narzędzia

- **D1 — Ponowne porównanie z konkurencją** · P1 · S · **wymaga zgody użytkownika** (uruchamia okno)
  Raport nie był powtórzony po zmianach. Uruchamiać tylko po wyraźnym „tak".
- **D2 — Niestabilne testy przeładowania wtyczek przy `-j6`** · P2 · S · do zrobienia
  Współdzielone pliki DLL między testami uruchamianymi równolegle.
- **D3 — Test `kb_standalone_player_camera_runtime`** · P3 · S · do zrobienia
  Zakłada DLL-e pod domyślną ścieżką katalogu build; powinien znajdować je względem swojego drzewa.
- **D4 — Domyślne ustawienia jakości** · P2 · S · do zrobienia
  Udokumentować koszt pamięci (4 kaskady = atlas 2048², 4 światła punktowe = ~25 MB) i profile jakości.

## Poza zakresem (decyzje użytkownika)

- Adaptery benchmarku (`E:\EngineBenchmarks\...`) nie są poprawiane — zmiany robimy w silniku.
- Nazwy innych silników nie mogą pojawić się w niczym, co trafia do gita (kod, komentarze, commity, PR-y, dokumenty).

## Zasady wykonania

Prompt dla agenta wykonującego te zadania: `docs/engine-quality-agent-prompt.md`.
Po zakończeniu zadania: zaktualizować status w tym pliku w tym samym commicie.
