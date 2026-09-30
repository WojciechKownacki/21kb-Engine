21kb: laboratorium duĹĽych Ĺ›wiatĂłw, Windows / headless / Release

IMPLEMENTATION_PROMPT.txt zawiera zadanie i konkretne klasy objÄ™te poprawkami.
Katalogi silnikĂłw referencyjnych sĹ‚uĹĽyĹ‚y wyĹ‚Ä…cznie do odczytu. Nie kopiowano kodu.

Budowanie (Developer Command Prompt x64, MSVC / Windows SDK / CMake / Ninja):
  cmake -S . -B build/perf-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DKB_BUILD_EDITOR=ON -DKB_BUILD_GRAPH_SHADERC=OFF -DKB_GENERATE_RENDERER_SHADERS=OFF -DKB_PERFORMANCE_SOURCE_DIR="%CD%/benchmarks/openworld"
  cmake --build build/perf-release --parallel 4 --target kb_game kb_editor kb_engine_tests kb_renderer_tests kb_openworld_perf

Ta konfiguracja uĹĽywa istniejÄ…cych binarnych shaderĂłw renderera. PeĹ‚ny cooker
materiaĹ‚Ăłw wymaga osobnego shaderc; OFF nie jest testem caĹ‚ego cookera.
Nie wymaga Pythona do kompilacji ani do uruchomienia natywnych testĂłw/scen.
Skrypty porĂłwnania i raportowania wymagajÄ… Pythona 3, wyĹ‚Ä…cznie standard library.

Generowanie nowego projektu (ponownie generuj na innej maszynie: descriptor
zawiera Ĺ›cieĹĽki do zbudowanych lokalnych pluginĂłw):
  build\perf-release\bin\kb_openworld_perf.exe generate E:\21kbProjekty\OpenWorldAfter_20260930

Surowe CPU, 180 klatek rozgrzewki + 360 pomiarĂłw:
  build\perf-release\bin\kb_openworld_perf.exe cpu E:\21kbProjekty\OpenWorldAfter_20260930 physics_4096 1
  build\perf-release\bin\kb_openworld_perf.exe cpu E:\21kbProjekty\OpenWorldAfter_20260930 batch_dirty_100k 1
  build\perf-release\bin\kb_openworld_perf.exe stream E:\21kbProjekty\OpenWorldAfter_20260930 static_100k 1
Tryb stream sprawdza trzy cykle, limit operacji, bĹ‚Ä™dy i liczbÄ™ encji po fazach.
Tryb cpu-trace dodatkowo zbiera istniejÄ…ce liczniki systemĂłw ECS. Profilowanie
trace moĹĽe zmieniÄ‡ timing; nie mieszaj takich prĂłbek z podstawowym pomiarem.

PorĂłwnanie z bazÄ…, trzy niezaleĹĽne procesy na scenariusz, kolejno:
  python benchmarks/openworld/run_compare.py --build build/perf-release --project E:/21kbProjekty/OpenWorldPerf_20260930 --output E:/21kbProjekty/OpenWorldAfter_20260930
RoĹ›linnoĹ›Ä‡ z nowego projektu:
  python benchmarks/openworld/run_compare.py --suite foliage --build build/perf-release --project E:/21kbProjekty/OpenWorldAfter_20260930 --output E:/21kbProjekty/OpenWorldAfter_20260930
  python benchmarks/openworld/analyze_compare.py --before E:/21kbProjekty/OpenWorldPerf_20260930 --after E:/21kbProjekty/OpenWorldAfter_20260930

ĹšcieĹĽki --build/--project/--output mogÄ… byÄ‡ wzglÄ™dne: skrypt rozwiÄ…zuje je przed
zmianÄ… katalogu roboczego procesu. Mierz po zakoĹ„czeniu kompilacji/testĂłw;
nie uruchamiaj innych benchmarkĂłw rĂłwnolegle. Skrypty Windows uĹĽywajÄ…
CREATE_NO_WINDOW; kb_game dodatkowo ma --headless, nie pokazuje okna ani
nie przechwytuje wejĹ›cia. ZwykĹ‚a gra nadal ma widoczne okno.

GPU: rzeczywisty D3D11, 1920x1080, ForwardPlus, 600 klatek, pierwsze 120
odrzucane. Brak kosztu screenshotĂłw w pomiarze. 1000/wall_frame_ms jest
przepustowoĹ›ciÄ… poza ekranem, nie dowodem FPS na ekranie. GPU timers sÄ…
opĂłĹşnione. Liczniki widocznoĹ›ci/Ĺ›wiateĹ‚/drawĂłw sumujÄ… przebiegi renderera.
Analizator odrzuca wyniki z dropped/missing_resources i sprawdza culling.

GeometrySwarm: 100 tys. / milion rezydentnych instancji, cztery trĂłjkÄ…ty na
instancjÄ™, zielony materiaĹ‚, bez cieni/wiatru/tekstur/alpha overdraw. Jeden
wĹ‚aĹ›ciciel ECS, nie osobna encja/fizyka dla kaĹĽdej roĹ›liny. Widoczny jest wycinek
pola kamery. GeometrySwarmVisibilityClusters zachowuje wynik cullingu poprzez
konserwatywne zakresy po 128 instancji; przy wielu LOD lub SurfaceCast zachowuje
oryginalnÄ… Ĺ›cieĹĽkÄ™. DuĹĽe sceny nadal wymagajÄ… budĹĽetowania widocznej geometrii.

Publiczne API streamingu (wywoĹ‚uj na wÄ…tku wĹ‚aĹ›ciciela Scene):
  scene.LoadedContent().ConfigureStreaming({.maxPendingLoads=8,
      .maxOperationsPerFrame=256, .maxMillisecondsPerFrame=2.0F});
  const auto id = scene.LoadedContent().LoadAsync("/Game/Scenes/Cell.21kbscene", owner);
  // istniejÄ…cy Runtime.Update pompuje zadania; obsĹ‚uĹĽ Ready/Failed/Error
  const bool accepted = scene.LoadedContent().UnloadAsync(id);

Wczytywanie jest additive. Fizyczne pliki oraz zarejestrowane Scene/ScenePrefab
uĹĽywajÄ… przygotowania w tle, istniejÄ…cego bulk ECS oraz porcjowanych zmian.
Zarejestrowane ĹşrĂłdĹ‚a korzystajÄ… z istniejÄ…cego AssetManager async worker;
test pakietu usuwa luĹşny plik przed Ĺ‚adowaniem. Referencje rozwiÄ…zywane sÄ… po
utworzeniu wszystkich encji. Aktywacja i usuwanie sÄ… stopniowe, nie atomowe.
Unload obejmuje runtime-spawned descendants. BudĹĽet 2 ms jest miÄ™kki: sprawdzany
miÄ™dzy porcjami. Licz caĹ‚y Runtime.Update, nie tylko StreamingStats. Porcja
moĹĽe przekroczyÄ‡ 2 ms, a przebudowa hierarchii/pose cache ma wĹ‚asny koszt.
Anulowanie nie blokuje Update na I/O. Zniszczenie Scene doĹ‚Ä…cza jego zadania.
StreamFocus automatycznie uĹĽywa tej Ĺ›cieĹĽki i zachowuje hysteresis/priorytety.
Sceny PrefabPrivate nie przyjmujÄ… streamingu. Dotychczasowe Load jest synchroniczne.

Testy poprawnoĹ›ci:
  build\perf-release\engine\kb_engine_tests.exe scene-prefab scene-runtime engine-library
  build\perf-release\bin\kb_renderer_tests.exe
PeĹ‚ny engine suite uruchamiaj z bin/ w Ĺ›rodowisku MSVC/CMake/Ninja (native script
test kompiluje plugin). Renderer suite uruchamiaj z gĹ‚Ăłwnego katalogu repo,
gdyĹĽ czÄ™Ĺ›Ä‡ testĂłw odczytuje ĹşrĂłdĹ‚a shaderĂłw. Program testowy jest w bin/ obok
shaderĂłw. PeĹ‚ny renderer suite ma osobny komunikat skip WebGPU fallback przy
juĹĽ uruchomionym urzÄ…dzeniu; ten benchmark mierzy D3D11.

Wyniki z 30.09.2026: Results/comparison.csv oraz .json w nowym projekcie,
Results/Raport_po_poprawkach.html i surowe CSV. MaĹ‚a kopia comparison.csv jest
w repozytorium. Nie doĹ‚Ä…czaj wygenerowanych projektĂłw/binariĂłw do ĹşrĂłdeĹ‚ silnika.
Nie deklaruj gotowoĹ›ci gry klasy AAA na podstawie kostek i prostych trĂłjkÄ…tĂłw.

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
