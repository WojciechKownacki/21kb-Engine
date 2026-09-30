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
