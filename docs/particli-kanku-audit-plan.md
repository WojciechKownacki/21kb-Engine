# Particli — audyt Kanku i kanoniczny plan implementacji

Status: plan implementacyjny, bez implementacji produkcyjnej  
Repozytorium docelowe: H:\23kb\21kb-Engine, gałąź 1.0  
Stan bazowy audytu: 2daecc9f62535eec27f929beb71d64b8a025f21c  
Repozytorium referencyjne: E:\VerthEngineProd, wyłącznie odczyt  
Data audytu: 2026-08-13

## 1. Zakres, metoda i granice

Audyt objął rzeczywisty kod Kanku, jego hostowe powierzchnie edytora oraz aktualne punkty rozszerzeń 21kb. Nie uruchamiano generatorów, buildów, testów ani programów mogących zmienić E:\VerthEngineProd. W 21kb nie zmieniono kodu produkcyjnego; ten dokument jest jedynym artefaktem audytu.

Stan bezpieczeństwa: E:\VerthEngineProd miał podczas końcowej kontroli istniejące wcześniej zmiany i untracked files w obszarach KyuMesh/RenderRHI/Notatki. Audyt ich nie otwierał do edycji, nie formatował, nie generował i nie modyfikował. Odczyty dotyczyły Kanku oraz wskazanych hostowych plików; żadna operacja zapisu nie była kierowana do E:.

Źródła Kanku przeczytane bezpośrednio:

- Source\Verth.Plugin.Kanku\Private\Source\KankuEffectEditorPanel.cpp i Private\Include\KankuEffectEditorPanel.h — kompletne drzewo UI, akcje, preview i stany dokumentu.
- Source\Verth.Plugin.Kanku\Private\Include\KankuEffectDocument.h — model efektu, emiterów, kart, outputów, krzywych, gradientów i eventów.
- Source\Verth.Plugin.Kanku\Private\Source\KankuPlugin.cpp — rejestracja pluginu, komponenty, lifecycle i runtime.
- Private\Source\KankuEmitterListState.cpp, KankuBuiltinRecipes.cpp, KankuRecipeBrowser.cpp, KankuCardDefinitionRepository.cpp, KankuDiagnosticsCollector.cpp — emitery, recipes, moduły i walidacja.
- Implementacja pul, composerów CPU/GPU, cookera, repozytorium i serializacji w całym Source\Verth.Plugin.Kanku.
- Hostowe PluginUiBridge.cpp, PluginBackedEditorPanel.cpp, ProjectFilesPanel.cpp, InspectorPanel.cpp, EditorSceneAuthoring.cpp, docking, bootstrap i session persistence.
- EngineContent\Kanku — assety i shaderowe zasoby referencyjne.

Źródła 21kb przeczytane bezpośrednio:

- sources/engine: asset manager, .kbvfx, SceneParticleSystems, SceneState, systemy sceny, skrypty, komponenty, scene/prefab codec i lifecycle modułów.
- sources/plugins: audio_miniaudio jako wzorzec providera oraz terrain_editor jako wzorzec rozdziału core/plugin.
- sources/renderer: RenderScene, SceneRenderer, Renderer, SceneParticleRenderSynchronizer, instance buffers, materiały i shadery.
- sources/editor: docking, message loop, viewports, Project Files, Inspector, scene actions, drag/drop, plugin catalog, project bootstrap i CMake/testy.
- główny CMakeLists.txt i właściwe targety CMake pod kątem najmniejszej późniejszej weryfikacji.

Nazwy nowych klas i plików w dalszej części są decyzją tego planu, a nie domysłem co do istniejącego kodu. Każdy istniejący symbol wskazany jako punkt integracji został zweryfikowany w branchu 1.0.

## 2. Wniosek kanoniczny

Particli nie może być drugim, równoległym systemem cząstek. 21kb już ma:

- typ assetu ParticleEffect i rozszerzenie .kbvfx;
- loader oraz atomowy zapis;
- publiczne SceneParticleSystems i ScriptParticleSystemApi;
- CPU simulation w SceneParticleSystemService;
- most SceneParticleRenderSynchronizer;
- billboard quad i integrację z materiałem.

Plan zachowuje ten publiczny kontrakt i ewoluuje .kbvfx z płaskiego v1 do wieloemiterowego v2. Stabilne typy assetu, komponentu, loadera, publicznego facade i neutralnego render snapshotu pozostają w kb_engine, ponieważ AssetManager przechowuje loader i payload dłużej niż może żyć przeładowywana DLL. Implementacja symulacji, kompilacja efektu i system sceny należą do pluginu Rendering.Particli. Renderer jest jedynym właścicielem uchwytów bgfx. Edytor dostaje testowalny Particli authoring core oraz cienki adapter do obecnego, zamkniętego systemu paneli.

Ta granica eliminuje cztery obecne problemy:

1. symulacja nie będzie pompowana przez ScriptRuntimeSceneSystem;
2. renderer nie będzie tworzył jednego MeshRenderProxy na cząstkę;
3. hot reload DLL nie pozostawi w AssetManager wskaźników do typów lub loaderów z rozładowanego modułu;
4. preview i runtime użyją tego samego kompilatora oraz kernela, zamiast dwóch rozbieżnych ścieżek.

## 3. Rzeczywisty Kanku — co należy odwzorować

### 3.1. Architektura i dane

Kanku jest pojedynczą prywatną DLL. Eksportuje descriptor w KankuPlugin.cpp:545-562. Dokument KankuEffectDocument ma do 8 emiterów i 32 event bindings; emiter ma do 16 kart. Oddzielne ID definicji i instancji są dobrą właściwością modelu. Parametry deklarują Float, wektory, Color, Texture, Mesh, Curve i Gradient, lecz produkcyjny panel potrafi edytować tylko Float i prosty kolor startowy.

Format .kanku jest niestandardowym tekstem liniowym z nagłówkiem i hashem FNV. Repozytorium ma zapis atomowy, ale Save panelu omija je, zapisuje bezpośrednio i ma limit 64 KiB. Cooker i cooked payload istnieją, lecz właściwy lifecycle edytora/runtime ich nie używa; konwersja efekt → recipe ogranicza dane do 4 emiterów. Importer jest pusty.

CPU pool jest strukturą SoA, ma pojemność 4096 i deterministyczny LCG. Kolejność kroku to spawn, lifetime/death, gravity/wind/drag, integrate, collision/events, color/size. Produkcyjna ścieżka wykonuje tylko Gravity, Wind, Drag i Collide; bogatszy composer kart jest testowy i niepodpięty. Burst, duration, prewarm, wiele authored outputów i część parametrów nie wpływają na runtime.

Instancje komponentów współdzielą pule według effect/params, zatem odtwarzanie i czas nie są niezależne. Render produkcyjny jest billboardowym overlayem; material/texture, depth, AA i większość outputów są ignorowane. GPU path jest nieosiągalny: próg wynosi 10 000 przy maksymalnej puli 4096, pipeline nie jest instancjonowany, shader jest pusty, CPU pozostaje źródłem prawdy. Nie ma bezpiecznej synchronizacji simulation/render.

W Particli należy zachować SoA, twarde limity, liczniki overflow, stabilne ID, deterministyczny seed i wspólny kernel preview/runtime. Nie wolno kopiować współdzielonego stanu playback, nieaktywnego GPU, niewykonywanych pól ani cichych fallbacków.

### 3.2. Pełne drzewo okien i powierzchni użytkownika

Kanku ma jedno własne, closable, singletonowe okno Kanku Effect Editor, panel ID Verth.Plugin.Kanku.EffectEditor. Host umieszcza je w centralnym leafie dockingu. Pozostałe powierzchnie to wspólne Project Files, Scene i Inspector.

Główne drzewo renderu, potwierdzone w KankuEffectEditorPanel.cpp:1282-1400:

    Kanku Effect Editor
    ├── Toolbar
    ├── poziomy wiersz
    │   ├── GPU Preview, 2/3 szerokości
    │   └── Composer, 1/3 szerokości, pionowy scroll
    │       ├── Emitters
    │       ├── Emitter Settings
    │       ├── Recipes
    │       ├── Behavior Cards
    │       ├── Output
    │       ├── Dependencies
    │       └── Diagnostics, gdy są problemy
    └── Status Bar

Toolbar, KankuEffectEditorPanel.cpp:514-633:

- Play/Pause, Restart, Stop;
- separator;
- Save, Revert, Validate, Bake, Apply to Selection;
- elastyczny spacer;
- nazwa lub (no effect), liczba emiterów, GUID, dirty.
- próba otwarcia innego pliku przy dirty pokazuje inline Discard & Open / Cancel, nie modal.

Preview, KankuEffectEditorPanel.cpp:941-1058:

- HUD: Playing/Paused, czas, Alive, FPS i semantyczny kolor FPS;
- pusty stan: load a recipe or add emitters to preview;
- LMB orbit, wheel zoom; brak resetu kamery, grid/background controls i komunikatu renderer-not-ready;
- hostowy offscreen target 768×768; domyślna kamera azymut 45°, elewacja 20°, zoom 1.

Emitters, KankuEffectEditorPanel.cpp:769-830:

- wiersz: nazwa, off lub N max, enable, up, down, remove;
- klik wybiera, przyciski zmieniają kolejność o jeden; brak drag reorder;
- + Add Emitter tworzy Emitter N i wybiera go;
- pusty stan: no emitters — add one or load a recipe.

Emitter Settings, KankuEffectEditorPanel.cpp:1060-1135:

| Pole | Zakres Kanku |
|---|---:|
| Spawn Rate | 0–500 |
| Lifetime Min / Max | 0–10 |
| Duration | 0–30 |
| Velocity X/Y/Z | -25–25 |
| Velocity Random | 0–1 |
| Gravity Scale | -5–5 |
| Start RGBA | 0–1 |
| Size Start | 0.01–8 |
| Size End | 0–8 |
| Fade / Looping / Prewarm | checkbox |

Recipes, KankuEffectEditorPanel.cpp:635-767:

- search;
- chips All, Simple, Projectile, Trail, Hit Impact, Beam, Portal, Aura, Wall, Teleport, Rain, Others, po 3 w rzędzie;
- live tiles po 3 w rzędzie, 84 px preview i nazwa;
- klik natychmiast dopisuje emitery, nadaje świeże ID, zaznacza ostatni i uruchamia preview;
- drag recipe do sceny tworzy obiekt efektu;
- 15 built-ins: Blood Splatter, Muzzle Flash, Bullet Trail, Explosion, Impact Sparks, Rain, Snow, Leaves, Fire Burst, Frost Nova, Arcane Sparks, Coin Burst, Level Up Aura, Smoke Stack, Sparks Shower;
- defekt referencji: wszystkie mają kategorię Simple, a miniatura renderuje tylko pierwszy emiter.

Behavior Cards, KankuEffectEditorPanel.cpp:848-939:

- zastosowany stos kart; karta ma tytuł, enable, remove i body parametrów;
- biblioteka chips po 3 w rzędzie;
- built-ins: Gravity, Wind, Drag, Color Over Life, Size Over Life, Alpha Over Life, Collide World, Billboard Output, Stretched Billboard Output, Sub Emitter Spawn;
- klik dodaje; drag ustawia payload, ale nie istnieje żaden drop receiver;
- brak reorderu i menu kontekstowego;
- tylko Float ma działający slider, inne typy są tekstem.

Output, KankuEffectEditorPanel.cpp:435-508:

- wiersz opisany Material GUID, ale przyjmuje wyłącznie Texture D&D;
- Blend: Opaque, Alpha, Add, Mul, Sub, Premul;
- Sort: None, B→F, F→B, Dist, Age;
- brak browse/pickera, output type, depth i AA.

Dependencies, KankuEffectEditorPanel.cpp:1169-1233:

- effect GUID, recipe GUID, material GUID per emitter, Find References;
- hostowe wyszukiwanie zawsze zwraca zero, więc funkcja jest wizualnie obecna, lecz nie działa.

Diagnostics, KankuEffectEditorPanel.cpp:1137-1163:

- error/warning rows; klik wybiera właściwy emitter/card;
- waliduje brak emiterów, pustą nazwę, brak emisji, lifetime, ujemny rate, duplikaty kart i parametry poza zakresem;
- sekcja pojawia się warunkowo; defekt: nagłówka nie da się faktycznie zwinąć.

Status:

- Errors/Warnings/Valid, Alive, Emitters, ostatnia komenda, Unsaved.

Project Files:

- wspólny browser, bez prywatnego okna Kanku;
- create przez [+] → Kanku Effect, unikalna nazwa, selection i inline rename;
- double-click otwiera/focusuje panel;
- ogólne rename/cut/copy/paste/duplicate/delete/import;
- .kanku drag do Scene tworzy ParticleEffect entity.

Inspector i Scene:

- komponent ParticleEffect: Effect, Enabled, Auto Play, Rate Multiplier, Max Particles Override, Deterministic Seed, Follow Transform;
- asset dropdown: None, wszystkie .kanku, Missing(id), reveal;
- brak dropu .kanku na pole Inspectora;
- Apply to Selection oraz drop w Scene tworzą/modyfikują komponent.

Kanku nie ma timeline, curve editora, gradient editora, keyframe editora, multi-document tabs, Save As, lokalnego undo/redo, skrótów, emitter rename, card reorder, menu kontekstowych stosu, async loading ani event-binding UI. Particli nie może przedstawiać tych funkcji jako zgodności 1:1. Edycja curve/gradient będzie świadomym rozszerzeniem wymaganym przez już istniejące dane .kbvfx.

### 3.3. Defekty UX, których nie kopiujemy

- mutacje kart nie zawsze ustawiają dirty;
- dirty document bez ścieżki oraz close/shutdown mogą ominąć ochronę;
- Revert nie synchronizuje preview;
- Find References zawsze zwraca zero;
- Bake ustawia wyłącznie flagę;
- Apply raportuje sukces bez selection;
- zły kolor statusu po niektórych błędach load;
- puste kategorie recipes;
- miniatura recipe renderuje tylko pierwszy emitter;
- pierwszy Save nie aktualizuje ścieżki session;
- Material/Texture są typowo pomylone;
- pola akceptowane przez authoring, ale ignorowane przez runtime.

## 4. Rzeczywisty stan 21kb i punkty rozszerzeń

### 4.1. Asset i AssetManager

ParticleEffectAsset w sources/engine/include/engine/scene/ParticleEffectAsset.hpp jest obecnie płaskim, jednoemiterowym v1. Ma materialReference, loop/duration/max, rate/speed/lifetime/direction/spread/gravity oraz kb::math::Curve sizeOverLifetime i kb::math::Gradient colorOverLifetime.

ParticleEffectAssetIO w sources/engine/include/engine/scene/ParticleEffectAssetIO.hpp oraz sources/engine/src/scene/ParticleEffectAssetIO.cpp używa płaskiego key value, ignoruje unknown keys i zapisuje atomowo przez SceneAssetBinaryIO::WriteBytesAtomically. ParticleEffectAssetLoader ładuje payload, ale nie implementuje dependency discovery ani validation.

Scene::OnCreate rejestruje loader ParticleEffect bezwarunkowo. To jest poprawna granica lifetime: loader i payload muszą pozostać w kb_engine. AssetManager::PublishRuntimeAsset pozwala publikować working copy bez zapisu i ma generacje/unload. AssetManager celowo nie przechowuje type_index z DLL, ponieważ type_info umiera przy module reload.

Decyzja: rozszerzenie pozostaje .kbvfx, typ pozostaje ParticleEffect, loader pozostaje w kb_engine. V1 jest tylko czytany i migrowany w pamięci; writer zapisuje wyłącznie v2.

### 4.2. Runtime i skrypty

SceneParticleSystemService ma maksymalnie 256 instancji i 2048 cząstek na instancję. Używa liniowego lookupu, per-frame ponownie rozwiązuje asset, trzyma std::vector<ParticleState>, usuwa cząstki przez erase/remove i generuje deterministyczny stożek. Emit potrafi zwrócić true mimo braku assetu/transformu. Nierozwiązywalny asset zamraża stary stan.

SceneParticleSystems::Advance jest wywoływane przez ScriptRuntimeSceneSystem. To oznacza, że bez runtime skryptów cząstki nie mają właściwego właściciela aktualizacji. Publiczne API Create/Play/Stop/SetSeed/SetParameterScalar/Emit i ScriptParticleSystemApi musi pozostać kompatybilne na poziomie nazw, lecz dostać typed results oraz delegację do providera.

EngineModuleHost i IEngineModule oferują OnLoad/Enable/SceneAttach/Detach/Disable/Unload. Audio.Miniaudio pokazuje właściwy wzorzec: moduł dodaje per-scene SceneSystem, system rejestruje backend w OnCreate i wyrejestrowuje go w OnDestroy.

### 4.3. Renderer

SceneParticleRenderSynchronizer w sources/renderer tworzy jeden syntetyczny MeshRenderProxy na każdą żywą cząstkę. Co klatkę buduje zbiory live IDs, ładuje effect, liczy model billboardu i wywołuje RenderScene::UpsertMesh. RenderScene następnie grupuje mesh instances, więc draw call może być zbatchowany, ale koszt map, proxy lifecycle i CPU transformów nadal jest per-particle.

Renderer tworzy synchronizer w Renderer.cpp:271, synchronizuje scenę przed submitami i zwalnia dane per-scene. SceneRenderer ma BaseTransparent i RenderInstanceBuffer, lecz RenderScene nie ma ParticleRenderProxy. Renderer i editor są obecnie ograniczone przez główny CMake do Win32.

Decyzja: istniejący synchronizer zostanie zmieniony w batch bridge. RenderScene dostanie neutralne particle batches, a osobny ParticleRenderer wykona outputy. Renderer, nie plugin, tworzy i niszczy bgfx buffers/programs.

### 4.4. Editor

DockPanelKind w sources/editor/include/kb/editor/docking/DockTypes.hpp jest zamkniętym enumem. DefaultDockWorkspace tworzy stałą listę paneli do ID 13. PanelContentRenderer ma switch po rodzaju. EditorApplicationMessageLoop jawnie kolejkuje viewporty Scene, Skeletal Mesh i Material. Nie istnieje ogólny editor-panel ABI z plugin DLL.

Właściwa minimalna integracja:

- dodać DockPanelKind::ParticliEditor i panel ID 14 w center leaf;
- wpiąć paint/hit/input/present w istniejące routery;
- trzymać dokument, command stack, walidację, recipes i preview session w kb_particli_editor_core;
- nie tworzyć w tym zadaniu ogólnego systemu editor panels z DLL, ponieważ byłby to niezależny refactor hosta.

Project Files ma jawne enumy create/context/double-click/icon. Drag state jest wspólny dla assetów i ma flagi typów. Inspector jest jawnie spięty z komponentami. Każde z tych miejsc wymaga cienkiego wpisu Particli, nie nowego równoległego asset browsera.

## 5. Docelowa architektura Particli

### 5.1. Targety i granice

| Target | Zawartość | Dozwolone zależności |
|---|---|---|
| kb_engine | stabilny asset v2, IO/validation, komponent, publiczny facade/backend ABI, neutralny render snapshot | STL, istniejący engine |
| kb_particli_core | compiler, CPU kernel, SoA pools, module executors, instancje, snapshot builder | kb_engine |
| kb_particli_plugin | ParticliModule, ParticliSceneSystem, ParticliSimulationBackend; dynamiczny lub static zgodnie KB_BUILD_PROVIDER_MODULES_AS_DLL | kb_particli_core, kb_engine |
| kb_particli_editor_core | document/commands, gateway, recipes, diagnostics, preview controller, curve/gradient models | kb_particli_core, kb_engine; bez Win32 |
| kb_renderer | particle batch storage, GPU resources, submittery outputów, shaders | istniejący renderer i bgfx; bez zależności do pluginu |
| kb_editor | wyłącznie adaptery dock/render/input/D&D/Inspector i host viewport | kb_particli_editor_core, kb_renderer |

Nie wolno przekazywać przez granicę DLL std::type_index, raw pointerów do asset payloadów ani bgfx handles. Backend jest rejestrowany na Scene i musi zostać odłączony przed zniszczeniem modułu. Snapshot zawiera engine-owned typy wartościowe i kopie/spany do bufora o jasno określonej generacji.

### 5.2. Klasy i odpowiedzialności

Engine:

- ParticleEffectAsset — jedyne źródło prawdy authoring data v2.
- ParticleEffectAssetIO — ścisły parser v1/v2 i kanoniczny, atomowy writer v2.
- ParticleEffectAssetValidator — StructuralValidate bez AssetRegistry oraz ValidateDependencies z registry; zwraca diagnostics z dokładnym emitterId/moduleId/propertyPath.
- ParticleEffectComponent — trwałe przypięcie assetu do entity.
- IParticleSimulationBackend — stabilny interfejs providera dla Create/Release/Play/Pause/Stop/Restart/Emit/override/query/snapshot/events.
- ParticlePlayback — per-scene rejestracja/wyrejestrowanie backendu i typed forwarding, analogicznie do AudioPlayback.
- SceneParticleSystems — zachowany facade dla C++/skryptów; mapuje stare bool/ID API na typed backend results bez implementacji symulacji.
- ParticleRenderSnapshot — value DTO opisujący gotowe zakresy cząstek, output/material i diagnostics; nie zawiera rendererowych typów ani DLL-owned deletera.
- ParticleRenderCapabilities — core DTO publikowany przez renderer do snapshot channel: consumer epoch, output/shader/compute/indirect support i budgets. Plugin może jawnie odrzucić GpuVisualRequired bez zależności do kb_renderer.

Particli core/runtime:

- ParticliEffectCompiler — waliduje source i buduje immutable CompiledParticleEffect: plan modułów, LUT krzywych/gradientów, limity, dependency IDs i pipeline keys.
- ParticliSimulationKernel — jedyna implementacja fixed-step; wspólna dla preview i runtime.
- ParticliParticlePool — prealokowany SoA z dense live range i swap-remove; positions, previousPositions, velocities, ages, lifetimes, colors, sizes, rotations, seeds i output data.
- ParticliEffectInstance — niezależny playback state, seed stream, per-emitter emission accumulators, owner/follow policy, pools i compiled revision; nie ma drugiego fixed-time accumulatora.
- ParticliModuleExecutorRegistry — jawna tabela type → executor; compiler odrzuca moduł bez executora.
- executory Gravity, Wind, Drag, InitialVelocity, ColorOverLife, SizeOverLife, AlphaOverLife, CollisionPlane i SubEmitter — wykonują tylko własną fazę.
- ParticliRenderSnapshotBuilder — wypełnia wolny core-owned snapshot slot i publikuje kompletną revision albo zgłasza backpressure.
- ParticliSimulationBackend — zarządza instancjami sceny, asset generations, typed results, events i snapshotami.
- ParticliSceneSystem — rejestruje backend, skanuje ParticleEffectComponent, wykonuje fixed update i wyrejestrowuje/cleanuje.
- ParticliModule — provider Rendering.Particli, dodaje/usuwa ParticliSceneSystem per scene.

Editor core:

- ParticliEditorDocument — source asset, path/id, clean revision, selected emitter/module, dirty i pending-open.
- ParticliEditorCommandStack — wszystkie mutacje jako undoable commands; save mark i history coalescing sliderów.
- ParticliAssetGateway — load/migrate/save/publish/unload, conflict detection i aktualizacja session path.
- ParticliEditorValidator — scala engine diagnostics z editor-only checks i nawigacją.
- ParticliRecipeLibrary — ładuje read-only .kbvfx z Content/Particli/Recipes tym samym IO; filtruje metadata; append robi deep copy i świeże ID.
- ParticliPreviewSession — izolowana scena/owner, ten sam compiler/kernel/backend i normalny renderer snapshot; deleguje czas do SceneRuntime fixed loop (1/60, max 8), bez własnego accumulatora, i zbiera HUD telemetry.
- ParticliBakeService — rzeczywiście kompiluje immutable runtime data i zapisuje content-hash cache atomowo; nie zmienia source assetu.
- ParticliCurveEditorModel i ParticliGradientEditorModel — editują typy kb::math, walidują key order/range i emitują commands.
- ParticliPanelLayout — wylicza toolbar, preview, composer, sections, status i hit rects dla docked/floating.
- ParticliPanelController — mapuje input, D&D i commands bez malowania.

Renderer:

- ParticleRenderBatchDesc/ParticleRenderProxy — effect/emitter/output/material/pipeline key, bounds, sort i span instance data.
- RenderScene::UpsertParticleBatches/RemoveParticleScene — przyjmuje jedną generację snapshotu na scene, bez mapy per-particle.
- SceneParticleRenderSynchronizer — po zmianie kopiuje/udostępnia opublikowane batches; nie ładuje assetów i nie buduje mesh proxy.
- ParticleInstanceBuffer — zwarty GPU layout billboard/stretched/mesh; dynamic/transient buffer rośnie do wysokiego watermark i ma jawny budget.
- ParticleRenderer — grupuje po output/material/blend/depth/sort, culluje batch bounds, sortuje tylko zakresy wymagające sort i submituje.
- ParticleBillboardSubmitter, ParticleStretchedBillboardSubmitter, ParticleMeshSubmitter, ParticleTrailSubmitter, ParticleRibbonSubmitter, ParticleBeamSubmitter, ParticleVolumetricSubmitter — osobne capability/execution units.
- ParticleGpuSimulation — późniejszy compute backend; nie jest wybierany, dopóki capability i conformance tests nie przejdą.

## 6. Asset .kbvfx v2

### 6.1. Model danych

ParticleEffectAsset:

- formatVersion = 2;
- effectId, displayName, recipeCategory, determinismSeed;
- durationSeconds, looping, backendPolicy;
- emitters: maksymalnie 8;
- eventBindings: maksymalnie 32.

ParticleEmitterAsset:

- stable emitterId, name, enabled;
- localPosition/localRotation/localScale;
- maxParticles 1–65536, simulationSpace Local/World;
- spawn: Continuous/Burst, rate curve, bursts, lifetime range, speed range, direction, spread, prewarm;
- ordered modules: maksymalnie 16;
- ParticleOutputAsset.

ParticleModuleAsset:

- stable moduleId;
- enum ParticleModuleType;
- enabled;
- phase wyprowadzona z typu przez compiler, nie authorowana;
- std::variant z typowanym payloadem; bez stringowej unii i bez niewykonywanych parametrów.

Wymagane payloady pierwszego kompletnego runtime:

- InitialVelocity: direction, speed min/max, randomization;
- Gravity: acceleration Vec3 lub scale wobec scenowej grawitacji;
- Wind: acceleration Vec3;
- Drag: nonnegative coefficient;
- ColorOverLife: kb::math::Gradient;
- SizeOverLife: kb::math::Curve;
- AlphaOverLife: kb::math::Curve;
- CollisionPlane: plane/floor, restitution, friction, max events per step;
- SubEmitter: target emitterId, trigger Birth/Death/Collision, count, maksymalna głębokość.

ParticleOutputAsset:

- type: Billboard, StretchedBillboard, PointSprite, Mesh, Trail, Ribbon, Beam, Volumetric;
- material: ParticleAssetReference;
- optional mesh/textureAtlas references zależne od typu;
- blend: Opaque, Alpha, Add, Multiply, Subtract, Premultiplied;
- sort: None, BackToFront, FrontToBack, Distance, Age;
- depthTest, depthWrite, softParticles, antiAliasing, alignment;
- output-specific typed payload.

ParticleAssetReference:

- assetId oraz virtualPath;
- zapisuje oba, jeśli registry zna oba; assetId jest primary runtime identity, path służy migracji/relink;
- walidacja wymaga, aby co najmniej jeden identyfikator rozwiązał się do dozwolonego typu i oba wskazywały ten sam asset.

ParticleEventBindingAsset:

- sourceEmitterId, trigger, optional sourceModuleId;
- action EmitTargetEmitter lub EmitEffectAsset;
- target ID/reference, count;
- maxDepth i per-step budget są obowiązkowe.

Początkowe hard ceilings, wspólne dla validatora, compilera, runtime i UI:

| Limit | Wartość |
|---|---:|
| effect instances / scene | 256 |
| emitters / effect | 8 |
| modules / emitter | 16 |
| event bindings / effect | 32 |
| CPU particles / emitter | 65 536 |
| CPU particles / scene | 262 144 |
| GPU visual particles / scene | 1 048 576 |
| spawns / fixed step / scene | 65 536 |
| events / fixed step / scene | 8 192 |
| sub-emitter depth | 3 |
| trail samples / particle | 64 |
| ribbon lub beam segments / emitter | 4 096 |
| retained snapshot slots / scene | 4 |
| retained GPU step journal / scene | 64 fixed steps |
| packed CPU snapshot bytes / scene | 64 MiB |
| particle GPU resources / scene | 256 MiB |

Niższe project/runtime budgets mogą odrzucać wcześniej. Zwiększenie hard ceiling wymaga zmiany schema limit, pamięciowego benchmarku i review; asset nie może sam wymusić nieograniczonej alokacji.

### 6.2. Format tekstowy i parser

V2 pozostaje tekstowy, aby zachować diffowalność i obecną filozofię złożonych assetów. Pierwszy semantyczny rekord jest zgodny z istniejącym asset_io::TextAssetHeader, a dalsze rekordy używają deklarowanych countów i indeksowanych, kanonicznie uporządkowanych kluczy:

    21kb ParticleEffect 2
    effect.displayName "Muzzle Flash"
    effect.emitterCount 1
    effect.emitter[0].id 42
    effect.emitter[0].moduleCount 3
    effect.emitter[0].module[0].type InitialVelocity
    ...

Kontrakt parsera:

- UTF-8, quoted strings z jednoznacznym escapingiem;
- maksymalny rozmiar source 512 KiB;
- wymagany nagłówek 21kb ParticleEffect 2 jako pierwszy semantyczny rekord;
- odrzucenie future version, duplicate scalar keys, brakujących indeksów, count mismatch, nieznanych enumów, NaN/Inf, niesortowanych lub zdublowanych IDs oraz wartości poza hard limits;
- nieznany klucz v2 jest błędem z linią i property path, a nie cichym ignore;
- writer emituje stałą kolejność pól, locale-independent floats i newline \n;
- zapis wyłącznie przez WriteBytesAtomically;
- loader zwraca błąd z lokalizacją, nie ogólne could not parse.

### 6.3. Migracja v1 → v2

Reader rozpoznaje brak formatVersion jako legacy v1 i tworzy jeden emitter:

| v1 | v2 |
|---|---|
| materialReference | emitter[0].output.material |
| looping/durationSeconds | effect.looping/durationSeconds |
| maxParticles | emitter[0].maxParticles |
| emissionRatePerSecond | spawn Continuous rate constant |
| startSpeedMin/Max, direction, spread | InitialVelocity |
| startLifetimeMin/Max | spawn lifetime range |
| gravityScale | Gravity module |
| sizeOverLifetime | SizeOverLife module |
| colorOverLifetime | ColorOverLife module |

Migracja nie zapisuje automatycznie pliku podczas load. Editor pokazuje status Migrated v1; Save zapisuje v2. Test golden musi wykazać identyczny baseline billboard behavior dla wspólnego podzbioru.

### 6.4. Dependency discovery i runtime compilation

ParticleEffectAssetLoader implementuje DiscoverDependencies i ValidateDependencies dla material, mesh, texture atlas oraz zewnętrznych sub-effects. Type checks używają istniejącego engine-level AssetKind/AssetMatchesKind zamiast zależności kb_engine → kb_renderer lub powielonych stringów. Cykl sub-effectów jest błędem. Compiler tworzy immutable CompiledParticleEffect i tablicę wykonania według faz, zachowując stabilną kolejność source wewnątrz fazy.

Backend policy:

- CpuDeterministic — zawsze CPU;
- GpuVisualPreferred — GPU tylko po capability + conformance; jawnie raportuje wybraną ścieżkę i fallback reason;
- GpuVisualRequired — typed failure BackendUnavailable, nigdy cichy CPU fallback.

Recipes nie mają drugiego formatu. Są normalnymi, read-only .kbvfx w sources/plugins/particli/content/Recipes i po buildzie Content/Particli/Recipes. Klik deep-copy emiterów do dokumentu; runtime nie zna recipe library.

## 7. Przepływ danych i lifecycle

### 7.1. Editor → Asset → Runtime → Renderer

    Project Files / Inspector / Scene drop
                    │
                    ▼
        ParticliEditorDocument + CommandStack
                    │ każda zaakceptowana mutacja
                    ├── ParticleEffectAssetValidator
                    ├── AssetManager::PublishRuntimeAsset
                    └── ParticliEffectCompiler
                                │
                                ▼
                    ParticliPreviewSession
                                │ fixed 1/60
                                ▼
                    ParticliSimulationKernel
                                │ complete back buffer
                                ▼
                    ParticleRenderSnapshot generation N
                                │
                                ▼
            SceneParticleRenderSynchronizer
                                │ batches
                                ▼
                     RenderScene / ParticleRenderer
                                │
                                ▼
                     renderer-owned bgfx resources

Save wykonuje structural validation, dependency validation, compiler validation, atomowy writer, AssetManager::Unload dla starej generacji i PublishRuntimeAsset nowej generacji. Dirty jest czyszczone dopiero po sukcesie wszystkich wymaganych operacji i aktualizacji hostowej ścieżki dokumentu. Błąd na dowolnym etapie zostawia dokument dirty i istniejący plik bez zmian.

Runtime:

    Scene/Prefab ParticleEffectComponent
                    │ effectAssetId
                    ▼
        ParticleEffectAssetLoader + dependencies
                    │
                    ▼
          ParticliEffectCompiler / cache
                    │ immutable CompiledParticleEffect
                    ▼
          ParticliSimulationBackend instance
                    │ fixed PostSimulation step
                    ▼
          core-owned ReadSnapshotPublisher<ParticleRenderSnapshot>
                    │
                    ▼
          renderer batches / GPU submissions

Scripts wywołują nadal Particles.Create/Play/Stop/SetSeed/SetParameterScalar/Emit. SceneParticleSystems przekazuje żądanie do ParticlePlayback. Brak providera, zły asset, brak właściciela, przekroczony limit i niewspierany output są rozróżnialnymi wynikami, a binding skryptowy przekazuje błąd zamiast fałszywego sukcesu.

### 7.2. Kolejność systemów

ParticliModule ma EngineModuleLoadingPhase::PreDefault. Dzięki temu backend jest podłączony przed modułem skryptów niezależnie od kolejności wpisów dynamicznych. ParticliSceneSystem:

- w OnFrameStart rejestruje bieżące asset generations i wykonuje pierwszy reconcile komponentów;
- RequiresFixedStep() = true;
- FixedUpdatePhase() = SceneFixedUpdatePhase::PostSimulation, gdzie ponawia tani revision/reconcile check po komendach strukturalnych PreSimulation, drenuje kolejkę i symuluje na bieżącym transformie po physics write-back;
- UpdatePhase() = PostFixed wyłącznie publikuje tombstone/diagnostics przy klatce bez fixed step i finalizuje telemetrykę, bez drugiej symulacji.

Script FixedTick działa w PreSimulation i może Emit/Play/Stop; Particli konsumuje te polecenia w PostSimulation tego samego substepu. ScriptRuntimeSceneSystem usuwa AdvanceParticleSystems z BeginFrame/ExecuteFrame. Drain OnParticleSystemFinished przenosi do ExecuteVariableFrame, obok pending collision events, przed Tick — zdarzenie utworzone w PostSimulation jest więc widoczne skryptowi w tym samym render frame. Bez ScriptModule Particli nadal symuluje.

### 7.3. Lifecycle instancji

1. Module load: brak zasobów sceny i GPU; rejestruje wyłącznie metadata Rendering.Particli.
2. Scene attach: ParticliModule dodaje ParticliSceneSystem i zapamiętuje SceneSystemHandle.
3. System OnCreate: tworzy backend/pools, rejestruje go przez ParticlePlayback, skanuje istniejące komponenty.
4. Component reconcile: Enabled + AutoPlay tworzy niezależną instancję; effectAssetId 0 lub invalid daje jawny diagnostic.
5. Create/Play: ładuje i kompiluje asset, alokuje per-emitter pule do validated caps, wyprowadza seed z component seed/effect seed/instance/emitter.
6. Fixed step: stabilna kolejność instanceId, emitter source order, particle dense order i fazy Command/Reconcile → Spawn/Initialize → Age/Death → Forces/Drag → Integrate → Collision/Constraints → ordered Events → Visual Attributes → Snapshot. Sub-emitter spawns z eventów wchodzą do następnego kroku; limit depth/budget zapobiega rekursji i modyfikacji iterowanego zakresu.
7. Stop: zatrzymuje nową emisję, żywe cząstki naturalnie wygasają. Pause zachowuje stan. Restart czyści pule, czas, accumulator i przywraca początkowy seed.
8. Owner transform: Follow Transform wpływa na kolejne spawny i local-space particles; false odłącza world transform w chwili startu. Utrata ownera zatrzymuje emission i drainuje albo czyści zgodnie z jawnie authorowaną OwnerDeathPolicy.
9. Hot reload assetu: compiler buduje candidate. Gdy emitter/module/output layout jest compatible, backend przełącza immutable revision na granicy fixed step i zachowuje żywe pule; w przeciwnym razie robi deterministyczny restart oraz diagnostic. Błędny candidate nie zastępuje działającej revision.
10. Release/component removal: przerywa emission, czyści pule, publikuje snapshot bez batcha i dopiero potem usuwa instance.
11. Scene detach: blokuje nowe komendy, wyrejestrowuje backend, kończy ewentualny compile work, publikuje pusty snapshot, czyści pools/events; Renderer::ReleaseScene usuwa swoje particle resources; dopiero potem DLL może zostać rozładowana.

Każdy EffectInstance ma własny czas, emission accumulators, RNG i pule. SceneRuntime jest jedynym właścicielem fixed-time accumulatora. Nie wolno stosować Kanku shared-pool-key jako stanu playback.

### 7.4. Synchronizacja i własność

- Wszystkie publiczne komendy sceny są wykonywane na owner thread albo trafiają do bounded command queue; debug build asertuje owner thread.
- CPU kernel buduje kompletny snapshot w wolnym, prealokowanym slocie. Core-owned ParticleRenderSnapshotChannel stosuje wzorzec istniejącego kb::core::ReadSnapshotPublisher, ale publikuje retained shared_ptr z bounded slot pool po zakończeniu pełnego kroku.
- Renderer trzyma shared_ptr<const ParticleRenderSnapshot> do końca submitu. Nietemplatowe Publish/Read są zdefiniowane w kb_engine, aby control block i deleter nie wskazywały do rozładowanej DLL.
- Sloty i ich wektory są rezerwowane przy create/high-watermark. Jeśli wszystkie retained sloty są zajęte, publikacja zostaje jawnie pominięta z SnapshotBackpressure zamiast alokować w hot path lub nadpisać bufor czytelnika.
- Renderer przed pierwszym consume publikuje ParticleRenderCapabilities do kanału sceny i zeruje je w ReleaseScene. GpuVisualRequired przed rejestracją consumera zwraca RenderBackendUnavailable; component autoplay pozostaje Pending i ponawia po zmianie capability epoch. Preferred uruchomione na CPU nie przełącza live state automatycznie — zmiana backendu wymaga jawnego Restart disposition.

Snapshot header zawiera revision, sceneId, backendEpoch i fixedStepIndex. Każdy emitter record zawiera instance/effect/emitter IDs, asset generation, output/material/mesh/atlas IDs, blend/depth/sort, bounds, live count i status. CPU stream zawiera compact position, previous position/velocity, size, rotation, stretch/frame, packed color i stable particle ID — bez model matrices. GPU stream nie zawiera uchwytu bgfx; zawiera program key oraz uporządkowany journal step/spawn/control commands.

Renderer odsyła lastConsumedFixedStep w capability state. Najnowszy snapshot zachowuje GPU journal od tego ack do current step, standardowo maksymalnie 64 kroki. Dwa viewporty używają tego samego ack i GPU state; tylko pierwszy wykonuje brakujące dispatches. Jeśli consumer nie nadąża albo viewport był nieobecny dłużej niż journal, backend zgłasza GpuCatchupOverflow i stosuje authorowaną RestartFromSeed/BoundedWarmup policy. Nie wolno pominąć kroków bez statusu ani wykonywać GPU sim osobno per viewport.
- Plugin nie przechowuje RenderScene pointerów ani bgfx handles.
- Renderer używa high-watermark buffers, nie alokuje per particle w steady state.
- Snapshot ma bounds per batch, overflow counters i dropped reason; brak pamięci lub budgetu nie jest wyjątkiem z noexcept, tylko typed diagnostic.

Determinism oznacza identyczny wynik dla tego samego asset hash, seed, command stream, fixed dt i wspieranego build/config. Bitowa identyczność float między różnymi ISA/kompilatorami nie jest obiecywana bez osobnego strict-FP gate. CPU jest ścieżką referencyjną dla conformance GPU.

## 8. Kanoniczny layout i mapowanie UI Kanku → Particli

### 8.1. Particli Editor

Particli Editor jest jednym closable panelem dokumentowym w center leaf. Otwiera jeden .kbvfx naraz, tak jak Kanku, ale hostowa session path jest aktualizowana po pierwszym Save. Default layout zachowuje 2/3 preview i 1/3 scroll composer. Nie dodaje wewnętrznego splittera w etapie parity; resizing odbywa się przez istniejący docking/floating/splittery hosta. Minimalny wspierany rozmiar panelu to 900×560; poniżej composer pozostaje 300 px, preview się kurczy, a toolbar przechodzi do overflow menu bez utraty akcji.

    Particli Editor
    ├── Toolbar
    ├── body
    │   ├── Preview
    │   │   ├── render surface
    │   │   ├── HUD
    │   │   ├── loading/error/empty overlay
    │   │   └── orbit/zoom input
    │   └── Composer scroll
    │       ├── Emitters
    │       ├── Emitter Settings
    │       ├── Recipes
    │       ├── Behavior Modules
    │       ├── Output
    │       ├── Events
    │       ├── Dependencies
    │       └── Diagnostics
    ├── Curve/Gradient drawer, tylko przy edycji właściwego modułu
    └── Status Bar

Events oraz Curve/Gradient drawer są jawnie oznaczonym rozszerzeniem Particli. Kanku ma dane dla eventów/krzywych, ale nie ma ich edytorów.

### 8.2. Toolbar i dokument

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| Play/Pause, Restart, Stop; KankuEffectEditorPanel.cpp:519-538 | Particli toolbar | Te same ikony/kolejność; sterują wyłącznie PreviewSession | ParticliPanelRenderer, ParticliPanelController, ParticliPreviewSession | sekwencja Play→Pause→Restart→Stop daje dokładne stany, Restart odtwarza hash seed |
| Save, Revert, Validate | dokument .kbvfx | Save atomowy i blokowany przez errors; Revert przeładowuje oraz resetuje preview/selection; Validate nie mutuje | ParticliAssetGateway, ParticliEditorValidator | fault-injection Save nie narusza starego pliku; Revert przywraca source i preview |
| Bake jest flagą | realny compile/cache | compiler + dependency/output capability gate + content-hash cache | ParticliBakeService, ParticliEffectCompiler | po Bake istnieje poprawny cache; zmiana source unieważnia hash; zły output blokuje Bake |
| Apply to Selection raportuje fałszywy sukces | undoable component mutation | disabled bez selection; wynik Added/Updated/NoSelection/Rejected; jedna transakcja dla multi-selection | EditorSceneParticliAssetActions | undo przywraca wszystkie poprzednie komponenty, status odpowiada wynikowi |
| nazwa/GUID/dirty | document metadata | AssetId/path, emitter count, dirty revision; dirty obejmuje wszystkie commands | ParticliEditorDocument | każdy typ mutacji ustawia dirty, undo do save point je czyści |
| inline dirty-open warning | kompletna loss protection | modal/inline policy wspólna dla Open, Close, Revert, shutdown i project switch: Save/Discard/Cancel | ParticliDocumentCloseGuard, host close router | żadna ścieżka zamknięcia nie traci niezapisanej zmiany bez wyboru |

Ctrl+S, Ctrl+Z, Ctrl+Y, Space i R są podpięte przez hostowy command routing, choć Kanku ich nie miał. Nie zmieniają wizualnej hierarchii toolbara.

### 8.3. Preview

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| GPU Preview 2/3, 768² offscreen | EditorSceneBgfxViewport surface | izolowana preview Scene z unique viewport key dla docked/floating; normalny Renderer path | ParticliPreviewSession, ParticliPanelRenderer, EditorApplicationMessageLoop | panel docked i floating prezentuje ten sam frame bez kolizji viewport key |
| Playing/time/alive/FPS | HUD | dodatkowo backend CPU/GPU, dropped, warnings; semantyczne kolory zachowane | ParticliPreviewTelemetry | wartości odpowiadają backend counters w 600 krokach |
| variable dt clamp 0.1 | SceneRuntime fixed 1/60 | preview przekazuje wall dt do SceneRuntime; max 8 substeps/frame i dropped-time warning, bez drugiego accumulatora | ParticliPreviewSession | różne render dt dają ten sam hash po równej liczbie fixed steps |
| LMB orbit, wheel zoom | preview camera | azymut 45°, elewacja 20°, zoom 1; clamp Kanku; input tylko w preview rect | ParticliPreviewCameraController | drag/wheel zmienia tylko kamerę Particli i respektuje clamps |
| czarny brak renderera | jawne stany | Loading asset/compile, Renderer unavailable, Compile error, Empty, Ready | ParticliPanelRenderer | screenshot każdego stanu; error zawiera actionable diagnostic |

Preview renderuje wszystkie emitery recipe/effect. Nie ma prywatnej uproszczonej mini-symulacji.

### 8.4. Emitters i settings

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| row: name, off/max, eye, up/down/remove | emitter row | identyczne elementy; double-click nazwy daje inline rename; buttons zachowane | ParticliEmitterListModel, ParticliPanelRenderer/Controller | selection, toggle, rename, reorder i remove przechodzą save/reload |
| brak emitter D&D | rozszerzenie funkcjonalne | grip + insertion line; up/down pozostają dla parity i klawiatury | ParticliEmitterDragController | drop zmienia source order, undo go odwraca, invalid self-drop no-op |
| + Add Emitter | typed default | default zgodny wizualnie z Kanku, ale pełny v2 i świeże stable ID | ParticliEditorCommands | 8 emitter limit blokuje dziewiąty z diagnostic |
| ustawienia scalar/color | typed property rows | zachowane etykiety i zakresy Kanku jako domyślne UI ranges; hard validation niezależne | ParticliEmitterSettingsModel | min/max, NaN paste i lifetime inversion są poprawnie obsłużone |
| Fade/Size/Color jako scalars | moduły z curve/gradient | proste wartości mapują do 2-key curves; Advanced otwiera drawer | ParticliCurveEditorModel, ParticliGradientEditorModel | keys są sortowane, endpoints zachowane i runtime evaluation zgodna z EngineMath |
| Prewarm authored, runtime ignored | prawdziwy prewarm | compiler wylicza bounded step count, preview/runtime ten sam wynik | ParticliSimulationKernel | prewarm hash = ręczne N kroków, budget overflow jawny |

Zakresy UI Kanku pozostają presetami sliderów. Wartość wpisana tekstowo może wyjść poza soft range tylko do hard range asset validatora.

### 8.5. Recipes

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| search + 12 category chips | Particli Recipe Library | te same chips i 3-column grid; rzeczywiste kategorie zapisane w recipe .kbvfx | ParticliRecipeLibrary, ParticliRecipeBrowserModel | każdy recipe pojawia się w oczekiwanej kategorii, filter empty state poprawny |
| 15 built-in recipes | 15 read-only .kbvfx | osobne normalne assety w Content/Particli/Recipes, walidowane tym samym loaderem | content/Recipes/*.kbvfx | test ładuje/kompiluje wszystkie 15 bez warnings |
| tile preview tylko emitter 0 | pełny tile preview | thumbnail job renderuje wszystkie emitery przez PreviewSession z ograniczonym budgetem | ParticliRecipeThumbnailService | multi-emitter golden pokazuje oba emitery |
| click append | undoable deep copy | świeże emitter/module IDs, selection last, jedna komenda | AppendRecipeCommand | append i undo są atomowe; source recipe bez zmian |
| drag recipe do Scene | undoable entity create | drag payload zawiera recipe asset; drop tworzy nowy project .kbvfx z kopią recipe, potem entity+component; cancel usuwa niezapisany candidate | EditorPointerDragState, EditorPointerDropHandler, EditorSceneParticliAssetActions | drop/cancel/undo nie zostawia orphan asset/entity |

Ostatni punkt świadomie różni się od Kanku in-memory recipe GUID: scena 21kb zawsze wskazuje trwały ParticleEffect asset.

### 8.6. Behavior Modules

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| applied cards, enable/remove/body | module stack | ten sam card chrome; body generowany z typed payload schema | ParticliModuleStackModel, ParticliPanelRenderer | każda wspierana karta add/edit/toggle/remove zapisuje i kompiluje |
| library chips 3/row | module library | tylko executory z registry; outputy są wyłącznie w Output, nie dublowane jako karty | ParticliModuleDefinitionCatalog | nie da się dodać niewykonywanego typu |
| martwy card drag | funkcjonalny insert/reorder | library drag pokazuje insertion line; applied card drag zmienia order; click-add pozostaje | ParticliModuleDragController | drag insert/reorder, escape cancel i undo test |
| tylko Float działa | typed controls | Vec3, Color, asset ref, Curve, Gradient, enum, bool mają właściwe edytory | ParticliModulePropertyModel | round-trip każdej variant payload |
| duplicate card diagnostics | compiler rule | repeatability zależy od typu: singleton odrzucony, additive może wystąpić wiele razy | ParticleEffectAssetValidator | duplicate Gravity odrzucony; dwa dozwolone modifiers zachowują source order |

Kanku nie ma lokalnego context menu. Particli parity również nie wymaga menu na kartach; wszystkie akcje są widoczne. Ogólne Project Files context menu pozostaje pełne.

### 8.7. Output, curves, gradients i events

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| pole Material akceptujące Texture | Material + opcjonalny Atlas | dwa type-correct asset fields; picker, reveal, clear i D&D; invalid type rejected | EditorMaterialAssetPickerDialog, ParticliOutputModel, drop policy | material/texture drop trafiają wyłącznie w poprawne sloty |
| Blend i Sort segmented controls | pełne enumy v2 | zachować kolejność/labels; zmiana aktualizuje pipeline key | ParticliOutputModel, ParticleEffectCompiler | save/load i render pipeline selection test każdego enumu |
| brak Output Type/depth/AA | rozszerzenie v2 | Output Type, alignment, depth test/write, soft particles, AA; pola warunkowe | ParticliOutputModel | zmiana typu pokazuje właściwe pola; unsupported capability blokuje Bake |
| brak curve UI | Curve drawer | plot, add/remove/move key, tangent mode, numeric fields, reset; command coalescing | ParticliCurveEditorModel/Renderer/HitTester | geometry/input tests plus Eval golden |
| tylko start color | Gradient drawer + color picker | stop add/remove/move, RGBA/HSV, numeric position; uogólniony EditorColorPickerDialog | ParticliGradientEditorModel, EditorColorPickerDialog | gradient round-trip, cancel nie mutuje, alpha zachowana |
| event data bez UI | Events section | source trigger, target emitter/effect picker, count/depth/budget, reorder/remove | ParticliEventBindingModel | cycle/depth validation i runtime birth/death/collision event tests |

Nie powstaje timeline, ponieważ Kanku go nie ma, a v2 nie potrzebuje globalnej ścieżki czasu. Duration, bursts, curves i gradients są edytowane w swoich właściwościach.

### 8.8. Dependencies, diagnostics i status

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| lista GUID/material deps | typed dependency tree | effect → emitter → output/module → asset; Missing i wrong-type jawne; reveal | ParticliDependencyModel | tree odpowiada DiscoverDependencies |
| Find References zawsze zero | prawdziwe reference query | przeszukuje scene/prefab component dependencies i otwarte working copies | EditorParticleEffectReferenceFinder | zapisany i niesaved reference są odnalezione |
| clickable diagnostics | stable property path | klik wybiera emitter/module i scroll/focusuje field; header faktycznie collapsible | ParticliEditorValidator, ParticliPanelController | każda diagnostic path nawiguje do właściwej kontrolki |
| status bar chips | pełna telemetria | errors/warnings/valid, alive, emitters, backend, overflow/dropped, command result, dirty | ParticliPreviewTelemetry | kolory i tekst wynikają z aktualnego modelu, nie starej flagi |

### 8.9. Project Files, Inspector, Scene i docking

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| [+] Kanku Effect | New Particle Effect | tworzy minimalny, poprawny v2, select + inline rename; double-click otwiera/focusuje | EditorAssetBrowserTypes, ContextCommandExecutor, DoubleClickHandler | unique create, rename, open i reload |
| generic context menu | istniejący Project Files | Open, Rename, Cut/Copy/Paste, Duplicate, Delete, Find References; duplicate nadaje nowe effect/emitter/module IDs | EditorAssetBrowserContextCommandExecutor | duplicate nie koliduje ID; delete dependency warning |
| Kanku icon | HeroIconKind::Bolt | mapowanie ParticleEffect → Bolt, bez nowego assetu ikony | ProjectFilesAssetIconResolver | resolver test zwraca Bolt dla ParticleEffect |
| .kanku drag do Scene | .kbvfx drag | ghost/ground placement, entity + ParticleEffectComponent w jednej undo transaction | EditorPointerDragSourceResolver, EditorPointerDropHandler, EditorSceneParticliAssetActions | drop, cancel, undo/redo i invalid asset |
| Inspector dropdown/reveal | picker + D&D + reveal | None/search/Missing; EditorParticleEffectAssetPickerDialog używa istniejącego generic AssetPickerWindow; drop .kbvfx działa | InspectorParticleEffectComponentModel, EditorParticleEffectAssetPickerDialog | picker, missing, reveal i D&D |
| komponent fields | ParticleEffectComponent | Effect, Enabled, Auto Play, Rate, Max, Seed, Follow oraz Owner Death policy | InspectorParticleEffectComponentModel/Interaction | edit → scene save/load → prefab instantiate → runtime |
| host docking/floating | DockPanelKind::ParticliEditor | panel ID 14 w center; istniejący reorder/detach/redock/split resize/session persistence | DockTypes, DefaultDockWorkspace, PanelContentRenderer, message loop | layout i open asset wracają po restart; viewport działa floating |

Stany Particli:

- Empty document: no effect;
- Empty effect: no emitters, disabled Play/Bake, actionable Add/Recipe;
- Loading: asset/dependencies/compiler/thumbnail z rozróżnieniem;
- Error: parse/dependency/compiler/renderer, poprzedni valid preview nie jest podszywany pod nowy asset;
- Missing asset w Inspectorze: zachowany ID, Missing(id), reveal disabled;
- Backend unavailable: runtime component istnieje, ale nie symuluje; diagnostic i typed API error;
- Overflow: efekt działa w limicie, licznik rejected spawn jest widoczny; brak cichego zwiększenia capacity;
- Dirty conflict: Save/Discard/Cancel dla open/close/revert/shutdown.

## 9. Manifest plików i klas

Oznaczenia: M — modyfikacja istniejącego pliku, N — nowy plik. Lista jest obowiązkowym zakresem implementacji; dodanie innych plików wymaga uzasadnienia zależnością wykrytą podczas kompilacji/testu, a nie pobocznego refactoru.

### 9.1. Build, plugin catalog i project bootstrap

| Plik | Zmiana |
|---|---|
| M CMakeLists.txt | add_subdirectory(sources/plugins/particli particli), dependencies/paths dla engine tests; Particli przed renderer/editor |
| N sources/plugins/particli/CMakeLists.txt | kb_particli_core, kb_particli_plugin, kb_particli_editor_core oraz focused tests/benchmarks; STATIC/SHARED według KB_BUILD_PROVIDER_MODULES_AS_DLL |
| M sources/editor/CMakeLists.txt | link editor core, plugin dependency/path, adapter sources, recipe content staging i focused editor test |
| M sources/editor/src/scene/EditorPluginCatalog.cpp | descriptor Rendering.Particli, category Rendering/VFX, binary path |
| M sources/editor/src/project/EditorProjectBootstrap.cpp | Rendering.Particli enabled w nowych projektach |
| M sources/editor/src/private/project/EditorProjectPaths.hpp i src/project/EditorProjectPaths.cpp | SavedRoot()/Saved/ParticliCache jako cache pochodny, nigdy Content |
| M samples/standalone_player/CMakeLists.txt i main.cpp | plugin path/dependency w dystrybucji/test harness; bez hardcoded static simulation |

Istniejące projekty nie są cicho mutowane. Editor po discovery .kbvfx bez włączonego providera pokazuje jednorazową migrację Add Rendering.Particli / Cancel. CLI i standalone validation kończą start jasnym błędem z komendą naprawczą. Dopiero zaakceptowana migracja zapisuje ProjectDescriptor.

### 9.2. Engine: asset, backend ABI i snapshot

| Plik | Klasa/odpowiedzialność |
|---|---|
| M sources/engine/include/engine/scene/ParticleEffectAsset.hpp | model v2 oraz legacy-compatible wartości |
| N sources/engine/include/engine/scene/ParticleEffectAssetSchema.hpp | wersja 2, limity i zamknięte enumy/variant payloads |
| M sources/engine/include/engine/scene/ParticleEffectAssetIO.hpp | Result z line/property diagnostics, Load/Save v1/v2 |
| M sources/engine/src/scene/ParticleEffectAssetIO.cpp | strict parser, migration dispatch, canonical atomic writer |
| N sources/engine/include/engine/scene/ParticleEffectAssetMigration.hpp | jawne mapowanie legacy v1 |
| N sources/engine/src/scene/ParticleEffectAssetMigration.cpp | bez disk mutation |
| N sources/engine/include/engine/scene/ParticleEffectAssetValidation.hpp | diagnostic severity/code/path |
| N sources/engine/src/scene/ParticleEffectAssetValidation.cpp | structural i dependency validation |
| M sources/engine/include/engine/scene/ParticleEffectAssetLoader.hpp | DiscoverDependencies/ValidateDependencies |
| M sources/engine/src/scene/ParticleEffectAssetLoader.cpp | typed dependency checks i dokładne błędy |
| N sources/engine/include/engine/particles/ParticleRuntimeResult.hpp | create/control/reload/backend statusy |
| N sources/engine/include/engine/particles/IParticleSimulationBackend.hpp | stabilny owner-thread provider ABI |
| N sources/engine/include/engine/particles/ParticlePlayback.hpp | Register/Unregister/forward/event queue |
| N sources/engine/src/particles/ParticlePlayback.cpp | analogia AudioPlayback, backend pointer w SceneState |
| N sources/engine/include/engine/particles/ParticleRenderSnapshot.hpp | renderer-neutral packed stream, batches, epoch/revision |
| N sources/engine/include/engine/particles/ParticleRenderCapabilities.hpp | renderer→plugin capability DTO i epoch, bez bgfx typów |
| N sources/engine/include/engine/particles/ParticleRenderSnapshotChannel.hpp | nietemplatowy wrapper ReadSnapshotPublisher |
| N sources/engine/src/particles/ParticleRenderSnapshotChannel.cpp | core-owned bounded retained slots/control blocks |
| M sources/engine/include/engine/core/ReadSnapshotQueue.hpp i tests/LibrarySafetyTests.cpp | opcjonalny retained-pointer publish overload oraz lifetime/backpressure test, bez zmiany istniejących value callers |
| M sources/engine/include/engine/scene/SceneParticleSystems.hpp | kompatybilny facade oraz nowe typed overloads/queries |
| M sources/engine/src/scene/modules/SceneParticleSystems.cpp | delegacja do ParticlePlayback |
| M sources/engine/src/private/scene/SceneParticleSystemService.hpp i sources/engine/src/scene/SceneParticleSystemService.cpp | usunięcie starej symulacji po przejściu testów; ewentualnie wyłącznie adapter migracyjny bez fallbacku |
| M sources/engine/src/private/scene/SceneState.hpp | backend pointer/owner thread, event queue, snapshot channel; usunięcie AoS legacy |
| M sources/engine/src/script/ScriptRuntimeSceneSystem.cpp | bez Advance; drain finished w PostFixed przed Tick |
| M sources/engine/src/script/ScriptParticleSystemApi.cpp | typed errors przy zachowaniu nazw Particles.* |
| M sources/engine/src/scene/Scene.cpp | loader nadal core-owned; lifecycle invariant tests |
| M sources/engine/CMakeLists.txt | pliki oraz focused kb_particli_asset_tests |

### 9.3. Engine: ParticleEffectComponent, scene i prefab

Nowy komponent:

    struct ParticleEffectComponent {
        std::uint64_t effectAssetId = 0;
        std::uint64_t deterministicSeed = 0;
        float rateMultiplier = 1.0F;
        std::uint32_t maxParticlesOverride = 0; // 0 = asset
        ParticleOwnerDeathPolicy ownerDeathPolicy = ParticleOwnerDeathPolicy::Drain;
        bool enabled = true;
        bool autoPlay = true;
        bool followTransform = true;
        bool restartOnActivate = true;
    };

Musi pozostać trivially copyable i przechowywać tylko authoring state; runtime handle należy do ParticliSceneSystem.

Nowe pliki:

- N sources/engine/include/engine/scene/ParticleEffectComponent.hpp — typ, StableId, persistability validation.
- N sources/engine/include/engine/scene/SceneParticleEffectComponents.hpp — public Set/Get/Remove facade.
- N sources/engine/src/private/scene/components/SceneParticleEffectComponentStore.hpp — ECS store.
- N sources/engine/src/scene/components/SceneParticleEffectStorage.cpp i SceneParticleEffectApi.cpp — storage/API.
- N sources/engine/src/scene/asset/io/components/SceneAssetParticleEffectComponentCodec.hpp/.cpp — binary read/write v33.

Obowiązkowe istniejące punkty integracji:

- M Scene.hpp, SceneComponents.hpp, SceneComponentQueries.hpp.
- M src/private/scene/components/SceneComponentRegistry.hpp, src/scene/components/SceneComponentRegistry.cpp, src/private/scene/components/SceneComponentStorage.hpp, src/scene/components/SceneComponentStorage.cpp.
- M src/private/scene/SceneComponentMutationService.hpp, SceneComponentQueryService.hpp oraz src/scene/modules/components/SceneComponentModule.cpp.
- M ScenePrefabNode.hpp i ScenePrefabOverrides.hpp; ParticleEffect dostaje override bit 28.
- M SceneDocument.hpp do version 33.
- M src/scene/asset/io/SceneAssetComponentCodec.cpp; ParticleEffect dostaje 64-bit component-presence bit 36.
- M src/scene/asset/io/SceneAssetWriter.cpp; effectAssetId jest zależnością sceny.
- M src/private/scene/prefab/ScenePrefabBakedData.hpp i src/scene/prefab/ScenePrefabBakedData.cpp.
- M src/private/scene/prefab/ScenePrefabOptionalComponentMask.hpp. Nie dopisywać kolejnego indeksu do błędnie współdzielonego baked mask; zastąpić parą {componentId, expectedPresent} dla wszystkich persistowanych optional components. Obecna tablica 20 elementów nie odpowiada późniejszym bitom Skeleton/Deformed i musi dostać test regresji.
- M ScenePrefabBulkInstantiationService.cpp, ScenePrefabComponentApplier.cpp, ScenePrefabComponentComparator.cpp, ScenePrefabComponentHasher.cpp, ScenePrefabComponentOverrideReporter.cpp, ScenePrefabComponentSnapshot.cpp, ScenePrefabInstanceSynchronizer.cpp, ScenePrefabNodeStateWriter.cpp i ScenePrefabValidator.cpp.
- M prefab/io/ScenePrefabAssetComponentParser.cpp i ScenePrefabAssetComponentWriter.cpp.
- M ScenePrefabAppliedPropertyBuilder.cpp, ScenePrefabPropertyOverrideApplier.cpp, ScenePrefabPropertyReverter.cpp, ScenePrefabOverrideMutationService.cpp, ScenePrefabVariantOverrideMutationService.cpp, ScenePrefabVariantOverrideService.cpp i ScenePrefabTemplateOverrideService.cpp, aby presence i wszystkie pola brały udział w apply/revert/variant.
- M ScriptSceneComponentApi.cpp i EngineLibraryScriptComponentAccess.hpp, jeśli generic component access ma zachować kompletny katalog.

HistoryRibbonComponent jest checklistowym analogiem ścieżki prefab, AudioSourceComponent analogiem edytowalnego asset-reference component. Akceptacja etapu komponentu ma wymusić, że żaden z powyższych adapterów nie został pominięty.

### 9.4. Particli core i plugin

| Plik | Klasa/odpowiedzialność |
|---|---|
| N sources/plugins/particli/ParticliModule.hpp/.cpp | metadata Rendering.Particli/PreDefault, Scene*→handle, ABI exports |
| N sources/plugins/particli/ParticliSceneSystem.hpp/.cpp | backend register/reconcile/fixed PostSimulation/detach |
| N runtime/ParticliCompiledEffect.hpp | immutable program i dependency/pipeline keys |
| N runtime/ParticliEffectCompiler.hpp/.cpp | source → compiled, capability-independent validation |
| N runtime/ParticliSimulationBackend.hpp/.cpp | instances, command/event queues, queries, revisions |
| N runtime/ParticliEffectInstance.hpp/.cpp | niezależny czas/RNG/emitter pools/reload |
| N runtime/ParticliEmitterPool.hpp/.cpp | dense SoA, capacity, stable compaction/history links |
| N runtime/ParticliModuleProgram.hpp/.cpp | fazowane typed operations |
| N runtime/ParticliSimulationStep.hpp/.cpp | wspólny fixed-step kernel |
| N runtime/ParticliSpawnSampler.hpp/.cpp | uniform solid-angle cone i deterministic seed derivation |
| N runtime/ParticliEventQueue.hpp/.cpp | ordered bounded events/sub-emitter depth |
| N runtime/ParticliRenderSnapshotBuilder.hpp/.cpp | packed value snapshot i tombstone |
| N runtime/modules/InitialVelocityModule.cpp, GravityModule.cpp, WindModule.cpp, DragModule.cpp, ColorOverLifeModule.cpp, SizeOverLifeModule.cpp, AlphaOverLifeModule.cpp, CollisionPlaneModule.cpp, SubEmitterModule.cpp | komplet executorów v2 |

### 9.5. Renderer i shadery

Nowe:

- N include/kb/render/particles/ParticleRenderer.hpp oraz src/particles/ParticleRenderer.cpp — sync/release/build/submit.
- N ParticleRenderBatchBuilder.hpp/.cpp — batch key, bounds/culling, compact uploads.
- N ParticleSorter.hpp/.cpp — stable per-view sort.
- N ParticleGpuResources.hpp/.cpp — renderer-only buffers/programs/uniforms.
- N ParticlePipelineState.hpp/.cpp — blend/depth/output/capability mapping.
- N ParticleBackendClassifier.hpp/.cpp — CPU/GPU policy state i reasons.
- N src/scene/pipeline/SceneTransparentDrawQueue.hpp/.cpp — wspólna kolejka transparent mesh/particles.
- N output/ParticleBillboardSubmitter.*, ParticleStretchedBillboardSubmitter.*, ParticlePointSpriteSubmitter.*, ParticleMeshSubmitter.*, ParticleTrailSubmitter.*, ParticleRibbonSubmitter.*, ParticleBeamSubmitter.*, ParticleVolumetricSubmitter.*.
- N gpu/ParticleGpuSimulation.hpp/.cpp.
- N shaders vs_particle_billboard.sc, fs_particle_billboard.sc, vs_particle_mesh.sc, fs_particle_mesh.sc, vs_particle_strip.sc, fs_particle_strip.sc, vs_particle_volumetric.sc, fs_particle_volumetric.sc, cs_particle_spawn.sc, cs_particle_update.sc, cs_particle_compact.sc, cs_particle_indirect_finalize.sc.
- N wymagane skompilowane warianty w prebuilt_shaders/{dxbc,dxil,spirv,glsl,essl,metal}; brak profilu jest błędem manifestu.

Modyfikowane:

- M SceneParticleRenderSynchronizer.hpp/.cpp — retained snapshot/batches, zero per-particle MeshProxy.
- M Renderer.hpp/.cpp — ownership, per-scene release, stats, sync raz na snapshot revision.
- M RenderScene.hpp/.cpp i SceneRenderTypes.hpp — particle batch proxy/statistics, bez invalidacji mesh groups.
- M SceneMeshSubmitter.*, scene/submit/SceneMeshDrawCommandSubmitter.* i scene/pipeline/MeshPipelineCommandBuilder.cpp — rozdzielenie transparent build/submit.
- M scene/pipeline/MeshPipelinePassPolicy.cpp, resources/RenderResources.hpp — pełne depth/blend, w tym jawny Subtractive albo Unsupported.
- M RendererCapabilityReport.hpp/.cpp i SceneGpuDrivenFeatureState.hpp — particle compute/indirect classification.
- M ShaderManifest.hpp/.cpp, shaders/varying.def.sc i sources/renderer/CMakeLists.txt — shader variants/staging.

### 9.6. Editor core, adaptery i content

Editor core w sources/plugins/particli/editor:

- N ParticliEditorDocument.hpp/.cpp, ParticliEditorCommandStack.hpp/.cpp.
- N ParticliAssetGateway.hpp/.cpp, ParticliEditorValidator.hpp/.cpp, ParticliBakeService.hpp/.cpp.
- N ParticliRecipeLibrary.hpp/.cpp, ParticliPreviewSession.hpp/.cpp, ParticliPreviewTelemetry.hpp.
- N ParticliCurveEditorModel.hpp/.cpp, ParticliGradientEditorModel.hpp/.cpp.
- N ParticliEmitterListModel.hpp/.cpp, ParticliModuleStackModel.hpp/.cpp, ParticliOutputModel.hpp/.cpp, ParticliEventBindingModel.hpp/.cpp, ParticliDependencyModel.hpp/.cpp.

Adaptery w sources/editor:

- N src/private/rendering/ParticliEditorPanelLayout.hpp, ParticliEditorPanelRenderer.hpp, ParticliCurveEditorRenderer.hpp, ParticliGradientEditorRenderer.hpp oraz odpowiadające src/rendering/*.cpp.
- N src/private/app/particli/EditorParticliPointerController.hpp, EditorParticliInputHandler.hpp, EditorParticliAssetDropHandler.hpp i odpowiadające src/app/particli/*.cpp.
- N src/private/scene/particli/EditorParticliSession.hpp, EditorParticliPreviewScene.hpp, EditorParticliEditCommand.hpp, EditorParticleEffectReferenceFinder.hpp i odpowiadające src/scene/particli/*.cpp.
- N src/private/inspection/InspectorParticleEffectComponentModel.hpp/.cpp oraz Interaction.
- N src/private/platform/win32/EditorParticleEffectAssetPickerDialog.hpp; implementacja korzysta z istniejącego AssetPickerWindow w EditorMeshAssetPickerDialog.cpp.
- N ParticliDocumentCloseGuard — wspólna decyzja Save/Discard/Cancel.

Modyfikowane dokładne punkty hosta:

- DockTypes.hpp, DefaultDockWorkspace.cpp, PanelContentRenderer.cpp, EditorHostSurfaceLayoutResolver.cpp, FloatingWindowBackBufferPainter.cpp, DockPanelChromeRenderer.cpp, DockWorkspaceTabStripRenderer.cpp.
- EditorApplicationMessageLoop.cpp i EditorWindowMessageRouter.cpp; wszystkie left/right/down/up/double-click/move/wheel routery dotykane tylko przez delegację do feature controller.
- EditorSceneContext.hpp/.cpp i EditorSceneDocumentLifecycle.cpp; Context nie zawiera logiki Particli.
- EditorWindowLifecycleHandler.cpp i tab-close route dla complete dirty guard.
- EditorAssetBrowserTypes.hpp, EditorAssetBrowserContextMenuState.cpp, EditorAssetBrowserContextCommandExecutor.cpp, EditorAssetBrowserDoubleClickHandler.hpp/.cpp.
- ProjectFilesAssetIconResolver.hpp/.cpp; używa istniejącego HeroIconKind::Bolt.
- EditorPointerDragState.hpp, EditorPointerDragSourceResolver.cpp, EditorPointerDropHandler.cpp.
- InspectorPanelState.hpp, InspectorComponentCatalog.cpp, InspectorPanelInteraction.cpp i InspectorPanelRenderer.cpp.
- EditorMeshAssetPickerDialog.cpp; wydziela wspólny filter/build bez duplikowania dialogu.
- EditorMaterialColorPickerDialog.hpp/.cpp i callsites materialu; najmniejszy konieczny refactor zmienia nazwę na ogólne EditorColorPickerDialog, ponieważ widget nie jest własnością materialu.
- IEditorCommand.hpp i EditorCommandStack.cpp dla document history focus/dirty routing.
- EditorHeadlessAutomation.cpp i tests/HeadlessAutomationScenario.json.

Content:

- N sources/plugins/particli/content/Recipes/{BloodSplatter,MuzzleFlash,BulletTrail,Explosion,ImpactSparks,Rain,Snow,Leaves,FireBurst,FrostNova,ArcaneSparks,CoinBurst,LevelUpAura,SmokeStack,SparksShower}.kbvfx.
- N tylko wymagane Particli materials/textures oraz ich licencje; żadnych kopii z VerthEngineProd bez jawnej weryfikacji licencji.

ParticliBakeService zapisuje wyłącznie atomowe pliki pochodne do Project/Saved/ParticliCache/<compilerVersion>/<semanticHash>.kbvfxc. Cache key obejmuje canonical source, resolved dependency hashes, compiler version, platform i renderer capability schema. Runtime source load pozostaje poprawny bez cache; błędny cache jest usuwany i kompilowany ponownie z jawnym diagnostic, nie traktowany jak source fallback.

## 10. Rendering/runtime — mapowanie większych elementów

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| global g_pool_manager i shared pool | ParticliEffectInstance | immutable compiled effect może być shared; czas/RNG/SoA wyłącznie per handle | SimulationBackend, EffectInstance, EmitterPool | dwa playbacki tego samego assetu nigdy nie współdzielą live state |
| variable ctx delta | Scene fixed loop | PostSimulation, dokładny scene fixedDelta/stepIndex, bez lokalnego catch-up | ParticliSceneSystem/SimulationStep | 30/60/144 render Hz daje ten sam hash N steps |
| Kanku LCG/SoA | dense preallocated SoA | RNG hash effect/instance/emitter/spawn ordinal; stable compaction zachowuje links | EmitterPool, SpawnSampler | scheduling order nie zmienia hash; zero steady allocations |
| pełny-cap scan/first dead | dense [0,liveCount) | spawn append, stable-compaction lub swap-remove zależnie od output; explicit cap overflow | EmitterPool | koszt zależy od live, nie capacity; deterministic overflow |
| aktywne Gravity/Wind/Drag/Plane | typed module plan | komplet authorowanych modułów albo compile reject | ModuleProgram/modules | każdy accepted module ma behavior golden |
| event bus depth 3, niewpięty | bounded event queue | depth 3, per-step budget, deterministic order | ParticliEventQueue | cycle/depth/overflow jawnie raportowane |
| CPU 6 vertices/particle | instanced compact stream | quad expand w vertex shader | BillboardSubmitter/shaders | 100k nie zwiększa meshProxyCount, 1 compatible draw |
| sort tylko per pool | per-view stable sort + common transparent queue | additive bez sortu; alpha stable depth/id; mesh interleave | ParticleSorter, SceneTransparentDrawQueue | camera-dependent order poprawny dla 2 viewportów |
| output enum Billboard/Mesh/Ribbon/PointSprite i test-only modes | pełne output executors | implementować etapami; validator nie akceptuje niewpiętego executora | output submitters | output capability matrix i goldens |
| stretched prototype | velocity-based shader variant | previous position/velocity, min length/stretch | Stretched submitter | orientation i zero-velocity golden |
| trail/ribbon slot order bugs | history/spawn ordinal | bounded ring history i degenerate breaks | Trail/Ribbon submitters | compaction nie zmienia kolejności |
| beam noise od render frame | fixed-step deterministic noise | seed/fixedStep/segment | Beam submitter | render FPS nie zmienia geometrii |
| fake volumetric quad | depth-aware ellipsoid raymarch | quality steps, early depth termination | Volumetric submitter | Low/High telemetry i depth intersection golden |
| niemożliwy GPU threshold + silent fallback | renderer-owned GPU visual backend | policy, capability reason, no fence/readback, dispatch raz/scene step | GpuSimulation/Classifier | required fails; preferred reports fallback; 2 viewports = 1 dispatch |
| overlay globals | core snapshot + renderer epoch | retained immutable snapshot, tombstone przed DLL unload | SnapshotChannel, ParticleRenderer | 100 reload/open-close cycles bez ghostów/handle leak |

Etapy outputów:

1. Billboard, StretchedBillboard, PointSprite i flipbook/soft-depth.
2. Mesh instanced, z istniejącym RenderMesh/RenderMaterial, LOD/shadow policy.
3. Trail/Ribbon/Beam z renderer-owned dynamic strip buffers.
4. GPU visual simulation po CPU conformance i capability matrix.
5. Volumetric jako rzeczywisty raymarch; do tego momentu output jest Unsupported, nie billboard fallback.

Subtractive nie ma obecnie pełnego odpowiednika w RenderResources. Particli dodaje RenderMaterialBlendMode::Subtractive o zdefiniowanej semantyce RGB = dstRGB - srcRGB × srcAlpha, alpha bez zmiany, z depth read/no write. Shader/pipeline i golden testy są częścią etapu bazowego; żadne mapowanie na Alpha nie jest dozwolone.

## 11. Kolejność implementacji, zależności i bramy

    0 Characterization
      └─ 1 Asset v2
          └─ 2 Component + backend ABI + plugin lifecycle
              └─ 3 CPU fixed-step
                  └─ 4 Snapshot channel
                      └─ 5 Baseline particle renderer
                          ├─ 6 Editor shell + preview
                          │   └─ 7 Complete authoring UX
                          ├─ 8 Mesh output
                          │   └─ 9 Trail/Ribbon/Beam
                          └─ 10 GPU visual simulation
                                  └─ 11 Volumetric + release hardening

Etap może się rozpocząć dopiero po przejściu bram jego poprzedników. Stage 6 może być rozwijany równolegle z 8 dopiero po stage 5; nie może tworzyć mockowego renderera lub drugiego preview kernel.

### Etap 0 — testy charakterystyk istniejącego 21kb

Zakres:

- zamrozić fixture płaskiego .kbvfx z HeadlessAutomationScenario.json;
- golden hash starego CPU behavior dla wspólnego podzbioru;
- scharakteryzować obecne Script Particles.*, proxy-per-particle i scene lifecycle;
- dodać statystyczny test cone sampler, który ujawni obecny liniowy rozkład kąta zamiast równomiernego solid angle;
- scharakteryzować 64→32-bit seed truncation oraz true/no-op Emit.

Kryteria:

- testy najpierw opisują zachowanie, które musi pozostać, oraz jawnie oznaczają zachowania naprawiane w kolejnych etapach;
- żaden production behavior nie jest zmieniony;
- legacy fixture i spodziewana migracja są reviewowalne.

### Etap 1 — .kbvfx v2, migracja i dependencies

Zależność: etap 0.

Zakres:

- schema/types, strict IO, legacy migration, structural validator;
- dependency discovery/validation dla material, material instance, mesh, atlas i sub-effect;
- canonical writer i atomic fault injection;
- 15 recipe assets przechodzą parser/validator, ale UI ich jeszcze nie renderuje.

Kryteria:

- v1 → v2 model zachowuje wszystkie stare pola; load nie zapisuje;
- v2 save→load→save jest byte-stable;
- malformed, duplicate ID/key, unknown enum/key, NaN/Inf, limit i future schema zwracają line + property path;
- dependency graph jest kompletny, wrong type/cycle odrzucone;
- fuzzowany deterministycznie parser nie crashuje ani nie alokuje ponad limit 512 KiB;
- stare asset tests i Headless fixture pozostają zielone.

### Etap 2 — komponent, provider ABI i lifecycle pluginu

Zależność: etap 1.

Zakres:

- ParticleEffectComponent w całej scene/prefab/reflection/override ścieżce;
- ParticlePlayback/IParticleSimulationBackend i typed results;
- Rendering.Particli PreDefault, attach/detach/reload;
- usunięcie ownership symulacji ze ScriptRuntimeSceneSystem;
- project bootstrap i jawna migracja istniejącego projektu.

Kryteria:

- v32 scene ładuje się; v33 component round-tripuje;
- prefab capture, bulk instantiate, compare/hash, apply/revert, variant i presence override przechodzą;
- optional component matching wykrywa extra/missing ParticleEffect oraz późniejsze istniejące komponenty;
- scena bez providera zwraca BackendUnavailable, a nie no-op/freeze;
- 100 attach/detach/reload pozostawia null backend, pusty event queue i brak DLL-owned objectów;
- system Particli rejestruje się PreDefault; script-only i no-script scene mają poprawny lifecycle;
- editor/CLI nie uruchamia starego .kbvfx bez jawnego wpisu/migration action.

### Etap 3 — deterministyczny CPU fixed-step i wszystkie baseline modules

Zależność: etap 2.

Zakres:

- EffectInstance, dense SoA, command/event queues, module compiler/kernel;
- Continuous/Burst/Prewarm, independent instances, hot generation cache;
- InitialVelocity/Gravity/Wind/Drag/Color/Size/Alpha/Collision/SubEmitter;
- typed Play/Pause/Restart/Stop/Emit/overrides;
- owner/follow/death policies i telemetry limits.

Kryteria:

- ten sam asset/seed/commands po N fixed steps daje identyczny snapshot hash przy render frame 30/60/144 Hz;
- preview-kernel fixture i runtime fixture są identyczne;
- dwa playbacki tego samego compiled effect mają niezależny czas/RNG/live set;
- Play/Restart resetuje dokładnie określony stan; Stop drainuje;
- command/event order jest stabilny; depth 3 i budgets jawnie odrzucają nadmiar;
- uniform cone przechodzi test statystyczny oraz zamrożony seed golden;
- parameter-compatible reload zachowuje particle IDs, topology reload zwraca Restarted, invalid reload StaleAfterInvalidReload z last-good;
- po warmup zero heap allocations per fixed step.

### Etap 4 — core-owned immutable render snapshot

Zależność: etap 3.

Zakres:

- packed CPU stream 48–64 B/particle potwierdzony static_assert;
- bounded retained snapshot channel oparty o sprawdzony publisher pattern, monotonic revision, backend epoch i tombstone;
- renderer query nie zależy od plugin headerów.

Kryteria:

- concurrent reader nie widzi torn/decreasing revision;
- dwa viewporty czytają ten sam fixedStepIndex;
- stary retained snapshot może przeżyć publikację następnego;
- po unload destrukcja snapshotu nie wywołuje kodu DLL;
- tombstone usuwa całą scene bez ghostów.
- po warmup publikacja nie alokuje; zajęcie wszystkich slotów daje SnapshotBackpressure i zachowuje ostatnią pełną rewizję.

### Etap 5 — billboard/stretched/point/flipbook renderer

Zależność: etap 4.

Zakres:

- usunięcie proxy-per-particle;
- ParticleRenderer, compact upload, batch/cull, common transparent queue;
- Billboard, StretchedBillboard, PointSprite, flipbook, soft particles;
- wszystkie blend/depth/sort, w tym Subtractive;
- stats/budgets i pełny shader manifest.

Kryteria:

- 100 000 particles nie zwiększa RenderScene::MeshProxyCount;
- jeden kompatybilny emitter/material daje jeden draw poza udokumentowanym splitem transient capacity;
- alpha/premul/modulate mają stable per-view back-to-front, additive nie sortuje; transparent mesh i particles interleave;
- dwa viewporty mają poprawne billboardy/sort bez podwójnej symulacji;
- transient shortage zwiększa droppedParticleCount z powodem;
- release scene/shutdown zeruje wszystkie particle GPU handles;
- visual goldens obejmują alignment, stretch zero/nonzero velocity, flipbook, soft-depth i 6 blend modes;
- po warmup zero heap allocations w batch build.

### Etap 6 — editor document shell i wspólne preview

Zależność: etap 5.

Zakres:

- panel ID 14, docking/floating/present;
- document/gateway/history/dirty guard;
- create/open/save/revert/validate/bake;
- isolated preview scene z PublishRuntimeAsset i tym samym backendem/rendererem;
- loading/error/empty/ready overlays.

Kryteria:

- create→open→edit→undo/redo→atomic save→close→reopen;
- first Save aktualizuje session path; restart przywraca dokument i layout;
- dirty prompt działa dla open/revert/tab close/floating close/app close/project switch;
- unsaved edit zmienia preview bez disk write;
- resize/dock/float nie resetuje preview ani kamery;
- preview close zwalnia Scene/backend/snapshot/GPU;
- Bake tworzy cache o source/dependency hash i invaliduje go po zmianie.

### Etap 7 — pełne authoring UX i host integration

Zależność: etap 6.

Zakres:

- emitters/settings/recipes/modules/output/events/dependencies/diagnostics/status;
- curve i gradient drawers;
- Project Files create/context/icon/double-click;
- picker/reveal/D&D, Scene placement i Inspector component;
- keyboard/focus/scroll/hit-test oraz wszystkie stany z sekcji 8.

Kryteria:

- każda pozycja tabel sekcji 8 ma unit layout/model/hit test albo scenariusz headless;
- 15 recipes ma właściwe kategorie, multi-emitter thumbnail i atomic append/undo;
- emitter/module click + D&D reorder zachowuje stable IDs/order przez save/reload;
- każdy variant property round-tripuje i jest undoable; continuous gesture = jedna komenda;
- invalid D&D type nie mutuje; valid Scene drop jest jedną undoable entity/component transaction;
- Find References odnajduje scene, prefab i unsaved working copy;
- Inspector picker/drop/Missing/reveal oraz scene/prefab/runtime round-trip przechodzą;
- screenshoty 900×560, 1280×720 i 4K DPI potwierdzają hierarchię 2:1 i brak clippingu.

### Etap 8 — Mesh output

Zależność: etap 5.

Zakres:

- RenderMesh dependency, compact particle TRS/color/custom stream;
- instanced sections/LOD/shadow/culling policy bez mesh proxy map.

Kryteria:

- N particles jednego mesh/material = jeden draw na section/LOD;
- wrong/missing mesh blokuje compile/Bake;
- LOD i shadow policy mają renderer tests/goldens;
- hot reload i scene release nie zostawiają resource refs.

### Etap 9 — Trail, Ribbon i Beam

Zależność: etap 8 oraz history fields z etapu 3.

Zakres:

- bounded cadence/distance trail ring;
- ribbon spawn ordinal/group i breaks;
- authored/bound beam endpoints i fixed-step noise;
- renderer-owned dynamic vertex/index buffers.

Kryteria:

- compaction nie zmienia trail/ribbon order;
- ring/segment/cpu/gpu budgets są twarde i diagnostyczne;
- beam geometry jest identyczna przy różnych render FPS;
- multi-viewport, hot reload i release tests nie wykazują stale buffers;
- visual goldens obejmują breaks, camera crossing i endpoint motion.

### Etap 10 — GPU visual simulation

Zależność: etapy 3–5.

Zakres:

- renderer-owned device SoA/ping-pong/alive counters/indirect args;
- spawn/control programs z fixedStepIndex;
- classifier GpuVisualPreferred/Required, capability/fault states;
- renderer publikuje capability epoch do core channel; backend required/pending/restart policy konsumuje go na fixed boundary;
- compute shadery i pełne profile.

Kryteria:

- forced capability matrix daje dokładny backend/fallback/fault reason;
- Required bez compute/shader/resource kończy Create/Play błędem;
- Preferred raportuje CPU restart/fallback; nigdy silent;
- dwa viewporty wykonują jedną sekwencję dispatch per scene fixed step;
- consumer ack wykonuje każdy journaled step dokładnie raz; brak viewportu ponad 64 kroki daje GpuCatchupOverflow oraz jawny restart/warmup;
- brak fence wait i synchronous readback w frame capture;
- CPU/GPU conformance mieści się w zdefiniowanej tolerancji dla modułów visual-only;
- device/shader fault nie zmienia backendu bez jawnego restart disposition;
- ReleaseScene zeruje compute/dynamic/indirect handles.

### Etap 11 — Volumetric i release hardening

Zależność: etap 10 dla GPU quality path; CPU particles mogą zasilać render.

Zakres:

- depth-aware ellipsoid impostor/raymarch, quality levels;
- kompletna telemetryka, budgets, content validation i packaging;
- 100-cycle stress preview/plugin/scene/device lifecycle.

Kryteria:

- depth early termination i opaque intersection goldens;
- Low/High zmienia wyłącznie udokumentowaną liczbę kroków/jakość i stats;
- unsupported capability daje explicit error, nie quad fallback;
- 100 cycles open/close, scene load/unload i plugin reload: resident CPU/GPU memory wraca do baseline;
- wszystkie shipped recipes/materials/shadery są wykrywane w clean install;
- release build przechodzi sanitizers/validation dostępne na wspieranej konfiguracji Win32.

## 12. Test matrix i najmniejsza weryfikacja

### 12.1. Focused targety

| Target | Zakres |
|---|---|
| kb_particli_asset_tests | schema, parser, migration, validation, dependencies, component/prefab codec |
| kb_particli_runtime_tests | module lifecycle, backend API, fixed-step, pools, modules, events, hot reload |
| kb_particli_renderer_tests | snapshot consumption, batches, sort, outputs, resources, offscreen goldens |
| kb_particli_editor_tests | document/model/layout/hit/input/dirty/D&D/picker/preview |
| kb_particli_sim_benchmarks | SoA step/spawn/modules/events 10k/100k/many emitter |
| kb_particli_renderer_benchmarks | upload/sort/batch/submit/multi-viewport/output |

Istniejące suites dostają tylko przekrojowe regresje:

- kb_engine_tests: Scene version/prefab, Script API i EngineModule reload;
- kb_renderer_tests: Renderer lifecycle/common transparent queue/shader manifest;
- kb_editor_tests: host route/docking/Inspector/Project Files;
- kb_editor_headless_automation_scenario: pełny E2E.

### 12.2. Przypadki obowiązkowe

Unit:

- every parser record/enumeration/limit oraz canonical float/quoted UTF-8;
- migration każdego pola v1;
- curve/gradient key ordering, evaluation i editor commands;
- RNG/statistical cone, seed/reset, pool compaction i stable order;
- module phase/order/duplicate policy;
- batch key, culling bounds, all blend/depth/sort state;
- all editor layout geometry i hit regions.

Integration:

- Scene v32/v33, prefab text/binary/bake/bulk/override/variant;
- dynamic/static module attach/detach/reload i missing provider;
- Script API przed/po fixed step oraz OnParticleSystemFinished before Tick;
- AssetManager PublishRuntimeAsset/LoadGeneration/invalid last-good;
- retained snapshot across publish/unload;
- renderer two-viewports, transparent interleave, device/resource teardown.

Editor/E2E:

1. create .kbvfx;
2. open Particli;
3. add/rename/reorder emitter;
4. add/reorder/edit every module category, curve i gradient;
5. recipe filter/append/drag;
6. preview play/pause/restart/stop, orbit/zoom;
7. Validate/Bake/Save/Revert/dirty close;
8. material/texture/mesh picker/drop/reveal;
9. Apply oraz .kbvfx drop do Scene;
10. Inspector edit, scene/prefab save/reload;
11. runtime autoplay/manual API/event/render stats;
12. disable/reload plugin i verify recovery/cleanup.

Performance:

- zero allocator calls po warmup w fixed step i batch builder;
- brak per-particle RenderScene proxy oraz CPU quad expansion;
- draw count zależy od batch keys, nie particle count;
- memory jest bounded przez validated capacities;
- benchmark osobno mierzy compile, simulate, snapshot pack, upload, sort, submit;
- pierwszy merge na stałym, nazwanym runnerze zapisuje baseline; kolejne zmiany nie mogą pogorszyć mediany ani p95 o >10% bez zaakceptowanego pomiaru/uzasadnienia. Nie importować niezmierzonych 1.5/4 ms z testów Kanku.

### 12.3. Komendy weryfikacji etapów

Przykład dla istniejącego katalogu build; konfigurację należy dostosować tylko do używanego presetu:

    cmake --build build --config Debug --target kb_particli_asset_tests
    ctest --test-dir build -C Debug -R "^kb_particli_asset_tests$" --output-on-failure

Następne etapy budują tylko właściwe targety:

    cmake --build build --config Debug --target kb_particli_runtime_tests
    cmake --build build --config Debug --target kb_particli_renderer_tests
    cmake --build build --config Debug --target kb_particli_editor_tests

Po zmianach w cienkich host adapters dodatkowo najmniejszy właściwy host target:

    cmake --build build --config Debug --target kb_editor
    ctest --test-dir build -C Debug -R "kb_particli_|kb_editor_headless_automation_scenario" --output-on-failure

Nie wykonywać pełnego buildu silnika. Pełny build jest dopuszczalny dopiero przed release candidate, jeżeli focused targets i ich link graphs nie potwierdzają packagingu; przed uruchomieniem trzeba wskazać konkretną niepokrytą zależność.

## 13. Kanoniczne mapowanie architektoniczne Kanku → Particli

| Kanku reference | 21kb target | Sposób implementacji | Pliki/klasy | Test akceptacyjny |
|---|---|---|---|---|
| prywatna DLL i descriptor | IEngineModule provider | Rendering.Particli, PreDefault, per-scene handle, ABI v1 | ParticliModule, root/plugin CMake, EditorPluginCatalog | dynamic/static load, dependency order, 100 reload cycles |
| KankuEffectDocument, 8 emitters/16 kart/32 events | ParticleEffectAsset v2 | typed variants, stable IDs, ordered records, istniejące Curve/Gradient | AssetSchema/IO/Validation/Migration | legacy + v2 roundtrip i full invalid matrix |
| słaby line parser, panel direct write | asset_io header + atomic IO | strict 21kb ParticleEffect 2, max size, diagnostics, canonical writer | ParticleEffectAssetIO | fault-injection nie narusza starego pliku |
| cooker/runtime payload niepodpięty i tracący pola | ParticliEffectCompiler | jedno immutable compiled source dla preview/runtime, realny bake cache | Compiler, BakeService | każdy accepted field ma executor/output; cache hash invalidation |
| in-memory recipes ograniczone do 4 emiterów | zwykłe read-only .kbvfx | ten sam loader/compiler, deep-copy wszystkich emiterów | RecipeLibrary/content | wszystkie 15 kompiluje; append zachowuje komplet |
| Kanku global/shared pools | per-instance backend state | immutable program shared, mutable state per handle | SimulationBackend/EffectInstance/EmitterPool | independent playback test |
| Kanku variable frame tick | Scene fixed-step | PostSimulation, command boundary i PostFixed script event drain | ParticliSceneSystem, ScriptRuntimeSceneSystem | cross-render-rate hash i same-frame event |
| Kanku ParticleEffect component | core ParticleEffectComponent | scene/prefab/reflection/Inspector, runtime handle side table | component manifest sekcji 9.3 | v33/prefab/override/Inspector/runtime E2E |
| preview używa podobnych pul, ale osobnej logiki czasu | isolated preview Scene | PublishRuntimeAsset + dokładnie ten sam compiler/kernel/snapshot/renderer | PreviewSession/EditorParticliPreviewScene | preview/runtime hash parity |
| overlay czyta mutable pool | core retained snapshot | bounded immutable generations, tombstone/epoch | SnapshotChannel/SnapshotBuilder | concurrent read i DLL unload lifetime |
| billboard CPU vertices | particle batch renderer | compact streams, VS expansion, common transparency | ParticleRenderer/output submitters | no mesh proxy, batch/draw/golden gates |
| martwa GPU path | renderer-owned GPU visual | capability classifier, fixed-step commands, no fence/readback | GpuSimulation/Classifier | capability/fault/multi-viewport gates |
| asset browser/scene/inspector hostowe | istniejące 21kb surfaces | jawne adaptery zamkniętych enumów/routerów | editor manifest sekcji 9.6 | create/open/drop/apply/inspect/save E2E |
| jeden centralny panel | DockPanelKind::ParticliEditor | center ID 14, dock/float/resize/session | Dock/Panel/MessageLoop adapters | layout/session/viewport tests |

## 14. Evidence index

Najważniejsze dowody Kanku:

- E:\VerthEngineProd\Source\Verth.Plugin.Kanku\Private\Source\KankuEffectEditorPanel.cpp:435-508, 514-633, 635-767, 769-939, 941-1058, 1060-1400 — całe UI i akcje.
- E:\VerthEngineProd\Source\Verth.Plugin.Kanku\Private\Include\KankuEffectDocument.h:16-244 — output enum, parametry, emitery, karty, events i limity.
- E:\VerthEngineProd\Source\Verth.Plugin.Kanku\Private\Source\KankuPlugin.cpp:187-287, 289-448, 450-563 — komponenty, runtime, pusty importer i lifecycle.
- E:\VerthEngineProd\Source\Verth.Plugin.Kanku\Private\Include\KankuParticlePool.h:4-33 oraz Private\Source\KankuSimulationTick.cpp:54-277 — SoA, spawn i rzeczywiście wykonywane moduły.
- E:\VerthEngineProd\Source\Verth.Plugin.Kanku\Private\Source\KankuRenderOverlayProvider.cpp:40-175 — aktywny billboard overlay i legacy path.
- E:\VerthEngineProd\Source\Verth.Plugin.Kanku\Private\Source\KankuGpuSimulationPipeline.cpp:15-78 — niemożliwy próg, wait i CPU-authoritative fallback.
- E:\VerthEngineProd\Source\Verth.Plugin.Kanku\Private\Source\KankuCooker.cpp:85-194, 223-267 oraz KankuRuntimeLoader.cpp:65-118 — niekompletny, nieużywany cooked path.
- E:\VerthEngineProd\Source\Verth.Editor\Panels\Private\PluginUiBridge.cpp:198-1409 i PluginBackedEditorPanel.cpp:80-1230 — host UI, preview, scroll, D&D i camera.
- E:\VerthEngineProd\Source\Verth.Editor\Panels\Private\ProjectFilesPanel.cpp:595-1511, InspectorPanel.cpp:1099-2269 oraz Source\Verth.Editor\Scene\Private\EditorSceneAuthoring.cpp:1126-1209 — Project Files/Inspector/Scene workflows.

Najważniejsze dowody 21kb:

- sources/engine/include/engine/scene/ParticleEffectAsset.hpp:18-50, ParticleEffectAssetIO.hpp:11-24 i src/scene/ParticleEffectAssetIO.cpp:103-214 — istniejący v1 i atomowy zapis.
- sources/engine/include/engine/assets/IAssetLoader.hpp:31-50 oraz AssetManager.hpp:111-150, 206-210, 290-294 — dependency API, working copies, generations i DLL type lifetime.
- sources/engine/src/scene/Scene.cpp:83-157 — loader, module attach/detach i shutdown order.
- sources/engine/include/engine/scene/SceneParticleSystems.hpp:37-139, src/scene/SceneParticleSystemService.cpp:24-411 i src/private/scene/SceneState.hpp:758-798 — obecny public facade, symulacja i AoS.
- sources/engine/src/script/ScriptRuntimeSceneSystem.cpp:161-258, 420-473 — obecne błędne ownership Advance oraz właściwe miejsce event drain.
- sources/engine/include/engine/scene/SceneSystem.hpp:18-51 i src/scene/SceneRuntime.cpp:401-426 — fixed phases/loop.
- sources/engine/include/engine/audio/AudioPlayback.hpp:156-252, src/audio/AudioPlayback.cpp:93-121 i sources/plugins/audio_miniaudio/MiniaudioModule.cpp:23-43 — kanoniczna granica providera.
- sources/engine/include/engine/core/ReadSnapshotQueue.hpp:11-50 — immutable retained snapshot pattern.
- sources/renderer/src/scene/SceneParticleRenderSynchronizer.cpp:41-145, include/kb/render/scene/RenderScene.hpp:53-86, 271-394 i src/Renderer.cpp:271, 809-822, 1863-1884 — obecny proxy bridge i renderer lifecycle.
- sources/editor/include/kb/editor/docking/DockTypes.hpp:17-31, src/docking/DefaultDockWorkspace.cpp:9-33, src/rendering/PanelContentRenderer.cpp:296-366 i src/app/EditorApplicationMessageLoop.cpp:275-544 — zamknięty panel/present routing.
- sources/editor/src/scene/EditorPluginCatalog.cpp:41-74, src/project/EditorProjectBootstrap.cpp:36-60 i sources/editor/CMakeLists.txt:923-971 — katalog, default plugins i build paths.
- sources/engine/include/engine/scene/SceneDocument.hpp:12-37, src/scene/asset/io/SceneAssetComponentCodec.cpp:19-133 oraz src/private/scene/prefab/ScenePrefabOptionalComponentMask.hpp:22-134 — persistence i wykryta luka optional-mask.

## 15. Ryzyka, ograniczenia i decyzje zamknięte

- Renderer i editor 21kb są obecnie Win32-only; potwierdza to główny CMakeLists.txt:284-319. CPU core ma pozostać przenośny, shadery mają komplet profili, ale release E2E Particli jest Win32, dopóki osobny projekt nie usunie hostowego gate.
- 21kb nie ma editor-panel ABI. W tej implementacji statyczny adapter hosta jest decyzją ostateczną; budowa ogólnego plugin UI ABI byłaby scope creep.
- Loader/payload/component/snapshot ABI pozostają w kb_engine. Umieszczenie ich w unloadowalnej DLL jest zakazane przez lifetime AssetManager.
- Rendering.Particli jest obowiązkowym providerem dla runtime .kbvfx. Nowe projekty mają go enabled; stare dostają jawną migrację. Nie utrzymujemy drugiego, cichego legacy backendu.
- Timeline nie powstaje: nie istnieje w Kanku. Curve/Gradient/Events powstają jako udokumentowane rozszerzenia, ponieważ ich dane już istnieją albo są potrzebne kompletnemu v2.
- Kanku graph schema, AutoBake, GPU pipeline i zaawansowane output prototypes nie są dowodem działającego UX/runtime. Plan bierze ich intencję, ale wymaga osobnych capability i acceptance gates.
- Assety graficzne/dźwiękowe z VerthEngineProd nie są kopiowane bez osobnej weryfikacji praw. Recipes odtwarzają zachowanie w nowym .kbvfx i używają assetów z jednoznaczną licencją.
- PrefabOptionalComponentMask ma obecnie niespójne mapowanie bit/index dla późniejszych komponentów. Etap 2 zawiera najmniejszą potrzebną korektę i test; bez niej nie wolno uznać component integration za poprawną.
- Shader generation jest opcjonalne w obecnym buildzie, więc Particli musi dostarczyć i manifestowo sprawdzić prebuilt profiles. Brak wariantu jest błędem packagingu.
- Absolutny budżet milisekund zostanie ustalony dopiero na nazwanym runnerze. Niezależnie od sprzętu obowiązują bramy strukturalne: bounded memory, zero steady allocations, brak per-particle proxy, brak sync readback i draw count od batch keys.

## 16. Handoff / Definition of Done implementacji

Agent implementujący nie musi ponownie odkrywać architektury. Powinien realizować etapy 0–11, używać manifestu sekcji 9 i nie zmieniać decyzji z sekcji 15 bez nowego review. Każdy etap kończy się dopiero po:

1. implementacji całego zakresu bez stubów/TODO/cichych fallbacków;
2. buildzie najmniejszego zmienionego targetu;
3. przejściu focused tests i wskazanych regressions;
4. benchmarku dla zmian hot path;
5. przeglądzie diffu, ownership, error paths, determinism i cleanup;
6. spełnieniu wszystkich kryteriów bramy etapu.

Release Particli jest gotowy dopiero po przejściu etapu 11, pełnego E2E z sekcji 12.2, clean-install content/shader testu oraz braku niewspieranego authoring field. Do tego czasu UI może udostępniać wyłącznie outputy/moduły, których etapowa capability jest zarejestrowana i przeszła bramę.
