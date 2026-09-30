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
