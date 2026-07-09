# Audyt krytyczny Material Editora

Data audytu: 2026-07-09
Projekt: `H:\23kb\21kb-Engine`
Wzorzec porównawczy: `F:\ref\UnrealEngine`
Stan: bieżący dirty worktree, w tym nowy inline texture picker
Charakter audytu: read-only; audyt nie wprowadził zmian w kodzie produkcyjnym

## Werdykt

Material Editor w obecnym stanie nie jest gotowy do bezpiecznego użycia produkcyjnego ani do wydania. Występują błędy mogące:

- cofnąć na dysku inny materiał niż aktualnie otwarty;
- zapisać niepoprawny materiał i oznaczyć go jako czysty;
- zmienić inną właściwość niż wskazana kursorem;
- renderować inny wariant jakości lub shading path niż pokazuje UI;
- produkować inne wyniki w Forward, Deferred, Shadow i Preview;
- zgubić przypisania tekstur lub sampler state po zapisie/migracji;
- pozostawić mutacje bez wpisu Undo albo zakleszczyć cały stos historii.

Najpierw należy naprawić integralność danych i dokumentów, następnie pipeline renderingu, a dopiero później ergonomię oraz funkcje dodatkowe.

## P0 — blokery wydania

### P0.1. Globalne Undo/Redo jest niebezpieczne między dokumentami

Historia poleceń jest globalna i nie jest partycjonowana ani czyszczona przy zmianie materiału.

Minimalny scenariusz:

1. Otworzyć materiał A.
2. Zmienić graf i wykonać Save.
3. Otworzyć materiał B.
4. Wykonać `Ctrl+Z`.
5. Polecenie dyskowe może cofnąć plik A, ale kod odświeży aktualnie otwarty B.
6. Kolejne Undo może trafić na working-copy command A. Command odmawia wykonania, wraca na szczyt stosu i blokuje wszystkie starsze wpisy.

Revert również nie usuwa historii dokumentu, więc Redo może wskrzesić odrzucone zmiany.

Źródła:

- `sources/editor/src/commands/EditorCommandStack.cpp:34-48`
- `sources/editor/src/scene/material/EditorMaterialAssetEditCommand.cpp:503-520`
- `sources/editor/src/scene/material/EditorMaterialAssetEditCommand.cpp:677-692`
- `sources/editor/src/scene/EditorSceneContext.cpp:1695-1721`
- `sources/editor/src/scene/EditorSceneContext.cpp:2559-2654`
- `sources/editor/src/scene/EditorSceneContext.cpp:5827-5857`

### P0.2. Save zapisuje przed walidacją

`CopyWorkingMaterialToSource` wykonuje zapis na dysk, ustawia working copy i wywołuje `MarkSaved()`, a dopiero potem uruchamia `ValidateMaterialEditorAsset`.

Skutki:

- niepoprawny graf trafia do pliku;
- dokument zostaje oznaczony jako czysty;
- Save zwraca `false`, mimo że zapis już nastąpił;
- kolejna próba Save może nie widzieć nic do zapisania;
- zachowanie różni się od ścieżki Material Instance, która waliduje przed zapisem.

Źródło: `sources/editor/src/scene/EditorSceneContext.cpp:7064-7110`.

Wymagane rozwiązanie: walidacja kompletnego dokumentu przed zapisem, zapis atomowy do pliku tymczasowego i dopiero po sukcesie aktualizacja clean snapshot/history.

### P0.3. Panel Details ma inne położenie pól w rendererze i hit-teście

Renderer zawsze dodaje Search i przesuwa pozycję pionową. Następnie może rysować Parent Chain, Instance Overrides, Static Switches, Layer Stack oraz Find. Hit-test zaczyna jednak od stałego `top + 34` i nie uwzględnia tych sekcji.

Skutki:

- klik widocznego pola może edytować następne pole;
- picker może otworzyć się dla innego `stableId`;
- klik może nie trafić w Details i przejść do grafu znajdującego się pod nieprzezroczystym panelem;
- ukryty node może zostać zaznaczony, przeciągnięty albo rozłączony.

Źródła:

- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:3360-3542`
- `sources/editor/src/private/rendering/MaterialEditorPanelRenderer.hpp:927-1149`
- `sources/editor/src/app/pointer/EditorLeftButtonDownRouter.cpp:469-591`

Istniejące testy w `EditorMaterialAssetAuthoringTests.cpp:4355-4372` utrwalają starszą geometrię zamiast porównywać renderowany layout z hit-testem.

### P0.4. Cube, Texture3D/Volume i Texture2DArray nie istnieją w realnym runtime

Model grafu oraz reflection niosą `dimension`, ale runtime tekstur obsługuje wyłącznie zasób 2D:

- `RenderTextureAssetData` zawiera tylko `width`, `height` i `rgba8`;
- resource ensurer zawsze wywołuje `RegisterTexture2D`;
- registry tworzy `bgfx::createTexture2D`;
- submit ignoruje dimension i wiąże ten sam uchwyt z samplerem Cube/3D/Array;
- fallback textures także są 2D.

Skutek: shader oczekujący `samplerCube`, `sampler3D` albo `sampler2DArray` otrzymuje zasób 2D. Wynikiem może być biały/czarny fallback, invalid binding albo zachowanie zależne od backendu.

Źródła:

- `sources/renderer/include/kb/render/resources/RenderTextureAssetLoader.hpp:15-20`
- `sources/renderer/src/runtime/RuntimeTextureResourceEnsurer.cpp:78-89`
- `sources/renderer/src/resources/RenderResourceRegistry.cpp:165-171`
- `sources/renderer/src/scene/submit/SceneMeshPassResources.cpp:547-580`

Testy GPU tworzą takie zasoby ręcznie i omijają produkcyjny asset/resource pipeline: `sources/renderer/tests/GraphForwardGpuRenderTests.cpp:2839-2864`.

### P0.5. ShadowDepth gubi WPO, displacement i alpha grafu

Cook celowo nie generuje `vs.bin` dla passu ShadowDepth. Jednocześnie binding ustawia `requiresGeneratedVertexShader` dla materiału z WPO/custom UV/displacement. Loader nie znajduje `vs.bin`, odrzuca graph shadow program i używa builtin fallback.

Skutki:

- cień nie odpowiada zdeformowanej geometrii;
- masked/graph alpha nie jest oceniane przez graph shadow shader;
- foliage, wind i vertex displacement mogą mieć statyczny, pełny cień.

Źródła:

- `sources/renderer/src/resources/RenderMaterialGraphShaderArtifact.cpp:617-625`
- `sources/renderer/src/resources/RenderMaterialGraphProgramBindingBuilder.cpp:220-223`
- `sources/renderer/src/scene/submit/SceneMeshPassResources.cpp:383-454`

Wzorzec UE: `F:\ref\UnrealEngine\Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp:538-562` — default/position-only shader jest dozwolony tylko wtedy, gdy materiał nie modyfikuje pozycji.

### P0.6. Deferred GBuffer gubi emissive, specular i shading model

GBuffer writer zapisuje tylko:

- base color;
- normal;
- metallic;
- roughness;
- occlusion.

Nie zapisuje emissive, jawnego outputu specular ani identyfikatora shading modelu. Gałąź Unlit jest umieszczona dopiero po gałęzi GBuffer, więc dla deferred jest nieosiągalna. `fs_deferred_lighting.sc` zawsze oblicza PBR i nie ma kanału na graph specular.

Skutki:

- Unlit staje się oświetlany;
- emissive znika;
- graph specular znika i wraca stałe zachowanie deferred PBR;
- wynik materiału różni się pomiędzy Forward i Deferred;
- UI i dokumentacja błędnie deklarują pełną ścieżkę Production dla Deferred.

Źródła:

- `sources/renderer/src/resources/RenderMaterialGraphShaderArtifact.cpp:304-321`
- `sources/renderer/src/resources/RenderMaterialGraphShaderArtifact.cpp:335-340`
- `sources/renderer/shaders/fs_deferred_lighting.sc:163-195`
- `docs/material_graph.md:41-47`

### P0.7. Różne blend/shading variants mogą nadpisywać ten sam plik binarny

`sourceHash` jest liczony z wygenerowanego `shader.source` przed uwzględnieniem części reflection używanej przez wrapper. Blend mode i shading model mogą zmienić wrapper bez zmiany `sourceHash`. Cook zapisuje jednak wszystkie takie warianty do:

`graph_<sourceHash>/<pass>/<backend>/fs.bin`.

Opaque/Masked albo DefaultLit/Unlit o tym samym źródle mogą więc nadpisać sobie `fs.bin`. Runtime tworzy różne variant/pipeline keys, lecz loader obu kluczy czyta tę samą ścieżkę. Ostatni ugotowany wariant wygrywa dla wszystkich.

Źródła:

- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:6670-6672`
- `sources/renderer/src/resources/RenderMaterialGraphShaderArtifact.cpp:525-550`
- `sources/renderer/src/scene/submit/SceneMeshPassResources.cpp:373-395`

## P1 — krytyczne błędy pipeline'u i danych

### P1.1. Cook variants kolidują, ponieważ kluczem jest tylko AssetId

`pending_`, `latest_` i `lastGood_` są mapami kluczowanymi wyłącznie `uint64_t assetId`. Nie uwzględniają:

- quality level;
- shading path;
- feature level;
- shader stage;
- preview kontra scene;
- backend/pass.

Preview request używa wybranej jakości i hardcoded Forward. Scene request używa High oraz ścieżki projektu. Dwa requesty tego samego materiału wzajemnie się nadpisują w debounce queue. Banner `Ready` opisuje więc ostatni dowolny wariant.

Źródła:

- `sources/editor/src/private/scene/material_preview/EditorMaterialGraphCookService.hpp:159-161`
- `sources/editor/src/scene/material_preview/EditorMaterialGraphCookService.cpp:409-435`
- `sources/editor/src/scene/EditorSceneContext.cpp:302-340`
- `sources/editor/src/scene/EditorSceneContext.cpp:6946-7040`
- `sources/editor/src/scene/EditorSceneContext.cpp:7244-7271`

### P1.2. Preview Quality nie steruje faktycznie renderowanym wariantem

Editor cook i preview rebuild otrzymują wybraną jakość. Właściwy `Renderer` przed submit ustawia jednak nowy runtime build context zawierający tylko shading path. Quality pozostaje domyślne `High`.

Skutek: QualitySwitch Low/Medium może nadal renderować High, fallback albo last-good o innym source hash.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:2900-2908`
- `sources/editor/src/scene/material_preview/EditorMaterialPreviewScene.cpp:340-344`
- `sources/renderer/src/Renderer.cpp:611-619`
- `sources/renderer/include/kb/render/resources/RenderMaterialGraphDocument.hpp:495-501`

### P1.3. Cache cooka może zwracać stare binaria

Wrapper dołącza wspólne biblioteki shaderowe i custom includes, ale produkcyjny cook service nie przekazuje `dependencyFiles`. Do klucza nie trafiają również wersja/binarium `shaderc` ani komplet include directories.

Repro: ugotować materiał, zmienić `pbr_graph_forward.sh` lub zawartość custom include, ponowić cook bez zmiany grafu. Możliwy jest cache hit starego `fs.bin`.

Źródła:

- `sources/editor/src/scene/material_preview/EditorMaterialGraphCookService.cpp:239-248`
- `sources/renderer/src/resources/RenderMaterialGraphShaderArtifact.cpp:239-242`
- `sources/renderer/src/resources/RenderMaterialGraphShaderArtifact.cpp:490-550`

### P1.4. Editor raportuje sukces kompilacji bez sprawdzenia wyniku kompilacji

`RefreshGraphDiagnostics` ustawia:

- `compileSucceeded = valid`;
- `hasGpuProgram = valid`;
- `fallbackApplied = true`.

`valid` oznacza wyłącznie brak błędu walidatora. Wynik `compile.Succeeded()` i rzeczywisty cook GPU są ignorowane. Build context dodatkowo używa `materialTypeAssetId` jako asset ID materiału.

Źródło: `sources/editor/src/private/scene/material/MaterialEditorState.hpp:5544-5591`.

### P1.5. Diagnostyka kompilatora jest gubiona lub nadpisywana

- compile diagnostics nie są dodawane do głównej listy Diagnostics;
- async cook errors są wypisywane tylko do konsoli;
- banner mówi `Failed ... see diagnostics`, chociaż panel nie zawiera błędu;
- `SetDiagnostics` zastępuje całą listę i czyści graph markers;
- schema/type diagnostics podczas Open mogą nadpisać błędy grafu.

Źródła:

- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:2931-2935`
- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:5544-5591`
- `sources/editor/src/scene/EditorSceneContext.cpp:2633-2650`
- `sources/editor/src/scene/EditorSceneContext.cpp:2991-3019`
- `sources/editor/src/scene/material_preview/EditorMaterialGraphCookService.cpp:306-323`

### P1.6. Domyślne wartości dynamicznych parametrów zerują się

Reflection dla `ParameterScalar`, `ParameterVector` i `ParameterColor` nie parsuje `defaultValueHint`. `defaultValue` pozostaje zerowe, a binding używa tych zer przy braku jawnego override.

Przykład: ParameterColor z domyślnym białym kolorem renderuje czarny.

Źródła:

- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:6141-6149`
- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:6224-6232`
- `sources/renderer/src/resources/RenderMaterialGraphProgramBindingBuilder.cpp:245-280`

### P1.7. Sampler state nie jest serializowany

`samplerState` istnieje w modelu i wpływa na runtime flags, lecz writer nie zapisuje go w `graphParameter`, a parser go nie odczytuje.

Skutek: Point+Clamp po Save/Reload wraca do Linear+Repeat.

Źródła:

- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:5558-5570`
- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:6243-6285`
- `sources/renderer/src/resources/RenderMaterialGraphFieldParser.cpp:462-475`

### P1.8. Migracja Material Type usuwa wszystkie przypisania tekstur grafu

Schema refresh iteruje tylko `schema.parameters`, ignorując `schema.textureSlots`. Teksturowe `graphParameterValues` zostają uznane za nieznane i skasowane.

Źródła:

- `sources/renderer/src/resources/RenderMaterialAssetLoader.cpp:338-376`
- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:8834-8847`

### P1.9. `sourceGraph` jest martwą referencją i tworzy dwie rozjeżdżające się prawdy

`CreateMaterialFromGraph` kopiuje cały graf do `.kbmat`, a następnie zapisuje tylko ID/path źródła. Runtime i cook zawsze kompilują inline `material.graph`.

Skutki:

- zmiana `.kbmaterialgraph` nie aktualizuje `.kbmat`;
- edycja `.kbmat` nie aktualizuje source graph ani wygenerowanego Material Type;
- dependency hash może wywołać reload po zmianie source graph, ale runtime nadal skompiluje starą kopię inline;
- raw graph asset nie ma normalnej komendy Open ani obsługi double-click;
- `Open Graph` tylko zaznacza asset w browserze.

Źródła:

- `sources/editor/src/scene/material/EditorMaterialAssetAuthoring.cpp:282-342`
- `sources/editor/src/scene/EditorSceneContext.cpp:2657-2681`
- `sources/editor/src/app/EditorAssetBrowserDoubleClickHandler.cpp:61-78`
- `sources/editor/src/assets/EditorAssetBrowserStateContext.cpp:16-31`
- `sources/renderer/src/runtime/RuntimeMaterialResolver.cpp:1952-1958`

### P1.10. Walidacja produkcyjna zawsze używa ścieżki Forward

Walidator potrafi odrzucić niedozwolone w deferred SceneColor/SceneDepth/DepthFade dla opaque/masked. `BuildRenderMaterialGraphIr` wywołuje jednak `ValidateRenderMaterialGraphDocument(graph)` bez przekazania ścieżki z build context, więc używana jest domyślna `GpuForward`.

Źródła:

- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:6020-6025`
- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:6839-6877`
- `sources/renderer/include/kb/render/resources/RenderMaterialGraphDocument.hpp:738-740`

### P1.11. Brakuje walidacji parametrów oraz kardynalności grafu

Nie są wystarczająco walidowane:

- duplikaty `graphParameterValue.stableId`;
- zgodność declared type z typem parametru;
- dokładnie jeden MaterialOutput;
- maksymalnie jeden link na input;
- kolejność i jednoznaczność linków.

Binding bierze pierwszy parametr o stable ID, a compiler pierwszy link/output. Zmiana kolejności wpisów w pliku może zmienić wynik materiału.

Źródła:

- `sources/renderer/src/resources/RenderMaterialAssetParser.cpp:87-134`
- `sources/renderer/src/resources/RenderMaterialGraphProgramBindingBuilder.cpp:157-165`
- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:892-901`
- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:6116-6121`

### P1.12. Programy GPU przeciekają po kolejnych rewizjach materiału

`MaterialProgramRegistry` ma `Release`, `Reload` i `BeginFrame`, ale produkcyjny `SceneMeshPassResources` używa tylko `Find`/`Acquire` oraz shutdown.

Skutki:

- każdy nowy source hash/pass zostaje w pamięci do zamknięcia renderera;
- długie sesje edytora akumulują programy i uniform maps;
- recook tego samego key po zmianie include może nadal zwracać stary live program.

Źródła:

- `sources/renderer/src/scene/submit/SceneMeshPassResources.cpp:429-447`
- `sources/renderer/src/MaterialProgramRegistry.cpp:127-275`

### P1.13. Node Preview nie ma osobnego poprawnego cooka i nie izoluje węzła

Builder kopiuje cały graf i podmienia tylko link do `MaterialOutput.baseColor`. Pozostają:

- pozostałe output links;
- opacity/alpha/WPO;
- niepowiązane błędy i cykle;
- wszystkie inne node'y.

Zmodyfikowany dokument ma inny source hash, ale cook service nadal otrzymuje oryginalny working copy. GPU node preview nie ma więc odpowiadającego artefaktu i zależy od CPU/builtin/last-good fallback.

Źródła:

- `sources/editor/src/private/scene/material_preview/EditorMaterialNodePreviewBuilder.hpp:18-51`
- `sources/editor/src/scene/EditorSceneContext.cpp:2845-2860`
- `sources/editor/src/scene/EditorSceneContext.cpp:2934-2944`

### P1.14. Failure policy/last-good jest niespójne z rzeczywistym runtime

Stan UI jest wyznaczany osobnym helperem, ale produkcyjny runtime nie respektuje w pełni deklarowanej polityki. Ensurer utrzymuje cached last-good niezależnie od policy, a UI może jednocześnie raportować `UsingGpuGraph` lub `fallbackApplied` bez rzeczywistego programu.

Źródła:

- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:8956-9005`
- `sources/renderer/src/runtime/RuntimeMaterialResourceEnsurer.cpp:179-189`
- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:5583-5591`

### P1.15. Preview hash pomija zależności funkcji i parameter collections

Hash working-copy preview uwzględnia dokument oraz zależności tekstur. Nie uwzględnia content hash Material Function ani Material Parameter Collection. Zmiana ich zawartości bez zmiany ID może nie przebudować preview. Dla instancji hash obejmuje tylko bezpośredniego parenta, nie pełny łańcuch.

Źródło: `sources/editor/src/scene/material_preview/EditorMaterialPreviewScene.cpp:60-142`.

### P1.16. Tymczasowe pliki preview kolidują między procesami/projektami

Ścieżki w globalnym temp zawierają tylko AssetId. Dwie instancje edytora lub dwa projekty z takim samym ID mogą nadpisać albo usunąć sobie pliki.

Źródła:

- `sources/editor/src/scene/material_preview/EditorMaterialPreviewScene.cpp:101-103`
- `sources/editor/src/scene/EditorSceneContext.cpp:343-345`

### P1.17. MPC defaults nadpisują runtime overrides i pozostawiają duchy parametrów

Przy rozwiązywaniu materiału globalny Material Parameter Collection store ponownie ładuje wartości domyślne. `LoadDefaults` wykonuje `SetValue` dla każdego defaultu, przez co może nadpisać aktywny runtime override. Nie usuwa też wartości parametrów, które zniknęły ze schema/assetu.

Repro:

1. Ustawić runtime MPC override.
2. Rozwiązać lub przeładować materiał używający collection.
3. Override wraca do defaultu.
4. Usunięty parametr może nadal być rozwiązywany z globalnego store.

Źródła:

- `sources/renderer/src/runtime/RuntimeMaterialResolver.cpp:323-363`
- `sources/renderer/src/resources/RenderMaterialParameterCollection.cpp:447-481`

## P1 — krytyczne błędy canvasu i interakcji

### P1.18. `ParameterVector` ma wire anchor poza widocznym pinem

Adapter ustawia wysokość 72 i trzy value fields. Canvas liczy pojedynczy output z użyciem liczby value fields, umieszczając anchor na `y=76`, czyli poza node'em. Renderer rysuje pin na środku body, około `y=51`.

Skutek: przewód zaczyna się 25 world px od widocznego pinu.

Źródła:

- `sources/editor/src/rendering/material_graph/MaterialGraphCanvasDocumentAdapter.cpp:292-500`
- `sources/editor/src/rendering/material_graph/MaterialGraphCanvas.cpp:247-250`
- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:541-545`

### P1.19. Asymetryczne CustomCode/MaterialFunctionCall mają różne render/hit/wire anchors

Adapter i Canvas układają lane według `max(inputs, outputs)`. Renderer centruje widoczne inputy według samej liczby inputów.

Repro: 1 input i 4 outputs. Hit/wire input wypada około `y=52`, a widoczny pin około `y=88` — rozjazd około 36 px.

Odwrotna asymetria także jest błędna: przy 4 inputach i 1 outpucie Canvas wybiera środkowy rząd, natomiast renderer centruje pojedynczy output w body. Widoczny output i anchor różnią się o około 12 px.

Źródła:

- `sources/editor/src/rendering/material_graph/MaterialGraphCanvasDocumentAdapter.cpp:568-581`
- `sources/editor/src/rendering/material_graph/MaterialGraphCanvas.cpp:241-246`
- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:497-545`

### P1.20. Hit-zony sąsiednich pinów nakładają się i wybierają pierwszy pin

Pionowy half-band ma około 15,6 px przy odstępie rzędów 24 px. Strefy zawsze się pokrywają, a pętla zwraca pierwszy pasujący pin bez wyboru najbliższego. Jednostronne input/output dodatkowo przejmują cały poziomy rząd.

Skutek: klik bliżej drugiego pinu może połączyć pierwszy kanał, np. Base Color zamiast Normal.

Źródło: `sources/editor/src/rendering/material_graph/MaterialGraphCanvas.cpp:289-380`.

### P1.21. Piny są aktywne poza viewportem

`HitTestPin`/`GraphPinAt` nie sprawdza obszaru graph canvas. Left-button-up testuje pin przed sprawdzeniem viewportu. Wire można zakończyć na niewidocznym pinie znajdującym się pod sąsiednim panelem albo toolbarem.

Źródła:

- `sources/editor/src/rendering/material_graph/MaterialGraphCanvas.cpp:289-400`
- `sources/editor/src/private/rendering/MaterialEditorPanelRenderer.hpp:2576`
- `sources/editor/src/app/pointer/EditorLeftButtonUpRouter.cpp:120-143`

### P1.22. Cancel rewire usuwa oryginalny link

Rozpoczęcie rewire natychmiast odłącza istniejący link. Escape, drop outside lub incompatible target wywołuje Cancel, który commituję transakcję zamiast ją wycofać.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:4634-4684`
- `sources/editor/src/app/pointer/EditorLeftButtonUpRouter.cpp:120-143`
- `sources/editor/src/app/EditorWindowMessageRouter.cpp:98-116`

Wzorzec UE: `F:\ref\UnrealEngine\Engine\Source\Editor\GraphEditor\Private\DragConnection.cpp:348-369` — transakcja relink powstaje dopiero dla udanego dropu.

### P1.23. Drag-create z filtered menu gubi autoconnect

Wire upuszczony na puste miejsce otwiera menu przefiltrowane pod pending connection. Zwykły click używa `AddMaterialGraphNodeForPendingConnection`, ale drag item najpierw otwiera zwykłe context menu, resetuje filtr, a potem dodaje node bez połączenia.

Źródła:

- `sources/editor/src/app/pointer/EditorLeftButtonUpRouter.cpp:112-147`
- `sources/editor/src/app/EditorPointerDropHandler.cpp:143-182`
- `sources/editor/src/scene/EditorSceneContext.cpp:4725-4748`
- `sources/editor/src/scene/EditorSceneContext.cpp:5123-5142`

### P1.24. Transient state przeżywa zmianę lub zamknięcie materiału

Open/Close nie resetuje wszystkich stanów:

- texture picker;
- context menu;
- pending pin connection;
- focus;
- pan/drag;

Node/comment selection, Find, enum dropdown i rename są resetowane przez `MaterialEditorState::Open/Close`; problem dotyczy powyższego context-level state oraz globalnego widoku pan/zoom.

Repro: otworzyć picker lub rozpocząć wire w A, zamknąć/przełączyć na B. Overlay albo pending operation nadal odnosi się do A.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:2559-2654`
- `sources/editor/src/scene/EditorSceneContext.cpp:2711-2722`
- `sources/editor/src/scene/EditorSceneContext.cpp:4900-4927`
- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:2654-2715`

### P1.25. Utrata capture zostawia mutację bez Undo

Comment drag modyfikuje working copy na żywo, a command powstaje dopiero w `EndMaterialGraphCommentDrag`. `WM_CANCELMODE` i `WM_CAPTURECHANGED` nie kończą ani nie rollbackują material graph dragów.

Skutek: Alt+Tab/capture loss może zostawić zmieniony dokument bez wpisu historii i z wiszącym stanem drag.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:3542-3588`
- `sources/editor/src/app/EditorWindowMessageRouter.cpp:430-474`

### P1.26. Comment group drag może „zabierać” node'y napotkane po drodze

`MoveGraphCommentGroup` przy każdym mouse move ponownie oblicza, które node'y są aktualnie wewnątrz komentarza. Kiedy komentarz przejedzie nad wcześniej zewnętrznym node'em, następny event uzna go za członka grupy i zacznie go przenosić.

Źródło: `sources/editor/src/private/scene/material/MaterialEditorState.hpp:2439-2467`.

### P1.27. RMB niszczy multi-selection przed rozstrzygnięciem menu/pan

Right-button-down natychmiast wybiera pojedynczy node albo czyści selection. Dzieje się to jeszcze zanim wiadomo, czy użytkownik chciał menu, czy pan.

Skutki:

- RMB na wybranym node redukuje A+B do B;
- RMB-pan z tła kasuje selection;
- minimalny ruch jednego piksela blokuje menu.

Źródła:

- `sources/editor/src/app/pointer/EditorRightButtonDownRouter.cpp:132-153`
- `sources/editor/src/scene/EditorSceneContext.cpp:3679-3691`
- `sources/editor/src/app/EditorWindowPointerHandler.cpp:151-175`

UE zachowuje multi-selection, jeśli node pod kursorem już należy do selection: `F:\ref\UnrealEngine\Engine\Source\Editor\GraphEditor\Private\SGraphPanel.cpp:1528-1538`.

### P1.28. Context menu komentarza może usunąć inny komentarz

RMB nie wykonuje `GraphCommentAt`. `ClearNodeSelection` ma early return, kiedy node selection jest puste, i nie czyści `selectedCommentId`.

Repro:

1. Zaznaczyć komentarz D.
2. Otworzyć RMB nad komentarzem C.
3. D pozostaje zaznaczony.
4. `Delete Selected` usuwa D.

Źródła:

- `sources/editor/src/app/pointer/EditorRightButtonDownRouter.cpp:132-153`
- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:2766-2790`
- `sources/editor/src/scene/EditorSceneContext.cpp:5471-5472`

### P1.29. Alt-click może skasować niewidoczny wire pod node'em

Hit-test linków nie uwzględnia occlusion przez node/comment. Router sprawdza Alt+link przed hit-testem node'a. Przewód rysowany przed opaque nodes może więc zostać usunięty po Alt-click na samym node.

Źródła:

- `sources/editor/src/rendering/material_graph/MaterialGraphCanvas.cpp:408-431`
- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:3075-3129`
- `sources/editor/src/app/pointer/EditorLeftButtonDownRouter.cpp:606-617`

### P1.30. Collapsed composite jest nieodwracalny

Renderer ukrywa internal nodes/links collapsed composite, ale produkcyjny router nie ma hit-testu ani dostępnej komendy ponownego rozwinięcia. Wczytany `collapsed=true` composite staje się martwym obiektem.

Źródła:

- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:3063-3074`
- `sources/editor/src/private/rendering/MaterialEditorPanelRenderer.hpp:2362-2414`

### P1.31. Rename node'a ginie bez ostrzeżenia

Pending text committer nie obsługuje node rename. Zmiana selection wykonuje Cancel, a dirty-close guard nie uwzględnia rename buffer.

Skutek: F2, wpisanie nazwy, klik innego node'a/Save/Close — tekst znika bez promptu.

Źródła:

- `sources/editor/src/app/EditorPendingTextEditCommitter.cpp:11-17`
- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:2766-2782`
- `sources/editor/src/scene/EditorSceneContext.cpp:2724-2735`
- `sources/editor/src/scene/EditorSceneContext.cpp:4301-4347`

### P1.32. Details nie ma scrolla i ukrywa legalne parametry

Renderer twardo ogranicza widoczne parametry i tekstury. Wheel w Material Editorze obsługuje picker/menu albo zoom canvas, nie Details scroll. Search jest narysowany, lecz klik nie ustawia focusu — działa praktycznie tylko przez `Ctrl+F`.

Źródła:

- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:3467-3470`
- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:3604-3627`
- `sources/editor/src/app/EditorMouseWheelRouter.cpp:83-118`

### P1.33. Opaque overlay nie blokuje inputu canvasu

Graph canvas obejmuje całe body, podczas gdy preview/details/diagnostics są na nim tylko narysowane. Blank miejsca overlay przepuszczają kliknięcia do grafu. Możliwe jest zaznaczenie, drag albo disconnect elementu znajdującego się pod panelem.

Źródła:

- `sources/editor/src/private/rendering/MaterialEditorPanelRenderer.hpp:664-689`
- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:3713-3756`

### P1.34. Inline texture picker ma regresje dostępności

Bieżący picker:

- nie ma kafla `None/Clear`, chociaż model wspiera wyczyszczenie asset ID;
- Accept wymaga wybranego valid assetu;
- zawsze używa czterech kolumn po 158 px;
- modal może zwęzić się do 240 px;
- obcięte kolumny nie mają horizontal scroll;
- stan pickera może przeżyć zmianę materiału.

Źródła:

- `sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp:2680-3033`
- `sources/editor/src/scene/EditorSceneContext.cpp:4050-4133`
- `sources/editor/src/scene/EditorSceneContext.cpp:4900-4927`

Poprzedni Win32 picker miał jawny wiersz `None/Clear`: `sources/editor/src/platform/win32/EditorMeshAssetPickerDialog.cpp:939-958`.

### P1.35. Texture type i color space są zgadywane z nazwy/path

Dimension picker szuka słów `cube`, `volume`, `3d`, `array` w type/import/name/path. Slot validation zgaduje normal/ORM/albedo/emissive z nazwy pliku i może twardo odrzucić asset.

Przykłady błędów:

- zwykła tekstura `cube_diffuse` zostaje uznana za cubemapę;
- prawidłowy cube bez odpowiedniego tokenu może być niewidoczny;
- nazwa jest traktowana jak prawdziwa informacja o color space;
- unknown jest akceptowane i otrzymuje oczekiwany color space bez sprawdzenia metadanych.

Źródła:

- `sources/editor/src/private/platform/win32/EditorMaterialAssetPickerDialog.hpp:66-86`
- `sources/editor/src/private/scene/material/EditorMaterialTextureSlotValidation.hpp:77-128`
- `sources/editor/src/scene/EditorSceneContext.cpp:5730-5745`

### P1.36. Normal texture metadata zależy od jednej dokładnej topologii

Sanitizer rozpoznaje normal mapę tylko w łańcuchu:

`TextureSample.color -> NormalUnpack.color -> MaterialOutput.normal`.

Jeżeli normal przechodzi przez Multiply, Blend, Reroute, Function itp., pozostaje domyślnym `baseColor/sRGB`.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:692-778`
- `sources/editor/src/scene/EditorSceneContext.cpp:4102-4108`

### P1.37. Undo części editów gubi multi-selection i primary node

Wiele wywołań `RecordMaterialGraphWorkingCopyEdit` przekazuje tylko `beforeSelectedNodeId`, bez pełnego `beforeSelectedNodeIds`. Undo odtwarza wtedy wyłącznie primary zamiast wcześniejszego multi-selection. Command dodatkowo ustawia primary jako `selectedNodeIds.back()`, chociaż poprawny primary nie musi być ostatnim elementem wektora.

Skutki:

- Undo zmienia selection niezależnie od właściwej edycji;
- multi-selection redukuje się do jednego node'a;
- primary/focus po Undo może wskazywać inny node niż przed operacją.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:6847-6890`
- `sources/editor/src/scene/material/EditorMaterialAssetEditCommand.cpp:677-691`
- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:3025-3036`

## P2 — istotne nieprawidłowości i udziwnienia

### P2.1. Ctrl-marquee działa jak Shift-add

Ctrl i Shift są spłaszczane do jednego `additive=true`; End wykonuje union. Canvas ma osobny poprawny mechanizm toggle, ale produkcyjny router go nie używa. Alt-remove nie istnieje.

Źródła:

- `sources/editor/src/app/pointer/EditorLeftButtonDownRouter.cpp:680-682`
- `sources/editor/src/scene/EditorSceneContext.cpp:3622-3642`
- `sources/editor/src/rendering/material_graph/MaterialGraphCanvas.cpp:708-740`

### P2.2. Brak dead-zone i snap-to-grid

Każdy niezerowy ruch tworzy realny edit lub oznacza RMB jako pan. Jeden piksel drżenia może przesunąć node i dodać Undo albo zablokować menu. Widoczny grid nie ma snapowania.

### P2.3. Pan/zoom jest globalny dla wszystkich materiałów

Stan nie jest kluczowany AssetId ani resetowany przy Open. Materiał B może otworzyć się poza ekranem po pracy nad A.

Źródła:

- `sources/editor/src/private/scene/EditorSceneContext.hpp:683`
- `sources/editor/src/scene/EditorSceneContext.cpp:2559-2654`

### P2.4. `Frame Selected` nie zawsze mieści selection

Zoom jest ograniczony do około `0.45-1.60`, również podczas fitowania. Duży graf wymagający np. 0.18 zostanie tylko wycentrowany, nie zmieści się w viewport.

Źródła: `sources/editor/src/scene/EditorSceneContext.cpp:3127,3286`.

### P2.5. Produkcja i testy używają dwóch implementacji interakcji canvasu

Produkcja tworzy `MaterialGraphCanvas` głównie jako helper geometrii/hit-testu. Faktyczne pointer, marquee, clipboard i drag są ponownie zaimplementowane w routerach oraz `EditorSceneContext`.

Skutek: testy `MaterialGraphCanvas::OnPointerDown/Move/Up` mogą przechodzić, mimo że produkcyjne zachowanie jest błędne.

Źródła:

- `sources/editor/src/private/rendering/MaterialEditorPanelRenderer.hpp:2417,2576`
- `sources/editor/tests/EditorMaterialGraphCanvasTests.cpp`

### P2.6. Edycje organizacyjne wywołują runtime preview i cook

Runtime content hash zeruje tylko pozycje node'ów. Comments, composites i inne editor-only metadata nadal wpływają na hash. Każdy `RecordMaterialGraphWorkingCopyEdit` uruchamia synchronizację preview i oba requesty cooka.

Skutki:

- rename/move/resize komentarza może przeładować asset i requestować shader cook;
- artifact cache key hashuje pełny zapis dokumentu, więc layout kasuje cache;
- brak rozdzielenia transakcji editor-only od zmian semantyki shadera.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:290-299`
- `sources/editor/src/scene/EditorSceneContext.cpp:6847-6908`
- `sources/renderer/src/resources/RenderMaterialGraphDocument.cpp:5834-5845`

### P2.7. Material Stats są pseudo-statystyką

Instruction estimate zlicza linie źródła zawierające `=`, `return` albo texture call. Nie jest to wynik skompilowanego GPU shadera. Model pokazuje tylko Base i Shadow, bez prawdziwego GBuffer. Shadow row jest uznany za graph tylko przy vertex outputs, pomijając masked alpha fragment path.

Źródła:

- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:5064-5105`
- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:5182-5241`

### P2.8. Ostrzeżenie o `2^N` permutations nie opisuje realnego cooka

Compiler bake'uje aktualny selector/context. Cook service kompiluje jeden graph context i listę passów; nie generuje pełnego zestawu quality/feature/static variants. Raportowana liczba wariantów nie odpowiada realnej liczbie artefaktów.

### P2.9. Pola numeryczne akceptują wartości niefinitywne

`ParsePlainFloat` używa `strtof`, ale nie sprawdza `std::isfinite`. `NaN`/`Inf` mogą przejść do dokumentu; `std::clamp` nie naprawia NaN. Parser stałych nie weryfikuje też trailing tokens po oczekiwanych komponentach.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:582-595`
- `sources/editor/src/private/scene/material/MaterialEditorState.hpp:3196-3235`

### P2.10. Martwe API sliderów i usunięty feedback kompatybilności

W bieżącym dirty diff:

- `BeginMaterialGraphConstantSliderDrag` i `DragMaterialGraphConstantSlider` zawsze zwracają `false`;
- routery nadal je wywołują;
- enum i helper `MaterialEditorGraphPinDragState` pozostały;
- renderer usunął pierścienie Source/Compatible/Incompatible oraz kolor hover wire.

Źródła:

- `sources/editor/src/scene/EditorSceneContext.cpp:4361-4382`
- `sources/editor/src/private/rendering/MaterialEditorPanelRenderer.hpp:1152-1189`
- `sources/editor/src/app/pointer/EditorLeftButtonDownRouter.cpp:510-516,634-639`

### P2.11. Standalone graph save/load ma inne semantyki błędów niż materiał

- standalone graph loader może traktować warning/migration jako fatal;
- `SaveGraph` zapisuje bez atomowej wymiany;
- zachowanie różni się od loadera pełnego materiału.

Źródła: `sources/renderer/src/resources/RenderMaterialGraphAssetLoader.cpp:84-86,165-172`.

## Porównanie z Unreal Engine

Najważniejszą różnicą nie jest wygląd UI, lecz invarianty architektoniczne.

### Transakcje i połączenia

UE tworzy `FScopedTransaction` dla tworzenia node'ów, linków, usuwania linków i relinku. Relink jest commitowany dopiero po prawidłowym dropie.

- `F:\ref\UnrealEngine\Engine\Source\Editor\UnrealEd\Private\MaterialGraphSchema.cpp:68,304,744-779`
- `F:\ref\UnrealEngine\Engine\Source\Editor\GraphEditor\Private\DragConnection.cpp:348-369`

### Diagnostyka kompilacji

UE pobiera compile errors z realnych Material Resources, pokazuje platformę/quality, wskazuje failing expression i ostrzega przed Apply/Close.

- `F:\ref\UnrealEngine\Engine\Source\Editor\MaterialEditor\Private\MaterialEditor.cpp:2661-2728`
- `F:\ref\UnrealEngine\Engine\Source\Editor\MaterialEditor\Private\MaterialEditor.cpp:2939-3150`
- `F:\ref\UnrealEngine\Engine\Source\Editor\MaterialEditor\Private\MaterialEditor.cpp:3207-3266`

### Shadow

UE nie używa default/position-only shadow materialu, jeżeli materiał modyfikuje pozycję.

- `F:\ref\UnrealEngine\Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp:538-562`

### Typy tekstur

UE sprawdza rzeczywistą klasę i material type zasobu, np. TextureCube, Texture2DArray i VolumeTexture, zamiast tokenów w nazwie.

- `F:\ref\UnrealEngine\Engine\Source\Runtime\Engine\Private\Materials\MaterialExpressions.cpp:4509-4518`
- `F:\ref\UnrealEngine\Engine\Source\Runtime\Engine\Private\Materials\MaterialExpressions.cpp:4623-4632`

### Selection i RMB

UE zachowuje multi-selection, jeżeli node pod RMB już należy do selection, i stosuje dead-zone przed rozpoznaniem drag/pan.

- `F:\ref\UnrealEngine\Engine\Source\Editor\GraphEditor\Private\SGraphPanel.cpp:1528-1538`
- `F:\ref\UnrealEngine\Engine\Source\Editor\GraphEditor\Private\SNodePanel.cpp:987-1022`

## Weryfikacja testami

Zbudowane i uruchomione zostały:

- `kb_editor_tests` — PASS;
- `kb_editor_material_graph_canvas_tests` — PASS;
- `kb_renderer_tests` — PASS.

Przejście testów nie obala findings. Pokazuje, że testy omijają krytyczne ścieżki integracyjne.

Najważniejsze braki pokrycia:

- Undo/Redo pomiędzy dwoma materiałami;
- Save niepoprawnego grafu i stan dirty/clean;
- zgodność renderer rect = hit-test rect w Details;
- zgodność widoczny pin = Canvas pin center = wire endpoint;
- ParameterVector z realnym linkiem;
- dynamic node z `outputs > inputs`;
- sąsiednie/nakładające się hit-zony;
- cancel rewire i capture loss;
- RMB na multi-selection i komentarzach;
- occlusion wire przez node/comment;
- hit pinów poza viewportem;
- dwa równoczesne warianty cook tego samego AssetId;
- Preview Low/Medium w rzeczywistym Rendererze;
- sampler state Save/Reload round-trip;
- migracja schema z texture slots;
- ShadowDepth z WPO + masked alpha;
- Deferred Unlit/emissive;
- produkcyjny resource pipeline Cube/3D/Array;
- invalidacja cache po zmianie include albo shaderc.

## Zalecana kolejność napraw

### Etap 1 — integralność danych

1. Per-document history albo pełna partycja command stack według asset/document ID.
2. Walidacja przed zapisem i atomowy Save.
3. Poprawienie Details layout/hit-test i zablokowanie inputu pod overlay.
4. Centralny lifecycle reset/rollback wszystkich transient edit states.
5. Rewire jako preview-only do udanego dropu.

### Etap 2 — prawdziwy pipeline produkcyjny

1. Naprawić ShadowDepth WPO/alpha.
2. Rozszerzyć GBuffer o emissive i shading model albo wycofać deklarację pełnego Deferred Production.
3. Wprowadzić prawdziwe zasoby Cube/3D/Array albo ukryć/oznaczyć node'y jako Unsupported.
4. Kluczować cook i status pełnym variant key/context.
5. Propagować quality/shading path do RuntimeMaterialResolver.
6. Włączyć rzeczywiste dependency hashing i reload programów.

### Etap 3 — format dokumentu i schema

1. Serializować sampler state.
2. Zachowywać texture slots podczas schema refresh.
3. Parsować defaultValueHint do reflection defaults.
4. Walidować duplikaty, typy, output cardinality i link cardinality.
5. Wybrać jedną autorytatywną postać grafu: inline material albo source graph, nie obie bez synchronizacji.

### Etap 4 — jedna geometria i jedna implementacja inputu

1. Renderer, hit-test i wire anchors muszą korzystać z jednego layout modelu.
2. Usunąć pełnowierszowe/overlapping pin hit bands.
3. Skonsolidować produkcyjne routery z testowanym kontrolerem Canvas.
4. Dodać occlusion i viewport clipping do hit-testów.
5. Naprawić comments, composite, marquee, RMB, snap i per-document view state.

### Etap 5 — testy i deklaracje funkcji

1. Dodać testy integracyjne rzeczywistych routerów Win32 i lifecycle.
2. Dodać GPU/runtime tests przechodzące przez asset loader/resource ensurer.
3. Testować pełne variant matrix: quality, shading path, pass, backend.
4. Zaktualizować `docs/material_graph.md` i support matrix tak, aby `Production` oznaczało rzeczywiście działający authoring, cook, runtime i preview.
