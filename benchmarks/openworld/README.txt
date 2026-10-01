21kb: laboratorium dużych światów, Windows / headless / Release

IMPLEMENTATION_PROMPT.txt zawiera zadanie i konkretne klasy objęte poprawkami.
Katalogi silników referencyjnych służyły wyłącznie do odczytu. Nie kopiowano kodu.

Budowanie (Developer Command Prompt x64, MSVC / Windows SDK / CMake / Ninja):
  cmake -S . -B build/perf-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DKB_BUILD_EDITOR=ON -DKB_BUILD_GRAPH_SHADERC=OFF -DKB_GENERATE_RENDERER_SHADERS=OFF -DKB_PERFORMANCE_SOURCE_DIR="%CD%/benchmarks/openworld"
  cmake --build build/perf-release --parallel 4 --target kb_game kb_editor kb_engine_tests kb_renderer_tests kb_openworld_perf

Ta konfiguracja używa istniejących binarnych shaderów renderera. Pełny cooker
materiałów wymaga osobnego shaderc; OFF nie jest testem całego cookera.
Nie wymaga Pythona do kompilacji ani do uruchomienia natywnych testów/scen.
Skrypty porównania i raportowania wymagają Pythona 3, wyłącznie standard library.

Generowanie nowego projektu (ponownie generuj na innej maszynie: descriptor
zawiera ścieżki do zbudowanych lokalnych pluginów):
  build\perf-release\bin\kb_openworld_perf.exe generate E:\21kbProjekty\OpenWorldAfter_20260930

Surowe CPU, 180 klatek rozgrzewki + 360 pomiarów:
  build\perf-release\bin\kb_openworld_perf.exe cpu E:\21kbProjekty\OpenWorldAfter_20260930 physics_4096 1
  build\perf-release\bin\kb_openworld_perf.exe cpu E:\21kbProjekty\OpenWorldAfter_20260930 batch_dirty_100k 1
  build\perf-release\bin\kb_openworld_perf.exe stream E:\21kbProjekty\OpenWorldAfter_20260930 static_100k 1
Tryb stream sprawdza trzy cykle, limit operacji, błędy i liczbę encji po fazach.
Tryb cpu-trace dodatkowo zbiera istniejące liczniki systemów ECS. Profilowanie
trace może zmienić timing; nie mieszaj takich próbek z podstawowym pomiarem.

Porównanie z bazą, trzy niezależne procesy na scenariusz, kolejno:
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldPerf_20260930 --output E:/21kbProjekty/OpenWorldAfter_20260930
Roślinność z nowego projektu:
  python benchmarks/openworld/run_compare.py --suite foliage --build build/perf-release --project E:/21kbProjekty/OpenWorldAfter_20260930 --output E:/21kbProjekty/OpenWorldAfter_20260930
  python benchmarks/openworld/analyze_compare.py --before E:/21kbProjekty/OpenWorldPerf_20260930 --after E:/21kbProjekty/OpenWorldAfter_20260930

Ścieżki --build/--project/--output mogą być względne: skrypt rozwiązuje je przed
zmianą katalogu roboczego procesu. Mierz po zakończeniu kompilacji/testów;
nie uruchamiaj innych benchmarków równolegle. Skrypty Windows używają
CREATE_NO_WINDOW; kb_game dodatkowo ma --headless, nie pokazuje okna ani
nie przechwytuje wejścia. Zwykła gra nadal ma widoczne okno.

GPU: rzeczywisty D3D11, 1920x1080, ForwardPlus, 600 klatek, pierwsze 120
odrzucane. Brak kosztu screenshotów w pomiarze. 1000/wall_frame_ms jest
przepustowością poza ekranem, nie dowodem FPS na ekranie. GPU timers są
opóźnione. Liczniki widoczności/świateł/drawów sumują przebiegi renderera.
Analizator odrzuca wyniki z dropped/missing_resources i sprawdza culling.

GeometrySwarm: 100 tys. / milion rezydentnych instancji, cztery trójkąty na
instancję, zielony materiał, bez cieni/wiatru/tekstur/alpha overdraw. Jeden
właściciel ECS, nie osobna encja/fizyka dla każdej rośliny. Widoczny jest wycinek
pola kamery. GeometrySwarmVisibilityClusters zachowuje wynik cullingu poprzez
konserwatywne zakresy po 128 instancji; przy wielu LOD lub SurfaceCast zachowuje
oryginalną ścieżkę. Duże sceny nadal wymagają budżetowania widocznej geometrii.

Publiczne API streamingu (wywołuj na wątku właściciela Scene):
  scene.LoadedContent().ConfigureStreaming({.maxPendingLoads=8,
      .maxOperationsPerFrame=256, .maxMillisecondsPerFrame=2.0F});
  const auto id = scene.LoadedContent().LoadAsync("/Game/Scenes/Cell.21kbscene", owner);
  // istniejący Runtime.Update pompuje zadania; obsłuż Ready/Failed/Error
  const bool accepted = scene.LoadedContent().UnloadAsync(id);

Wczytywanie jest additive. Fizyczne pliki oraz zarejestrowane Scene/ScenePrefab
używają przygotowania w tle, istniejącego bulk ECS oraz porcjowanych zmian.
Zarejestrowane źródła korzystają z istniejącego AssetManager async worker;
test pakietu usuwa luźny plik przed ładowaniem. Referencje rozwiązywane są po
utworzeniu wszystkich encji. Aktywacja i usuwanie są stopniowe, nie atomowe.
Unload obejmuje runtime-spawned descendants. Budżet 2 ms jest miękki: sprawdzany
między porcjami. Licz cały Runtime.Update, nie tylko StreamingStats. Porcja
może przekroczyć 2 ms, a przebudowa hierarchii/pose cache ma własny koszt.
Anulowanie nie blokuje Update na I/O. Zniszczenie Scene dołącza jego zadania.
StreamFocus automatycznie używa tej ścieżki i zachowuje hysteresis/priorytety.
Sceny PrefabPrivate nie przyjmują streamingu. Dotychczasowe Load jest synchroniczne.

Testy poprawności:
  build\perf-release\engine\kb_engine_tests.exe scene-prefab scene-runtime engine-library
  build\perf-release\bin\kb_renderer_tests.exe
Pełny engine suite uruchamiaj z bin/ w środowisku MSVC/CMake/Ninja (native script
test kompiluje plugin). Renderer suite uruchamiaj z głównego katalogu repo,
gdyż część testów odczytuje źródła shaderów. Program testowy jest w bin/ obok
shaderów. Pełny renderer suite ma osobny komunikat skip WebGPU fallback przy
już uruchomionym urządzeniu; ten benchmark mierzy D3D11.

Wyniki z 30.09.2026: Results/comparison.csv oraz .json w nowym projekcie,
Results/Raport_po_poprawkach.html i surowe CSV. Mała kopia comparison.csv jest
w repozytorium. Nie dołączaj wygenerowanych projektów/binariów do źródeł silnika.
Nie deklaruj gotowości gry klasy AAA na podstawie kostek i prostych trójkątów.

Produkcja: kolejna seria z bazą 3f4a9fc6 (01.10.2026)
PRODUCTION_PROMPT.txt zawiera cel, klasy, kontrakt własności i bramki.
PRODUCTION_RESULTS_20261001.txt zawiera wyniki i konkretne niezaliczone bramki.
Jest to raport historyczny pierwszego etapu. Wyniki kolejnego etapu i poprawkę
metodologii CPU opisuje PRODUCTION_RESULTS_STAGE2_20261001.txt.
Baza została osobno skompilowana z tym samym harness pomiarowym. Sceny, liczba
obiektów, rozdzielczość i backend pozostały zgodne. Nie porównuj serii pomiarów
z różnych dat jako jednego kontrolowanego eksperymentu.

Projekt mieszany: 10k prostych meshów, 10k colliderów statycznych, 512 ciał
ruchomych, 128 świateł punktowych + słońce, cienie, 100k instancji oraz komórka
10k meshów po 960 trójkątów. Kamera steruje StreamFocus, trzy kompletne cykle
w 3420 klatkach. Trzy procesy dają dziewięć zweryfikowanych cykli.
Forward+ ma obecnie limit 32 świateł/pas. Scena lights_512 bada selekcję
spośród 512 świateł; nie dowodzi jednoczesnego oświetlenia wszystkimi.

Generowanie na danej maszynie:
  build\perf-release\bin\kb_openworld_perf.exe generate E:\21kbProjekty\OpenWorldProduction_20260930
Seria bazowa oraz końcowa: identyczne poniższe polecenia, osobne --output.
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldAfter_20260930 --output E:/21kbProjekty/OpenWorldProduction_20260930 --suite compare
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldAfter_20260930 --output E:/21kbProjekty/OpenWorldProduction_20260930 --suite foliage
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProduction_20260930 --output E:/21kbProjekty/OpenWorldProduction_20260930 --suite production --fixed-step
  python benchmarks/openworld/report_production.py --before E:/21kbProjekty/OpenWorldProductionBefore_20260930 --after E:/21kbProjekty/OpenWorldProduction_20260930
--fixed-step wymusza symulację 1/60 s tylko w headless z profile-file. Daje
stałą pracę symulacji mimo zmiennej przepustowości CPU/GPU. Seria zwykła
zachowuje rzeczywisty zegar. Raport rozdziela obie serie.
--repetition 1/2/3 pozwala powtórzyć konkretną serię; nie wybieraj najładniejszej.

Nowe testy API i rendererowego unieważniania:
  cmake --build build/perf-release --parallel 4 --target kb_ecs_api_tests
  build\perf-release\engine\kb_ecs_api_tests.exe
  build\perf-release\bin\kb_renderer_tests.exe command-reuse
  build\perf-release\bin\kb_renderer_tests.exe webgpu-texture-fallback
Testy obejmują publikację ECS przed OnSet, odroczenie i dziedziczenie backendu,
limit wątków, animację UI, LOD między pasami, TAA, zmianę zasobów i kamer,
brak kolejnego uploadu statycznych instancji oraz cienie generowanej geometrii.

Drugi etap (01.10.2026): topologia transformów, aktywna fizyka i pojemność
SceneTransformTopologyCache jest właścicielem pochodnych poziomów wykonania.
Dodanie i usuwanie liści nie przebudowuje całego świata. Przeniesienie poddrzewa
zachowuje bezpieczną pełną przebudowę. RemovalVersion w NativeArchetypeStorage
rozróżnia dodanie od usunięcia: nieznane usunięcie komponentu/rodzica nadal
wymusza bezpieczne przeliczenie, także po późniejszym usunięciu znanego liścia.
JoltStaticBodyBatchCache przechowuje wyłącznie klucze ważności batchy. Nie
kopiuje komponentów ani ciał. Transform/Collider, archetyp i generacja encji
unieważniają zakres. Limit backendu wynosi 131072 ciała; nie oznacza to testu
131072 aktywnych ciał ani zwiększenia limitu kontaktów solvera.

CPU odbiera teraz zdarzenia kolizji w każdej klatce. ProductionBeforeCorrected
zawiera świeże CPU z bazy 3f4a9fc6 z identycznym konsumentem. Pomiary GPU oraz
streamingu skopiowano z zachowanej bazy, ponieważ ich obciążenie nie zmieniło
się przy dodaniu konsumenta do testu CPU. Manifest opisuje pochodzenie próbek.
Runtime.Update nie obejmuje testowego Wake, odczytu zdarzeń ani raycastów;
total_ms je obejmuje, query_ms mierzy raycasty osobno. Nie porównuj CPU z
pierwszym etapem, który kumulował nieodebrane kontakty.

Końcowe serie (każda zawiera trzy procesy, uruchamiaj kolejno):
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldAfter_20260930 --output E:/21kbProjekty/OpenWorldProductionStage2_20261001 --suite compare
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldAfter_20260930 --output E:/21kbProjekty/OpenWorldProductionStage2_20261001 --suite capacity
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldAfter_20260930 --output E:/21kbProjekty/OpenWorldProductionStage2_20261001 --suite foliage
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProduction_20260930 --output E:/21kbProjekty/OpenWorldProductionStage2_20261001 --suite production --fixed-step
  python benchmarks/openworld/report_production.py --before E:/21kbProjekty/OpenWorldProductionBeforeCorrected_20261001 --after E:/21kbProjekty/OpenWorldProductionStage2_20261001
Nowy colliders_100k wymaga 1000 rzeczywistych trafień raycastów/klatkę. Baza
65536 ciał nie ma poprawnego pomiaru 100k; raport podaje brak porównania.
Reporter zapisuje też niezaliczone wyniki i zwraca exit 1 przy nieudanej
bramce lub regresji średniej ponad 5%, zamiast ukrywać je zielonym statusem.

AssetManager::PrepareAsyncLoad obejmuje walidację zależności. Jej czytanie
i dekodowanie sceny działa na istniejącym workerze. AssetRegistrySnapshotCache
współdzieli widok do odczytu między zadaniami jednej generacji; jedynym
mutowalnym katalogiem jest istniejący AssetRegistry. Zmiana generacji przed
publikacją daje błąd do obsłużenia i wymaga ponownego zgłoszenia wczytania.
Pierwszy widok po zmianie katalogu wymaga jego kopii; nie jest to darmowy
streaming dowolnej liczby zasobów. Anulowanie i wymiana loadera zachowują
istniejący kontrakt generacji zadań.

Pełna weryfikacja kompilacji graph shaderów (osobno od porównania wydajności):
  cmake -S . -B build/perf-release -DKB_BUILD_GRAPH_SHADERC=ON
  cmake --build build/perf-release --parallel 4 --target kb_game kb_editor kb_renderer_tests kb_engine_tests kb_ecs_api_tests kb_openworld_perf
  build\perf-release\bin\kb_renderer_tests.exe graph-shader-artifact
  build\perf-release\bin\kb_renderer_tests.exe graph-forward-gpu
Nie używaj czasu budowania shaderc jako czasu renderera. KB_GENERATE_RENDERER_SHADERS
pozostaje OFF, więc istniejące shadery renderera nie zmieniają się w porównaniu.
Rzeczywiste gotowanie graph shaderów sprawdza oddzielny test artefaktów.

Etap 3: masowe transformy (PRODUCTION_RESULTS_STAGE3_20261001.txt)
SceneTransformLeafBatchUpdater działa na pożyczonych wierszach kanonicznego
ECS. Liście mogą ominąć kopię całej hierarchii, gdy cały łańcuch rodziców jest
zsynchronizowany. Zmieniony przodek, poddrzewo, nieznane usunięcie i aktywny
budżet propagacji zachowują dotychczasowy algorytm. Wskaźniki rodzica żyją
wyłącznie w obrębie zakresu; nie są trwałą kopią danych sceny.
Publikacja odczytuje bieżący komponent przed każdym OnSet. Obserwator może
usunąć lub edytować kolejny komponent bez jego odtworzenia ze starej kopii.
SceneTransformComponentStore używa zarejestrowanego ID swojego World zamiast
ponownie wyszukiwać typ przy każdym odczycie i sygnale dirty.

Aktualna bramka batch_dirty_100k: Runtime.Update średnia <=8 ms, P99 <=12 ms.
mutation_ms i total_ms nadal są mierzone; zaliczenie Update nie oznacza,
że 100k mutowanych obiektów wraz z renderowaniem mieści się w 16.67 ms.
Nowy projekt i surowe serie: E:/21kbProjekty/OpenWorldProductionStage3_20261001.
Ten sam generator i scenariusze, trzy procesy na scenariusz, bez trace:
  build\perf-release\bin\kb_openworld_perf.exe generate E:/21kbProjekty/OpenWorldProductionStage3_20261001
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProductionStage3_20261001 --output E:/21kbProjekty/OpenWorldProductionStage3_20261001 --suite compare
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProductionStage3_20261001 --output E:/21kbProjekty/OpenWorldProductionStage3_20261001 --suite capacity
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProductionStage3_20261001 --output E:/21kbProjekty/OpenWorldProductionStage3_20261001 --suite foliage
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProductionStage3_20261001 --output E:/21kbProjekty/OpenWorldProductionStage3_20261001 --suite production --fixed-step
  python benchmarks/openworld/report_production.py --before E:/21kbProjekty/OpenWorldProductionBeforeCorrected_20261001 --after E:/21kbProjekty/OpenWorldProductionStage3_20261001
Reporter wymusza nową bramkę bulk; historyczny raport etapu 2 zawiera bramki
obowiązujące w momencie jego pomiaru. Do porównania etapów użyj --before
E:/21kbProjekty/OpenWorldProductionStage2_20261001. colliders_100k ma wtedy
rzeczywiste porównanie przed/po, bez przypisywania mu nieistniejącej bazy 3f4a.


Etap 4 (2026-10-01): lokalna retencja poleceń i własność buforów instancji
RenderScene grupuje zwykłe siatki w strony po maks. 1024 ID na mesh/material.
Geometry Swarm i Space Stroke mają własne grupy właścicieli. Dane pozostają
w ECS/proxy; strony, polecenia i rekordy cullingu są pochodnym stanem renderera.
SceneMeshBatchCommandCache przenosi istniejące wektory poleceń. Pełne przebudowy
łączą zgodne polecenia między stronami; stabilna kamera pozwala na retencję stron.
SceneMeshInstanceBufferPool oddziela alokacje od sortowania, chroni wielokrotne
submit w klatce i viewporty; nieużywane klucze usuwa po 3 klatkach.
Zmiany widoczności, usuwanie i rekey siatki aktualizują tylko zależne grupy.

Projekt: E:/21kbProjekty/OpenWorldProductionStage4_20261001. Powtórzenie:
  build/perf-release/bin/kb_openworld_perf.exe generate E:/21kbProjekty/OpenWorldProductionStage4_20261001
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProductionStage4_20261001 --output E:/21kbProjekty/OpenWorldProductionStage4_20261001 --suite compare
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProductionStage4_20261001 --output E:/21kbProjekty/OpenWorldProductionStage4_20261001 --suite capacity
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProductionStage4_20261001 --output E:/21kbProjekty/OpenWorldProductionStage4_20261001 --suite foliage
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldProductionStage4_20261001 --output E:/21kbProjekty/OpenWorldProductionStage4_20261001 --suite production --fixed-step
  python benchmarks/openworld/report_production.py --before E:/21kbProjekty/OpenWorldProductionStage3_20261001 --after E:/21kbProjekty/OpenWorldProductionStage4_20261001

Aktualne bramki pełnego celu: bulk total_ms średnia i P99 <=16.67 ms; city fixed
wall_frame_ms P99 <=16.67 ms; mixed fixed warm_wall_frame_ms P99 <=16.67 ms.
Mixed: ciepłe próbki od klatki 120; pełna seria i zimny maksimum pozostają
w raporcie. FAIL oznacza dalszą pracę; pliki historyczne zachowują stare bramki.

Etap 5 (2026-10-01): runtime SyncStructural po zmianie topologii zamiast pełnego Sync.
Bieżące zmiany proxy są odbierane również po uzgodnieniu struktury.
Projekt: E:/21kbProjekty/OpenWorldProductionStage5_20261001.
Odtworzenie: polecenia etapu 4 z Stage5 zamiast Stage4 w ścieżkach; identyczne
suite compare, capacity, foliage, production --fixed-step i generator.
Porównanie:
  python benchmarks/openworld/report_production.py --before E:/21kbProjekty/OpenWorldProductionStage4_20261001 --after E:/21kbProjekty/OpenWorldProductionStage5_20261001
Pełny raport: PRODUCTION_RESULTS_STAGE5_20261001.txt; dane: production_stage5_summary.csv.
Bramki pozostają bez zmian. Wynik reportera exit 1 zachowuje otwartą pracę.
