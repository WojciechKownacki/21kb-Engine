# Engine21kbLibrary — plan biblioteki skryptowej runtime

## 1. Cel i granice

`Engine21kbLibrary` jest publiczną powierzchnią skryptowania gry dla trzech
front-endów: C++ (native behaviour), Lua oraz Visual Graph. Ma umożliwić
tworzenie pełnych gier, ale **nie jest drugim runtime'em, drugim ECS-em ani
kopią stanu sceny**.

Źródłem prawdy pozostają istniejące systemy silnika: `Scene`/ECS, runtime
renderera, fizyka, audio, input, asset registry i system skryptów.
`Engine21kbLibrary` ma być ich wersjonowanym, typowanym i sprawdzalnym
kontraktem. Każda operacja biblioteki albo wywołuje właściwy system, albo
odkłada jednoznaczną komendę do jego kolejki. Nie przechowuje ukrytego stanu,
który mógłby rozjechać się z runtime'em.

Aktualny silnik już posiada wartościowy fundament: `ScriptRuntimeHost`,
backendy Native/Lua/VisualGraph, `ScriptApiCatalog`, `ScriptApiNameRegistry`
oraz zdarzenia `Created`, `Activated`, `Ready`, `FixedTick`, `Tick`,
`LateTick`, `BeforeRender`, `AfterRender`, `Deactivated`, `Destroyed`.
Istnieją także pierwsze moduły `World`, `Transform`, `Time`, `Input`,
`Physics` i `Audio`. Ten plan rozwija je do produktu — nie zastępuje ich
równoległym mechanizmem.

### Niezmienialne zasady projektu

1. Jedna rejestracja funkcji opisuje nazwę, sygnaturę, uprawnienia, wątek,
   semantykę błędu oraz bindingi Lua i Visual Graph.
2. API operuje na stabilnych uchwytach (`EntityHandle`, `AssetRef`,
   `SceneRef`), nigdy na surowych wskaźnikach komponentów przechowywanych przez
   skrypt między klatkami.
3. Modyfikacje struktury ECS podczas iteracji są komendami zastosowanymi w
   zdefiniowanym punkcie synchronizacji; odczyty mają jasne zasady świeżości.
4. Kolejność skryptów, zdarzeń, timerów i zapytań jest deterministyczna w
   obrębie tej samej konfiguracji świata.
5. Funkcje niegotowe produkcyjnie nie są udawane. Są nieeksponowane albo
   zwracają jawny, diagnostyczny błąd kontraktu — nigdy cichy fallback.
6. Każda publiczna funkcja ma test native, test Lua, test bindingu Visual
   Graph oraz test błędu dla błędnych danych, jeśli dane wejściowe są możliwe.
7. Hot path nie wykonuje refleksji, alokacji ani wyszukiwania po stringu na
   każdą klatkę. Nazwy kompilują się/rejestrują do stabilnych identyfikatorów.

## 2. Docelowy model programowania

```text
Game code / Lua / Visual Graph
              │
              ▼
 Engine21kbLibrary (publiczne moduły, typy, kontrakty, uprawnienia)
              │  jednolity ScriptFunctionDesc / binding catalog
              ▼
 ScriptRuntimeHost + kolejki komend + scheduler lifecycle
              │
              ▼
 Scene/ECS | Input | Physics | Renderer | Audio | Assets | Network | UI
```

Przykładowy kod gry ma mieć tę samą intencję w każdym frontendzie:

```cpp
void PlayerController::FixedTick(Engine21kbLibrary::FixedContext& ctx) {
    const auto move = ctx.input.Action2D("Move");
    ctx.physics.MoveKinematic(ctx.self, move * speed * ctx.time.fixedDelta);
    if (ctx.input.Pressed("Fire")) {
        ctx.world.Spawn(prefabs.projectile, ctx.transform.WorldPose(ctx.self));
    }
}
```

Wizualny węzeł `Input.Action2D → Physics.MoveKinematic` i Lua
`Input.action2d("Move")` muszą wykonywać tę samą operację backendową, a nie
trzy niezależne implementacje.

## 3. Lifecycle i punkty synchronizacji

### Kontrakt świata

| Faza | Kiedy | Dozwolone działanie |
|---|---|---|
| `Created` | po utworzeniu instancji behaviour | inicjalizacja danych lokalnych; brak założenia aktywności |
| `Activated` | entity i komponent aktywne w świecie | subskrypcje, rejestracja timerów |
| `Ready` | po aktywacji wszystkich zależności sceny | odczyt innych obiektów, rozpoczęcie gry |
| `FixedTick` | 0..N razy przed symulacją renderowaną | deterministyczna logika/fizyka; stałe `fixedDelta` |
| `Tick` | raz na klatkę po kroku stałym | input, gameplay zależny od frame delta |
| `LateTick` | po `Tick` wszystkich behaviours | kamera, śledzenie, uporządkowane zależności |
| `BeforeRender` | przed zgłoszeniem renderowania | przygotowanie prezentacji, bez blokowania GPU |
| `AfterRender` | po zgłoszeniu renderowania | telemetry/debug, bez mutacji render-listy bieżącej klatki |
| `Deactivated` | przed utratą aktywności | anulowanie subskrypcji/timerów |
| `Destroyed` | jednoznacznie przed zwolnieniem | zwolnienie zasobów script-local; idempotentne cleanup |

Kolejność jest: `Created → Activated → Ready → (FixedTick*) → Tick →
LateTick → BeforeRender → AfterRender → Deactivated → Destroyed`. `Ready` nie
może wykonać się dwa razy dla jednej aktywacji bez jawnej semantyki restartu.

### Pionowy wycinek, który kończy lukę „edytor bez gry”

- [x] LIB-001 Opisać publiczny namespace, eksporty i politykę wersjonowania `Engine21kbLibrary`. Dowód: `sources/engine/include/engine/library/EngineLibrary.hpp` (namespace `kb::library`, doc kontraktu eksportów, `LibraryApiVersion{major,minor,patch}` + `kEngineLibraryApiVersion{0,1,0}` + `CanRun()`), `sources/engine/src/library/EngineLibrary.cpp` (`ToString`), test `sources/engine/tests/EngineLibraryTests.cpp` (suite `engine-library`, zarejestrowana w `TestSuites.hpp`/`SceneRuntimeTests.cpp`/CMakeLists). `cmake --build build --target kb_engine_tests --config Debug` — build bez warningów; `kb_engine_tests.exe engine-library` i pełny `kb_engine_tests.exe` (wszystkie suity) — exit 0.
- [x] LIB-002 Utworzyć pojedynczy moduł rejestracji biblioteki wywoływany przez `ScriptRuntimeHost` przy tworzeniu świata. Dowód: `sources/engine/include/engine/library/EngineLibraryModule.hpp` + `sources/engine/src/library/EngineLibraryModule.cpp` (`kb::library::EngineLibraryModule::Install(host)` zastąpiło 6 rozproszonych wywołań `Script*Api::Register` w `ScriptRuntimeHost::RegisterDefaultBackends()`, patrz `sources/engine/src/script/ScriptRuntimeHost.cpp`). Testy: `sources/engine/tests/EngineLibraryTests.cpp::RunModuleInstallCoversAllDomainsTest` (Input/Audio/World/Time/Physics/Transform osiągalne przez jeden entry point) i `RunModuleInstallReportsDuplicateDiagnosticsTest` (błąd = 6 diagnostyk, nie cichy fallback). `cmake --build build --target kb_engine_tests --config Debug` — bez warningów; `kb_engine_tests.exe engine-library` oraz pełny `kb_engine_tests.exe` (wszystkie suity) — exit 0.
- [x] LIB-003 Zastąpić ręczne, rozproszone rejestracje modułów jednym katalogiem z właścicielem, sygnaturą i capability. Dowód: po domknięciu LIB-016/017, `EngineLibraryModule::Install()` (`sources/engine/src/library/EngineLibraryModule.cpp`) nie ma już hardkodowanej sekwencji 6 wywołań — iteruje `Catalog()` (`std::vector<LibraryModuleDesc>`), pomijając moduły z `capability=false` zamiast je fałszywie rejestrować. `InstallModules(host, modules)` wystawione publicznie do wstrzykiwania własnego zestawu (testy, przyszłe LIB-020). Testy: `RunModuleCatalogTest` (kształt katalogu), `RunModuleInstallSkipsUnavailableCapabilityTest` (capability=false nigdy nie woła Register). Build+testy zielone.
- [x] LIB-004 Zmapować obecne `ScriptLifecycleEvent` na publiczne konteksty lifecycle bez duplikowania scheduler'a. Dowód: `sources/engine/include/engine/library/EngineLibraryLifecycle.hpp` (`kb::library::LifecycleEvent` = alias `kb::script::ScriptLifecycleEvent`, `LibraryLifecycleContextKind{Behaviour,Fixed,Frame,Render}`, `ClassifyLifecycleContext()`), `sources/engine/src/library/EngineLibraryLifecycle.cpp` (`ToString`). Test: `EngineLibraryTests.cpp::RunLifecycleContextClassificationTest` (10/10 zdarzeń zgodnych z tabelą z sekcji 3 planu + `static_assert` braku duplikacji enuma). Build+testy zielone (jak wyżej), exit 0.
- [x] LIB-005 Zdefiniować gwarantowaną kolejność `executionOrder`, następnie stabilny identyfikator entity i komponentu. Dowód: kontrakt sortowania (`BehaviourTickGroup` asc → `BehaviourComponent::executionOrder` asc → entity id asc) był zduplikowany w `ScriptRuntime.cpp::ComesBefore` i `ScriptRuntimeSceneSystem.cpp::CollectBehaviourRecords` — wydzielony do jednej `inline` funkcji `kb::scene::BehaviourExecutionOrderLess` (`sources/engine/include/engine/scene/BehaviourExecutionOrder.hpp`, header-only — hot path, sortowany co klatkę, brak LTO w buildzie), oba miejsca ją teraz wołają. Naprawiona realna luka determinizmu: `SyncBehaviourLifecycles`/`ShutdownTrackedBehaviours` w `ScriptRuntimeSceneSystem.cpp` iterowały po `unordered_map` przy wysyłce `Deactivated`/`Destroyed` dla usuwanych behaviours — teraz sortowane tym samym komparatorem przed dispatchem. `kb::library` wystawia kontrakt w `sources/engine/include/engine/library/EngineLibraryExecutionOrder.hpp` (`TickGroup`, `BehaviourExecutionOrderLess`, `EntityId` = `kb::ecs::Entity::IdType` stabilne w ramach sesji z generation-counter przez flecs, `ComponentId` = `kb::ecs::ComponentId` udokumentowane jako stabilne TYLKO w ramach procesu — luka do zamknięcia w LIB-009). Test: `EngineLibraryTests.cpp::RunExecutionOrderContractTest`. `cmake --build build --target kb_engine_tests --config Debug` — bez warningów; `kb_engine_tests.exe engine-library`, `kb_engine_tests.exe script` (pełna suita ScriptRuntimeTests po refaktorze) i pełny `kb_engine_tests.exe` — exit 0.
- [x] LIB-006 Ustalić punkt zastosowania komend ECS dla każdej fazy i przetestować go. Dowód: research wykazał, że dziś istnieją dwa niepołączone mechanizmy — `kb::ecs::World` (natychmiastowe mutacje, chronione tylko przez `StructuralChangeValidator` rzucający przy strukturalnej zmianie w trakcie otwartej iteracji Query) i `kb::ecs::CommandBuffer` (w pełni odłożony, jawny `Playback()`, ale niepodłączony do żadnej fazy lifecycle). Skrypty (`World.Spawn`/`World.Destroy` w `ScriptWorldApi.cpp`) używają ścieżki natychmiastowej i są bezpieczne wyłącznie dzięki wzorcowi "collect-then-dispatch" w `ScriptRuntime::DispatchSceneBehaviours`/`ScriptRuntimeSceneSystem::SyncBehaviourLifecycles` (zbiórka przez `ForEach` kończy się PRZED wywołaniem jakiegokolwiek skryptu). Kontrakt udokumentowany i wystawiony w `sources/engine/include/engine/library/EngineLibraryCommandApplication.hpp` (`CommandApplicationPoint{Immediate,DeferredUntilPlayback}`, `CommandApplicationPointFor(phase)` — dziś `Immediate` dla każdej fazy). Test: `EngineLibraryTests.cpp::RunCommandApplicationContractTest` — realny native behaviour spawnujący entity w Tick, dowodzi że (1) nowe entity jest natychmiast żywe/czytelne w tej samej fazie, (2) NIE dostaje własnych zdarzeń lifecycle w tej samej klatce (bo snapshot dispatchu tej fazy był już zebrany), (3) dostaje `Created`+`Tick` dopiero od następnej klatki. Build+testy zielone (jak wyżej), exit 0. Luka do przyszłych zadań: brak testu wzorca "CommandBuffer nagrywany w trakcie iteracji Query, playback po pętli" — to jawny zakres LIB-083, nadal nieukończone.
- [x] LIB-007 Udostępnić `BehaviourContext`, `FixedContext`, `FrameContext` i `RenderContext` bez referencji o niepewnym życiu. Dowód: `sources/engine/include/engine/library/EngineLibraryContext.hpp` — cztery typy finalne dziedziczące z `LibraryContextBase` (non-owning wskaźnik na call-scoped `ScriptExecutionContext`, żyjący wyłącznie na czas jednego callbacku; jedyna wartość bezpieczna do przechowania to `Self()` zwracające `EntityId` — zwykłą 64-bit wartość z LIB-005, nie referencję). `BehaviourContext` (Created/Activated/Ready/Deactivated/Destroyed) bez danych czasowych; `FixedContext.FixedDeltaSeconds()`/`FrameContext.DeltaSeconds()` dla FixedTick/Tick+LateTick; `RenderContext` bez mutatora render-listy (zgodnie z zakazem z sekcji 3 planu). Test: `EngineLibraryTests.cpp::RunLibraryContextTest` — realny native behaviour dispatchowany przez `ScriptRuntimeSceneSystem::ExecuteFrame`, weryfikuje poprawny typ kontekstu i wartości dla Created/FixedTick/Tick/BeforeRender. Build+testy zielone, exit 0.
- [x] LIB-008 Wprowadzić `EntityHandle` z generacją, kontrolą świata i diagnostyką nieaktualnego uchwytu. Dowód: `sources/engine/include/engine/library/EngineLibraryEntityHandle.hpp` + `sources/engine/src/library/EngineLibraryEntityHandle.cpp` — `EntityHandle{entity, sceneId}` (wartościowy, bez wskaźnika do Scene/World, wzorem `ScenePrefabHandle`), generacja opakowana przez istniejący `kb::ecs::Entity`/flecs (LIB-005, nie zduplikowana), kontrola świata przez porównanie `Scene::Id()`, diagnostyka `Validate()` rzucająca `std::invalid_argument` (nieprawidłowy uchwyt) / `std::out_of_range` (inna scena / nieaktualny uchwyt) wzorem istniejącego `World::ValidateEntityHandle`. `LibraryContextBase::SelfHandle()` (`EngineLibraryContext.hpp`) zwraca pełny handle, `Self()` (LIB-007) pozostał bez zmian. Test: `EngineLibraryTests.cpp::RunEntityHandleTest` (żywy/martwy/invalid/inna scena/po recyklingu indeksu) + rozszerzenie `RunLibraryContextTest` o `SelfHandle()`. Build+testy zielone, exit 0.
- [x] LIB-009 Wprowadzić `AssetRef<T>` oraz `SceneRef` z identyfikatorem runtime, a nie ścieżką systemową. Dowód: `sources/engine/include/engine/library/EngineLibraryAssetRef.hpp` — `AssetRef<T>` to alias `kb::assets::AssetHandle<T>` (nie nowa warstwa: istniejący `AssetId` to już deterministyczny hash logicznej ścieżki+typu, `AssetManager` już ma cache/refcounting), `SceneRef = AssetRef<kb::scene::SceneDocument>` korzystający z istniejącego `SceneAssetLoader` — jawnie odróżniony od `kb::scene::Scene::Id()` (runtime instance z LIB-008). Test: `EngineLibraryTests.cpp::RunAssetRefTest` — zapisuje realny plik `.21kbscene`, mountuje projekt, discover, ładuje przez `AssetManager::Load<SceneDocument>`, weryfikuje payload i że `AssetId` jest odtwarzalny jako `MakeAssetId(NormalizeAssetPath(virtualPath)+":"+type)` — niezależnie od physicalPath. Build+testy zielone, exit 0.
- [x] LIB-010 Ujednolicić wyniki błędów: `Result<T, ScriptError>` dla operacji zawodnych oraz wyjątek/diagnostykę adaptera dla Lua. Dowód: `kb::library::Result<T>` + `ScriptError` (`EngineLibraryResult.hpp`, `EngineLibraryError.hpp`/`.cpp`) — jedna, spójna forma wyniku zawodnej operacji `kb::library` (Ok/Fail, nie duplikuje osobnych struct-ów `ScriptFunctionCallResult`/`EngineLibraryModuleResult`, tylko nazywa wzorzec). Realna, krytyczna naprawa bezpieczeństwa: research wykazał, że wyjątek C++ rzucony przez zarejestrowany callback (np. przyszłe `EntityHandle::Validate()`) przechodzący przez `lua_pcall` (PUC-Lua skompilowane jako C, longjmp-based) to dziś UB/crash procesu, nie diagnostyka. Naprawione w DWÓCH miejscach: `ScriptFunctionRegistry::Call` (`sources/engine/src/script/ScriptFunctionRegistry.cpp`, jedyny punkt przez który przechodzą Native, Lua `CallFunction` i przyszły Visual Graph) oraz `NativeScriptBackend::ExecuteLifecycle`/`ExecuteEvent` (`sources/engine/src/script/NativeScriptBackend.cpp`, bezpośredni dispatch behaviourów) — wyjątek konwertowany na `ScriptFunctionCallResult.errors`/`ScriptDiagnostic`, pętla dispatchu pozostałych behaviourów w tej samej fazie nie jest przerywana. Testy: `ScriptRuntimeTests.cpp::RunScriptFunctionRegistryExceptionSafetyTest` (rejestr zostaje używalny po wyjątku), `RunNativeScriptBackendExceptionSafetyTest` (behaviour po throwing-behaviourze w tej samej fazie nadal dostaje Tick — dowód że pętla przeżywa), `EngineLibraryTests.cpp::RunResultTest`. `cmake --build build --target kb_engine_tests --config Debug` — bez warningów; `kb_engine_tests.exe` (`engine-library`, `script`, pełny run) — exit 0 (co samo w sobie dowodzi, że proces NIE ulega crashowi/terminate przy testowanym wyjątku — kluczowy dowód naprawy).
- [x] LIB-011 Dodać test kolejności lifecycle dla Native, Lua i Visual Graph. Dowód: `sources/engine/tests/ScriptRuntimeTests.cpp` — `RunNativeFullLifecycleOrderTest`, `RunPucLuaFullLifecycleOrderTest`, `RunVisualGraphFullLifecycleOrderTest` (każdy weryfikuje pełną sekwencję 10 zdarzeń Created→Destroyed w jednym ciągłym porównaniu, przez dwa kolejne `ExecuteFrame` — poprzednie testy pokrywały tylko fragmenty) oraz `RunCrossBackendLifecycleOrderParityTest` (Native+Lua+VisualGraph w jednej scenie, utworzone w kolejności odwrotnej do oczekiwanej, dowodzi że `BehaviourExecutionOrderLess` obowiązuje niezależnie od backendu). Build+testy zielone (`kb_engine_tests.exe`, suite `script`, pełny run) — exit 0.
- [x] LIB-012 Dodać test powtórnej aktywacji, niszczenia w tej samej klatce i anulowania pending commands. Dowód: (1) powtórna aktywacja bez rekreacji już pokryta przez istniejący `RunScriptRuntimeSceneSystemDynamicLifecycleTest`; (2) niszczenie wielu behaviourów w tej samej klatce pokryte przez `RunMultipleBehavioursRemovedSameFrameOrderTest` (dodany podczas audytu LIB-005); (3) anulowanie pending commands — NOWY test `sources/engine/tests/ScriptRuntimeTests.cpp::RunPendingCommandCancelledByDestroyTest`: entity wysyła targetowany event do innej entity i natychmiast ją niszczy w tej samej klatce — dowodzi, że `ScriptRuntime::DrainEvents`' `pending` queue bezpiecznie porzuca dostawę do zniszczonego celu (świeża kolekcja behaviourów przy każdym dispatchu), zamiast dostarczać do nieaktualnej/wiszącej encji lub crashować. Build+testy zielone (`kb_engine_tests.exe`, suite `script`, pełny run) — exit 0.
- [x] LIB-013 Dodać minimalny template `PlayerController` działający w Play Mode bez zależności edytora. Dowód: research potwierdził, że jedyną istniejącą, faktycznie edytoro-niezależną powierzchnią "Play Mode" jest `kb-cli run` (headless, zero bgfx/okna/edytora, prawdziwa pętla klatek przez `ScriptRuntimeHost`/`ScriptRuntimeSceneSystem`) — `EditorPlayModeSceneSession`/`EditorPlayModeState` żyją w `sources/editor` i są jawnie tym, czego zadanie zabrania. `sources/tools/tests/KbCliTests.cpp::RunPlayerControllerTemplateTests` — realny `PlayerController.lua` (Input.Vector2 → Transform.Translate, wzorzec z `ScriptAgentProjectFiles`' AGENTS.md), zapisana scena z node'em `Player`, uruchomione w procesie przez prawdziwe komendy CLI: `validate` → `scene-attach` → `run --frames 5`. Test weryfikuje: exit 0, `Ready` odpalone raz, `Tick` odpalone dokładnie 5× (raz na klatkę), event `PlayerMoved` wyemitowany, `0 diagnostics`. `Input.Vector2` bezpiecznie zwraca zera bez skonfigurowanego mapowania (potwierdzone w `InputSubsystem::GetActionValue`), więc headless run nie crashuje mimo braku urządzenia wejścia. Build+test zielone (`kb_cli_tests.exe`) — exit 0. Luka odnotowana (nie blokuje LIB-013): `Physics.*` ma dziś tylko `Raycast`, brak `MoveKinematic`/velocity — ruch idzie przez `Transform.Translate`, zgodnie z aktualnym stanem API; brak windowed/renderer standalone playera (poza zakresem "bez edytora, headless").
- [ ] LIB-014 Dodać minimalny template `Projectile` z prefabem, ruchem, kolizją i zniszczeniem. ZABLOKOWANE: "kolizja" wymaga script-facing collision event API (`OnCollisionEnter/Stay/Exit`), które nie istnieje przed LIB-127 (sekcja 10, dziś `ScriptPhysicsApi` ma tylko `Physics.Raycast`, potwierdzone grepem — zero `OnCollision*`/`OnTrigger*` w `sources/engine/src/script`). Patrz `others/_temp.md` POWRÓT.
- [ ] LIB-015 Dodać sample scene i test end-to-end: input → ruch → spawn → kolizja → audio/log. ZABLOKOWANE: zależy od LIB-014 (kolizja), ten sam brakujący prerequisite.

## 4. Architektura i kontrakty bazowe

### 4.1 Katalog bindingów i metadata

- [x] LIB-016 Zdefiniować `LibraryModuleDesc`: moduł, wersja, owner runtime, capability i zależności. Dowód: `sources/engine/include/engine/library/EngineLibraryModuleDesc.hpp` — `name`, `LibraryModuleVersion`, `ownerRuntime` (realna etykieta zweryfikowanego właściciela stanu, np. `kb::scene::SceneEntities` dla World — sprawdzone w kodzie, nie zgadywane), `capability`, `dependencies`. Realnie skonsumowane przez `EngineLibraryModule::Catalog()`/`Install()` (LIB-003). Test: `RunModuleCatalogTest`.
- [x] LIB-017 Zdefiniować `LibraryFunctionDesc`: kanoniczna nazwa, wejścia, wyjścia, determinism, thread affinity i błąd. Dowód: `sources/engine/include/engine/library/EngineLibraryFunctionDesc.hpp` — `canonicalName` (odnosi się do `ScriptFunctionSignature::name`, nie duplikuje wejść/wyjść, które już tam żyją), `LibraryThreadAffinity`, `LibraryDeterminism`, `canFail`. Pole `LibraryModuleDesc::functions` (przyrostowo audytowane — pusta lista = "jeszcze nie zaudytowane", nie "nie istnieje"; pełny audyt wszystkich funkcji to LIB-231/LIB-237, nie to zadanie). Pilotażowo wypełnione i realnie zweryfikowane dla `World.Exists` (czysta funkcja nad `SceneEntities::IsAlive`, deterministyczna, main-thread, nie generuje błędu). Test anti-drift: `RunFunctionDescCatalogResolvesTest` — każdy audytowany `canonicalName` musi rozwiązać się w rzeczywiście zarejestrowaną funkcję przez `ScriptApiCatalog`.
- [x] LIB-018 Zdefiniować `LibraryTypeDesc`: serializacja, porównanie, domyślna wartość, Visual Graph pin i Lua marshalling. Dowód: `sources/engine/include/engine/library/EngineLibraryTypeDesc.hpp` — cienki agregator nad JUŻ istniejącymi, realnymi implementacjami (`kb::script::ToString` = serializacja, `kb::script::ToVisualGraphValueType` = Visual Graph pin, `luaTypeName` = dokumentacja marshallingu z `PucLuaValueBridge` — prywatny nagłówek, więc nazwa jest deklaratywna, nie wywołuje kodu, zgodnie z granicą public/private), `defaultValue`. Realna naprawa braku: `kb::script::ScriptValue` NIE MIAŁO `operator==` — dodane (`ScriptValue.hpp`), bo "porównanie" bez działającej implementacji byłoby pustą deklaracją. `DescribeType()` zwraca deskryptor dla wszystkich 7 `ScriptValueType`. Test: `EngineLibraryTests.cpp::RunTypeDescTest` — krzyżowa weryfikacja z `ToString`/`ToVisualGraphValueType`, oraz `ScriptValue::operator==` (w tym rozróżnienie Entity vs Component mimo tego samego surowego id). Build+testy zielone (`kb_engine_tests.exe` + `kb_cli_tests.exe`, oba dotknięte zmianą w `ScriptValue.hpp`) — exit 0.
- [x] LIB-019 Zdefiniować `LibraryPropertyDesc` dla bezpiecznych pól komponentów i assetów. Dowód: `sources/engine/include/engine/library/EngineLibraryPropertyDesc.hpp` — `LibraryPropertyDesc = kb::script::ScriptApiCatalogProperty` (alias, nie duplikat — ten typ już opisuje name/type/writable dla właściwości komponentów rejestrowanych w `ScriptSceneComponentApi.cpp`). Właściwości assetów nie mają dziś żadnego script-facing akcesora (LIB-155+ Assets), więc alias czeka gotowy na ich przyszłe użycie zamiast wprowadzać drugi kształt. Test: `EngineLibraryTests.cpp::RunPropertyDescTest` — `static_assert` tożsamości typu + realne właściwości Transform z żywego `ScriptApiCatalog` używane wprost jako `LibraryPropertyDesc`. Build+testy zielone.
- [x] LIB-020 Dodać walidację kolizji nazw, zmian sygnatur i cykli zależności modułów podczas startu. Dowód: `sources/engine/include/engine/library/EngineLibraryModuleValidation.hpp` + `.cpp` — `ValidateModuleCatalog()` sprawdza: puste/zduplikowane nazwy modułów, zależności wskazujące na nieistniejący moduł (literówki), cykle zależności (DFS 3-kolorowy), oraz funkcję (`LibraryFunctionDesc::canonicalName`) zaudytowaną jednocześnie przez dwa różne moduły ("zmiana sygnatury"/kolizja własności). Wpięte w `EngineLibraryModule::InstallModules()` PRZED jakąkolwiek rejestracją — nieprawidłowy katalog nie rejestruje NIC (fail-fast), zamiast częściowej rejestracji niespójnego zestawu. Testy: `RunModuleCatalogValidatesTest` (prawdziwy katalog produkcyjny przechodzi), `RunModuleValidationDuplicateNameTest`, `RunModuleValidationUnknownDependencyTest`, `RunModuleValidationCycleTest` (w tym pozytywny przypadek acyklicznego łańcucha), `RunModuleValidationDuplicateFunctionTest`, `RunModuleInstallFailsFastOnInvalidCatalogTest` (dowodzi że `Register()` NIGDY nie jest wołane dla złego katalogu). Build+testy zielone.
- [x] LIB-021 Zablokować rejestrację po starcie świata lub zdefiniować bezpieczną transakcję hot-reload. Decyzja: prostszy, bezpieczniejszy wariant "zablokować" — pełna transakcja hot-reload to osobne, dedykowane zadanie (LIB-228, M2). Dowód: `kb::script::ScriptFunctionRegistry` (`ScriptFunctionRegistry.hpp`/`.cpp`) — `Lock()`/`IsLocked()`, `Register()` odrzuca (zwraca `false`) po zablokowaniu. `ScriptRuntime::ExecuteLifecycle`/`ExecuteLifecycleForBehaviour`/`DispatchEvent` (jedyne 3 fundamentalne punkty dispatchu, przez które przechodzą wszystkie warianty `...AndDispatchEvents`) wołają `functions_.Lock()` jako pierwszą instrukcję — rejestracja jest legalna podczas setupu (konstrukcja `ScriptRuntimeHost`, `configureHost`), zablokowana od pierwszego realnego dispatchu fazy/zdarzenia świata. Test: `ScriptRuntimeTests.cpp::RunScriptFunctionRegistryLocksAfterFirstDispatchTest` — rejestracja przed dispatchem działa, po pierwszym dispatchu jest odrzucana (i nie dodaje funkcji), funkcje zarejestrowane wcześniej pozostają w pełni wywoływalne. Cała suita testów (obejmująca dziesiątki testów wywołujących dispatch) przeszła bez zmian — potwierdza, że istniejący kod już naturalnie rejestruje przed dispatchem. Build+testy zielone (`kb_engine_tests.exe` + `kb_cli_tests.exe`).
- [x] LIB-022 Generować manifest API dla edytora, dokumentacji, autocompletion i testów kompatybilności. Dowód (infrastruktura już istniała z wcześniejszej sesji — zweryfikowana w tej turze jako realnie spełniająca kryterium): `kb::script::ScriptApiExport::ToMarkdown/ToJson/ToLuaStubs` (`sources/engine/src/script/ScriptApiExport.cpp`) konsumuje `ScriptApiCatalog::Build(host)` i generuje: Markdown (dokumentacja), JSON (dane strukturalne dla edytora/przyszłych testów kompatybilności LIB-024), Lua stuby `---@meta` (autocompletion, zgodne z Lua Language Server przez `.luarc.json`). Dostępne przez `kb-cli api --print markdown|json|lua` i `kb-cli init-agent` (zapisuje `.kb/api/{kb.lua,script_api.md,script_api.json}` + `AGENTS.md`). Testy: `ScriptApiCatalogTests.cpp::RunCatalogExportTest` (treść wszystkich 3 formatów, w tym walidacja że Lua stuby faktycznie parsują się jako poprawny Lua), `KbCliTests.cpp::RunApiCommandTests`. Nie wymagało nowego kodu w tej turze — pozostałe braki (hash ABI/API do artefaktu builda, porównanie w CI) to jawnie osobne LIB-023/LIB-024.
- [x] LIB-023 Zapisać manifest API do artefaktu builda z hash'em ABI/API. Dowód: `sources/engine/include/engine/library/EngineLibraryManifest.hpp`/`.cpp` — `ComputeApiManifestHash()` (deterministyczny FNV-1a 64-bit, 16 cyfr hex), `BuildApiManifest(catalog)` (paruje `kEngineLibraryApiVersion` z hashem kanonicznego JSON z `ScriptApiExport::ToJson`), `ToJson(manifest)`. Wpięte w `kb-cli api`/`kb-cli init-agent` (`sources/tools/src/CliApiCommands.cpp`) — zapisują `.kb/api/manifest.json` jako część artefaktu builda obok istniejących `kb.lua`/`script_api.md`/`script_api.json`. Architektura: `kb::script` (ScriptAgentProjectFiles) NIE zależy od `kb::library` (zachowana granica wrap-only) — hash dopisuje narzędzie CLI, jedyna warstwa uprawniona łączyć obie strony. Testy: `EngineLibraryTests.cpp::RunApiManifestTest` (determinizm, wrażliwość na zmianę treści, spójność z `ToString(version)`), `KbCliTests.cpp::RunApiCommandTests` (manifest.json zapisany, identyczny hash przy dwóch kolejnych `init-agent` na niezmienionym projekcie). Build+testy zielone (`kb_engine_tests.exe` + `kb_cli_tests.exe`).
- [ ] LIB-024 Porównywać manifest poprzedniej wersji w CI i wykrywać breaking changes. CZĘŚCIOWO: silnik porównania gotowy i przetestowany — `sources/engine/include/engine/library/EngineLibraryManifestComparison.hpp`/`.cpp`: `CompareApiCatalogs(baseline, current)` klasyfikuje każdą różnicę (usunięta/zmieniona funkcja lub property = Breaking, tylko dodana funkcja/component/property = Additive), `ApiCompatibilityReport::HasBreakingChanges()`. Testy: `EngineLibraryTests.cpp::RunApiCompatibilityComparisonTest` (identyczny katalog = brak zmian; usunięta funkcja/zmieniony typ pinu/property stające się read-only = Breaking; nowa funkcja = Additive, nie Breaking). ZABLOKOWANE dla realnego wpięcia "w CI" (`.github/workflows/ci.yml`): wymaga decyzji o strategii przechowywania baseline'u między uruchomieniami CI (np. commitowany plik referencyjny w repo + osobny workflow do jego aktualizacji, czy pobieranie artefaktu z poprzedniego builda brancha bazowego) — to decyzja dot. współdzielonej infrastruktury, nie kodu; nie modyfikowałem `.github/workflows/ci.yml` bez tej decyzji. Patrz `others/_temp.md` POWRÓT.
- [x] LIB-025 Dodać atrybut `Deprecated` z migratorem Visual Graph oraz komunikatem dla Lua/C++. Dowód: `sources/engine/include/engine/library/EngineLibraryDeprecation.hpp`/`.cpp` — `LibraryDeprecation{message, replacementCanonicalName, sinceVersion}`, `FormatDeprecationWarning()` (jeden wspólny komunikat, więc Lua i C++ widzą identyczny tekst — nie ma osobnych ścieżek do rozjazdu), `MigrateVisualGraphCallNativeNodes()` — mechaniczne przepisanie klucza bindingu `"Function."+stara` → `"Function."+nowa` dla węzłów `CallNative` w grafie (klucz zgodny z auto-generowanym bindingiem z `ScriptFunctionVisualGraphBindings`), świadomie NIE migruje niczego, gdy deprecation nie deklaruje zamiennika (usunięta funkcjonalność ma zostać zgłoszona człowiekowi, nie po cichu przepisana na coś innego). Test: `EngineLibraryTests.cpp::RunDeprecationTest`. Build+testy zielone.
- [x] LIB-026 Zdefiniować stabilne identyfikatory funkcji niezależne od kolejności rejestracji. Dowód: `sources/engine/include/engine/library/EngineLibraryFunctionId.hpp`/`.cpp` — `ComputeLibraryFunctionId(canonicalName)` (FNV-1a 64-bit hash nazwy, ten sam wzorzec co `ComputeApiManifestHash`/`kb::assets::MakeAssetId`), deterministyczny i niezależny od pozycji w `ScriptFunctionRegistry::Functions()` czy kolejności rejestracji modułów — kontrast z `kb::ecs::ComponentId` (LIB-005), które JEST zależne od kolejności rejestracji w danym procesie. Nie zastępuje wyszukiwania po nazwie (`ScriptFunctionRegistry::Call` nadal rozwiązuje po stringu) — to rezerwa pod przyszłe zwarte klucze (Visual Graph binary IR, LIB-235). Test: `EngineLibraryTests.cpp::RunFunctionIdTest` (determinizm, różne nazwy → różne id, identyczne id z dwóch niezależnie skonstruowanych hostów). Build+testy zielone.
- [x] LIB-027 Zdefiniować politykę feature flags: capability ujawniona tylko, gdy backend istnieje. Dowód: zaimplementowane jako część LIB-016/LIB-003 — `LibraryModuleDesc::capability` (`EngineLibraryModuleDesc.hpp`) + `EngineLibraryModule::InstallModules()` pomija moduł z `capability=false` (nigdy nie woła `Register()`, nie zgłasza diagnostyki — to nie błąd, to świadomie wyłączona funkcja). Test: `EngineLibraryTests.cpp::RunModuleInstallSkipsUnavailableCapabilityTest` (dowodzi, że `Register()` faktycznie nigdy nie jest wołane dla capability=false) oraz `RunModuleCatalogTest` (wszystkie 6 modułów produkcyjnych mają `capability=true`, bo ich backend jest realnie skompilowany w tym buildzie).
- [ ] LIB-028 Dodać report startowy: dostępne moduły, wyłączone capability, wersje i przyczyna.
- [ ] LIB-029 Dodać test zgodności katalogu: każda funkcja ma opis i binding we wszystkich wspieranych frontendach. CZĘŚCIOWO: część "binding we wszystkich frontendach" zrobiona i przetestowana — `EngineLibraryTests.cpp::RunCatalogFunctionsHaveVisualGraphBindingsTest` iteruje WSZYSTKIE funkcje z żywego `ScriptApiCatalog` i weryfikuje realny binding Visual Graph (native+runtime) oraz wpis w rejestrze Native/Lua dla KAŻDEJ z nich (Native+Lua CallFunction są strukturalnie gwarantowane przez jedną ścieżkę `ScriptRuntimeHost::RegisterFunction`, więc realną, niezależną częścią do zweryfikowania było wyłącznie wiązanie Visual Graph — i to jest teraz pokryte dla całego katalogu, nie tylko pojedynczych funkcji jak wcześniej). ZABLOKOWANE: część "każda funkcja ma OPIS" — dziś żaden typ (`ScriptApiCatalogFunction`, `LibraryFunctionDesc`) nie ma pola opisu tekstowego; dodanie go retroaktywnie do dziesiątek istniejących funkcji to osobne zadanie dokumentacyjne (naturalnie powiązane z LIB-248 "generator Markdown/reference z manifestu"), nie chcę fabrykować pustych/fałszywych opisów żeby "domknąć" checkbox. Patrz `others/_temp.md` POWRÓT.
- [x] LIB-030 Dodać test, że nieeksponowana funkcja nie jest możliwa do wywołania po nazwie. Dowód: `sources/engine/tests/ScriptRuntimeTests.cpp::RunUnexposedFunctionCannotBeCalledTest` — nazwa nigdy niezarejestrowana wywołana przez `ScriptFunctionRegistry::Call` (jedyny punkt przez który przechodzą Native, Lua `CallFunction` i Visual Graph `CallNative`) zwraca jawny błąd "is not registered", `executed=false`, nigdy ciche powodzenie. Build+testy zielone.

### 4.2 Własność, bezpieczeństwo i błędy

- [x] LIB-031 Zdefiniować własność obiektów tworzących zasoby (`Owned`, `Borrowed`, `Shared`, `Weak`). Dowód: `sources/engine/include/engine/library/EngineLibraryOwnership.hpp`/`.cpp` — `LibraryOwnership{Owned,Borrowed,Shared,Weak}` + `LibraryOwnershipTraits<T>` (specjalizacje kompilacyjne, nie proza — nieklasyfikowany typ to błąd kompilacji, nie cichy domyślny wybór). `EntityHandle` (LIB-008) sklasyfikowany jako `Borrowed` (Scene jest właścicielem), `AssetRef<T>`/`SceneRef` (LIB-009) jako `Shared` (ref-counted `shared_ptr` w `AssetHandle<T>`). Test: `EngineLibraryTests.cpp::RunOwnershipTest` (`static_assert` dla obu typów + `ToString`). Build+testy zielone.
- [x] LIB-032 Zabronić przekazywania wskaźników/referencji C++ poza granicę klatki skryptowej. Dowód: `kb::script::ScriptValue` — jedyny kanał, którym dane przechodzą przez granicę Lua/Visual Graph (argumenty funkcji, wyjścia, payloady zdarzeń, zmienne exposed) — dostał `static_assert` wprost w `ScriptValue.hpp`, że żadna alternatywa `Storage` nie jest wskaźnikiem/referencją; edycja typu dodająca wskaźnik nie skompiluje się. Test: `EngineLibraryTests.cpp::RunNoPointersCrossScriptBoundaryTest` — dodatkowo blokuje dokładny kształt/kolejność `Storage` (6 alternatyw) jako strażnika regresji. `LibraryContextBase::Raw()` (LIB-007) pozostaje jedynym udokumentowanym wyjątkiem (C++-only escape hatch, niebezpieczny do zachowania poza wywołaniem — nigdy nie przechodzi do Lua/Visual Graph). Build+testy zielone (`kb_engine_tests.exe` + `kb_cli_tests.exe`, oba dotknięte zmianą w `ScriptValue.hpp`).
- [x] LIB-033 Dodać kontrolę przynależności `EntityHandle` do świata i sceny. Dowód: zaimplementowane jako część LIB-008 — `EntityHandle::IsAlive(scene)`/`Validate(scene, op)` porównują `sceneId_` (przechwycone przy tworzeniu handle) z `scene.Id()` PRZED sprawdzeniem żywotności encji; niezgodność = `false`/`std::out_of_range` ("belongs to a different world"). Test: `EngineLibraryTests.cpp::RunEntityHandleTest` (`wrongWorldHandle` — handle z jednej sceny sprawdzany względem innej).
- [x] LIB-034 Dodać ochronę przed użyciem entity po `Destroy` i po zmianie generacji. Dowód: zaimplementowane jako część LIB-008 — `EntityHandle::IsAlive(scene)` deleguje do `scene.Entities().IsAlive()` (generation-checked przez flecs, LIB-005), więc handle do zniszczonej encji — nawet jeśli jej indeks zostanie zrecyklowany przez nową encję — zawsze zwraca `false`. Test: `EngineLibraryTests.cpp::RunEntityHandleTest` (zniszczenie encji → `IsAlive`==false + `Validate` rzuca `std::out_of_range`; utworzenie nowej encji po zniszczeniu → stary handle pozostaje `false`, nowa encja żyje).
- [x] LIB-035 Zdefiniować kody błędów: invalid handle, inactive world, unavailable capability, permission, invalid argument, timeout. Dowód: `sources/engine/include/engine/library/EngineLibraryError.hpp` — `LibraryErrorCode{InvalidHandle,InactiveWorld,UnavailableCapability,Permission,InvalidArgument,Timeout}` (dokładnie 6 kategorii z planu, z komentarzem definiującym granice każdej), `ScriptError` (LIB-010) dostało pole `code`. Realne użycie: `EntityHandle::CheckError(scene, op)` (`EngineLibraryEntityHandle.hpp`/`.cpp`) — nierzucający odpowiednik `Validate()`, zwraca `std::optional<ScriptError>` z `code=InvalidHandle` dla wszystkich 3 trybów awarii uchwytu (invalid/wrong-world/stale); wspólna logika wydzielona do `FirstFailureReason()`, żeby `Validate()` i `CheckError()` nie duplikowały trzech sprawdzeń. Test: `EngineLibraryTests.cpp::RunErrorCodeTest`. Build+testy zielone.
- [x] LIB-036 Dodać strukturę błędu zawierającą moduł, funkcję, entity, asset i fazę lifecycle. Decyzja: rozszerzyć istniejący `kb::script::ScriptDiagnostic` (już miał entity/asset/backend, faktycznie przepływa przez cały runtime — `ScriptBackendExecutionResult`/`ScriptRuntimeExecutionResult`), nie tworzyć równoległego typu w `kb::library`. Dowód: `sources/engine/include/engine/script/ScriptBackend.hpp` — dodane pola `module`, `function`, `lifecyclePhase` (`std::optional<ScriptLifecycleEvent>`, bo nie każda diagnostyka ma fazę). Wypełnianie przyrostowe (jak LIB-017/029): `NativeScriptBackend::InvokeNativeCallback` (naprawa z LIB-010) teraz realnie wypełnia `lifecyclePhase` z `context.Lifecycle()`. Test: rozszerzony `ScriptRuntimeTests.cpp::RunNativeScriptBackendExceptionSafetyTest` (weryfikuje `lifecyclePhase == Tick` na prawdziwej diagnostyce z wyjątku). Build+testy zielone (`kb_engine_tests.exe` + `kb_cli_tests.exe`).
- [x] LIB-037 Ujednolicić limity wejścia: długość stringów, kolekcji, payloadów zdarzeń i rekurencji graphów. Dowód: `sources/engine/include/engine/library/EngineLibraryInputLimits.hpp` — `LibraryInputLimits{maxStringLength=65536, maxCollectionSize=4096, maxEventPayloadArguments=32, maxGraphRecursionDepth=8}`. Realnie egzekwowane DWA z czterech: (1) `maxStringLength` — nowy limit w `kb::script::ScriptFunctionRegistry::ValidateInputs` (`ScriptFunctionRegistry.cpp`, jedyny punkt walidacji dla Native+Lua+Visual Graph), zweryfikowany testem z prawdziwym stringiem 65536B (OK) i 65537B (odrzucone); (2) `maxGraphRecursionDepth` — JUŻ istniał i był egzekwowany jako `ScriptRuntimeDispatchOptions::maxEventDepth` (domyślnie 8), teraz udokumentowany jako spełniający to kryterium, wartości zweryfikowane jako identyczne testem. `maxCollectionSize`/`maxEventPayloadArguments` to świadomie zarezerwowane wartości polityki — brak dziś typu kolekcji (`Array<T>` to LIB-058) i mechanizmu walidacji payloadu zdarzeń bez przebudowy `Emit`/`EmitTo` (dziś `void`, brak kanału błędu — dodanie cichego odrzucania złamałoby zakaz cichych fallbacków) do egzekwowania; nie fabrykowano fałszywej walidacji. Test: `EngineLibraryTests.cpp::RunInputLimitsTest`. Build+testy zielone.
- [x] LIB-038 Dodać ochronę przed reentrancy w zdarzeniach i callbackach. Dowód: `kb::script::ScriptFunctionRegistry` — jedyny choke point przez który przechodzą wszystkie wywołania Native/Lua/Visual Graph — dostał licznik głębokości `mutable std::size_t callDepth_` (`ScriptFunctionRegistry.hpp`) i strażnika `kMaxCallDepth=64` (`ScriptFunctionRegistry.cpp`, anonimowa przestrzeń nazw obok `kMaxScriptStringArgumentLength` z LIB-037). `Call()` odrzuca wywołanie z jasnym diagnostykiem ("exceeded the maximum call depth... reentrant or mutually recursive call chain") ZANIM wejdzie w callback, gdy `callDepth_ >= kMaxCallDepth`; poza tym RAII `CallDepthGuard` (lokalna klasa w anonimowej przestrzeni nazw) inkrementuje/dekrementuje licznik dokładnie wokół `iter->callback(...)`, więc głębokość jest poprawnie przywracana na każdej ścieżce wyjścia (normalny powrót, oba catch z LIB-010). Chroni to zarówno bezpośrednią rekurencję (funkcja wywołuje samą siebie), jak i łańcuch wzajemnie rekurencyjnych funkcji, bo licznik jest globalny dla rejestru, nie per-funkcja. Test: `ScriptRuntimeTests.cpp::RunScriptFunctionRegistryReentrancyGuardTest` — rejestruje `Tests.Recurse`, którego callback woła `host.Functions().Call("Tests.Recurse", ...)` na sobie; weryfikuje że łańcuch zatrzymuje się na limicie głębokości z jasnym komunikatem zamiast przepełnić stos, że kolejne wywołanie tego samego łańcucha deterministycznie zawodzi tak samo, i że sam rejestr pozostaje nienaruszony (sygnatura nadal znaleziona) po odrzuceniu. Build+testy zielone: `kb_engine_tests.exe` (cały `RunAllSuites`, exit 0, brak wyjątków) oraz `kb_cli_tests.exe` (exit 0) po pełnym `cmake --build . --target kb_engine_tests/kb_cli_tests --config Debug`.
- [ ] LIB-039 Dodać jawne anulowanie `Subscription`, `TimerHandle`, `AsyncHandle` i `TaskHandle`.
- [ ] LIB-040 Przetestować usuwanie entity z callbacku, callback po anulowaniu i błąd po unloadzie sceny.

## 5. Typy podstawowe, matematyka i dane

Matematyka musi odwzorowywać typy silnika, nie tworzyć równoległego zestawu
wektorów. Typy wartościowe są wspólne dla native, Lua serializer'a i pinów
Visual Graph.

- [ ] LIB-041 Udostępnić `Bool`, `Int32`, `Int64`, `UInt32`, `Float`, `Double`, `String`, `Name`, `Guid` i `Hash`.
- [ ] LIB-042 Udostępnić `Vec2`, `Vec3`, `Vec4`, `IVec2`, `Color`, `Rect`, `Bounds`, `Ray`, `Plane` i `Pose`.
- [ ] LIB-043 Udostępnić `Quat`, `Mat3`, `Mat4` oraz jawną konwencję handedness i kolejność mnożenia.
- [ ] LIB-044 Udostępnić `Radians`, `Degrees`, bezpieczne konwersje i nie mieszać jednostek w API.
- [ ] LIB-045 Udostępnić `Math.Clamp`, `Lerp`, `InverseLerp`, `Remap`, `SmoothStep`, `MoveTowards`, `Damp`.
- [ ] LIB-046 Udostępnić `Min`, `Max`, `Abs`, `Sign`, `Floor`, `Ceil`, `Round`, `Frac`, `Mod`, `Sqrt`, `Pow`, `Exp`, `Log`.
- [ ] LIB-047 Udostępnić funkcje trygonometryczne i odwrotne ze zdefiniowaną domeną błędu.
- [ ] LIB-048 Udostępnić `Dot`, `Cross`, `Length`, `Normalize`, `Distance`, `Project`, `Reflect`, `Refract`.
- [ ] LIB-049 Udostępnić `Angle`, `SignedAngle`, `Slerp`, `LookRotation`, `FromToRotation`, `RotateTowards`.
- [ ] LIB-050 Udostępnić `Perlin`/gradient noise i deterministic seeded random bez globalnego ukrytego generatora.
- [ ] LIB-051 Udostępnić `RandomStream` z seedem, snapshotem stanu i funkcjami integer/float/range/shuffle.
- [ ] LIB-052 Dodać `Easing` jako wartościowy enum/krzywą, bez allocacji callbacków w hot path.
- [ ] LIB-053 Udostępnić `Curve` i `Gradient` z deterministyczną ewaluacją oraz serializacją assetową.
- [ ] LIB-054 Zdefiniować wartości `NaN`, infinity i zero-length dla wszystkich operacji matematycznych.
- [ ] LIB-055 Dodać property/fuzz tests dla wektorów, quaternionów, interpolacji i serializacji.
- [ ] LIB-056 Dodać parity tests wyników C++, Lua i Visual Graph z tolerancją float.

### Kolekcje i tekst

- [ ] LIB-057 Udostępnić niemutowalny `ArrayView<T>` dla danych zwracanych przez runtime.
- [ ] LIB-058 Udostępnić kontrolowany `Array<T>`, `Map<K,V>`, `Set<T>`, `Queue<T>` i `Stack<T>` dla danych skryptowych.
- [ ] LIB-059 Ustalić koszt alokacji kolekcji i warianty `NonAlloc`/caller-provided buffer dla hot path.
- [ ] LIB-060 Ustalić deterministyczną iterację map/setów albo zakazać polegania na jej kolejności.
- [ ] LIB-061 Udostępnić `Option<T>`/`Result<T,E>` w językach, które je wspierają, i idiomatyczne adaptery Lua/graph.
- [ ] LIB-062 Udostępnić formatowanie tekstu bez niekontrolowanej alokacji w pętli klatki.
- [ ] LIB-063 Udostępnić parsing liczb, GUID, kolorów i dat z jednoznaczną lokalizacją/invariant culture.
- [ ] LIB-064 Dodać UTF-8 jako jedyny format publicznych stringów oraz walidację granicy platformy.

## 6. Świat, entity, komponenty i transformacje

### World i Scene

- [ ] LIB-065 Udostępnić `World.Current`, `World.IsPlaying`, `World.FrameIndex` i `World.FixedStepIndex`.
- [ ] LIB-066 Udostępnić `World.Spawn(prefab, pose, parent?)` zwracające uchwyt dopiero po zdefiniowanym flushu.
- [ ] LIB-067 Udostępnić `World.Destroy(entity)` z idempotencją i flagą deferred/immediate zgodną z ECS.
- [ ] LIB-068 Udostępnić `World.Exists`, `World.IsActive`, `World.SetActive` i `World.Name`.
- [ ] LIB-069 Udostępnić `World.FindByName`, `FindByTag`, `FindAllByTag` z udokumentowanym kosztem.
- [ ] LIB-070 Udostępnić `World.InstantiatePrefab` z override'ami danych i kontrolą ownership.
- [ ] LIB-071 Udostępnić `Scene.Load`, `Unload`, `SetActive`, `Find`, addytywne ładowanie i progress.
- [ ] LIB-072 Zdefiniować granicę sceny persistent/gameplay oraz przeżywanie entity przy zmianie sceny.
- [ ] LIB-073 Dodać lifecycle zdarzeń `SceneLoading`, `SceneLoaded`, `SceneActivated`, `SceneUnloading`, `SceneUnloaded`.
- [ ] LIB-074 Dodać test tworzenia/destrukcji 10k entity bez niestabilnego porządku i wycieków handles.

### Komponenty i query

- [ ] LIB-075 Udostępnić `Entity.Has<T>`, `TryGet<T>`, `GetRequired<T>`, `Add<T>`, `Remove<T>` tylko dla komponentów zarejestrowanych do skryptów.
- [ ] LIB-076 Zdefiniować registry komponentów: schema, wersja, serializacja, thread policy, read/write capability.
- [ ] LIB-077 Udostępnić bezpieczne pola komponentów przez generated accessors, nie przez dowolny reflection write.
- [ ] LIB-078 Dodać `Query<T...>` wyłącznie do faz dopuszczających iterację i z zakazem structural change w pętli.
- [ ] LIB-079 Dodać `Query.With`, `Without`, `Any`, `ChangedSince`, `Enabled` oraz stable order opt-in.
- [ ] LIB-080 Dodać batch command buffer dla `Spawn`, `Destroy`, Add/Remove component i przypisania tagów.
- [ ] LIB-081 Dodać `ComponentChanged` event z limitem, coalescingiem i bez alokacji per zmiana.
- [ ] LIB-082 Dodać walidator, że typ komponentu nie wystawia surowego adresu pamięci do Lua/graph.
- [ ] LIB-083 Dodać test aliasingu query, entity destroyed in query i command flush boundary.
- [ ] LIB-084 Udostępnić inspector-friendly metadata komponentów bez uczynienia edytora warunkiem runtime.

### Transform i hierarchia

- [ ] LIB-085 Udostępnić `Transform.LocalPosition`, `LocalRotation`, `LocalScale`, `WorldPose` i `SetWorldPose`.
- [ ] LIB-086 Udostępnić `Transform.Parent`, `SetParent(parent, keepWorld)` i kontrolę cykli hierarchii.
- [ ] LIB-087 Udostępnić `Transform.GetChild`, `ChildCount`, `FindChild` oraz iterację bez alokacji.
- [ ] LIB-088 Udostępnić `Translate`, `Rotate`, `LookAt`, `TransformPoint`, `InverseTransformPoint`.
- [ ] LIB-089 Zdefiniować kiedy world matrix jest aktualizowana po modyfikacji i jak to obserwuje renderer/fizyka.
- [ ] LIB-090 Dodać `TransformChanged` z informacją local/world i bez event storm.
- [ ] LIB-091 Dodać test zachowania `keepWorld`, parent destroy, deep hierarchy i zero/ujemnej skali.
- [ ] LIB-092 Zdefiniować serializację prefab override dla transformacji i hierarchy.

## 7. Czas, harmonogram, timery i asynchroniczność

- [ ] LIB-093 Udostępnić `Time.Delta`, `UnscaledDelta`, `FixedDelta`, `Elapsed`, `FrameIndex`, `FixedStepIndex`.
- [ ] LIB-094 Udostępnić `Time.Scale`, pause domains i jawne zasady zachowania `FixedTick` podczas pause.
- [ ] LIB-095 Udostępnić `Timer.Once`, `Timer.Repeat`, `Timer.Cancel`, `Timer.Pause`, `Timer.Resume` i owner entity.
- [ ] LIB-096 Zdefiniować kolejność timerów o tym samym czasie oraz catch-up po długiej klatce.
- [ ] LIB-097 Udostępnić `Coroutine`/`Task` tylko po wyborze modelu: generator Lua, state machine graph i C++ task adapter.
- [ ] LIB-098 Zaimplementować yield na sekundy, fixed steps, event, asset load i scene load bez blokowania main thread.
- [ ] LIB-099 Zaimplementować cancellation propagated przy Deactivated/Destroyed/scene unload.
- [ ] LIB-100 Dodać `AsyncResult<T>` z sukcesem, błędem, anulowaniem i callbackiem na właściwym wątku.
- [ ] LIB-101 Dodać watchdog na nieskończone coroutine/graph loop i diagnostykę miejsca utworzenia.
- [ ] LIB-102 Dodać test deterministyczności timerów i anulowania w tej samej fazie.

## 8. Zdarzenia, komunikaty i sygnały

- [ ] LIB-103 Rozdzielić zdarzenia lokalne entity, zdarzenia świata i komunikaty między systemami.
- [ ] LIB-104 Zdefiniować typowany `EventId`/schema zamiast dowolnych string payloadów w hot path.
- [ ] LIB-105 Udostępnić `Events.Subscribe`, `Unsubscribe`, `Emit`, `EmitDeferred` i `Broadcast`.
- [ ] LIB-106 Dodać filtry odbiorcy: entity, tag, komponent, scene, player i channel.
- [ ] LIB-107 Zdefiniować synchroniczne vs deferred dispatch i zakazać niejawnego mieszania.
- [ ] LIB-108 Dodać event payload value types, limity rozmiaru oraz wersjonowanie schema.
- [ ] LIB-109 Dodać `Signal<T>` dla lokalnej obserwacji bez globalnego busa.
- [ ] LIB-110 Dodać telemetry: liczba subskrypcji, dispatch duration, dropped/invalid events.
- [ ] LIB-111 Dodać test kolejności, unsubscribe during dispatch, destroyed owner i recursive emit.
- [ ] LIB-112 Dodać bridge zdarzeń gameplay do Visual Graph z typowanymi pinami.

## 9. Input i urządzenia

- [ ] LIB-113 Zdefiniować asset `InputActionMap`, action, binding, composite, deadzone i rebinding.
- [ ] LIB-114 Udostępnić `Input.ActionBool`, `ActionFloat`, `Action2D`, `Pressed`, `Released`, `Held`.
- [ ] LIB-115 Udostępnić akcje per player/local user, a nie jeden globalny singleton wejścia.
- [ ] LIB-116 Udostępnić urządzenia keyboard/mouse/gamepad/touch tylko przez normalizowany model input.
- [ ] LIB-117 Udostępnić `Pointer.Position`, delta, scroll, button i ray z aktywnej kamery.
- [ ] LIB-118 Udostępnić input contexts/priorities dla UI, gameplay, konsoli i debug overlay.
- [ ] LIB-119 Udostępnić rebinding runtime z walidacją konfliktu i serializacją ustawień użytkownika.
- [ ] LIB-120 Obsłużyć focus lost, device disconnect, background window i reset stanów pressed.
- [ ] LIB-121 Dodać recording/replay input do testów deterministycznych.
- [ ] LIB-122 Dodać test parity action state w FixedTick/Tick i między native/Lua/graph.

## 10. Fizyka, kolizje i ruch

- [ ] LIB-123 Zdefiniować komponenty i API `RigidBody`, `Collider`, `CharacterController`, `Joint`, `PhysicsMaterial`.
- [ ] LIB-124 Udostępnić force, impulse, velocity, angular velocity, kinematic move i sleep/wake.
- [ ] LIB-125 Udostępnić `Raycast`, sphere/box/capsule cast, overlap i closest point z warstwą maski.
- [ ] LIB-126 Udostępnić wersje `NonAlloc` zapytań i wymaganie bufora przy użyciu w ticku.
- [ ] LIB-127 Udostępnić `OnCollisionEnter/Stay/Exit` i `OnTriggerEnter/Stay/Exit` z deterministycznym payloadem.
- [ ] LIB-128 Zdefiniować relację między `FixedTick`, command buffer a momentem wykonania symulacji fizyki.
- [ ] LIB-129 Udostępnić collision layers, query layers i matrix interakcji jako asset/projekt config.
- [ ] LIB-130 Dodać API constraints/joints z limitem dostępnych, faktycznie obsługiwanych typów.
- [ ] LIB-131 Dodać API charakteru: slope limit, step offset, grounding, platform motion i gravity.
- [ ] LIB-132 Dodać debug draw fizyki i trace pojedynczego query bez wpływu na release path.
- [ ] LIB-133 Przetestować fast mover, spawn/despawn collider, parented rigidbody i scene unload.
- [ ] LIB-134 Przetestować determinism claim wyłącznie tam, gdzie backend fizyki go realnie gwarantuje.

## 11. Renderowanie, kamera, materiały i efekty

- [ ] LIB-135 Udostępnić `Camera` z pose, projection, FOV/ortho, near/far, viewport i priority.
- [ ] LIB-136 Udostępnić target renderowania, clear flags, layer mask i aktywną kamerę per player/view.
- [ ] LIB-137 Udostępnić `MeshRenderer` tylko przez istniejące assety mesh/material i bez ręcznego GPU ownership.
- [ ] LIB-138 Udostępnić sloty materiałów zgodnie z sekcjami mesha oraz `MaterialOverride` per slot.
- [ ] LIB-139 Udostępnić `MaterialInstance` jako instancję runtime z explicite lifetime i limitem wariantów.
- [ ] LIB-140 Udostępnić bezpieczny set shader parameterów po nazwie/ID walidowanym względem graph/material schema.
- [ ] LIB-141 Udostępnić `Light`, shadow policy, color temperature/intensity i layer mask, jeśli renderer je obsługuje.
- [ ] LIB-142 Udostępnić post-process volume/profile wyłącznie z assetowym, serializowalnym zestawem parametrów.
- [ ] LIB-143 Udostępnić particles/VFX jako assetową instancję: play, stop, seed, parameter, event.
- [ ] LIB-144 Udostępnić `Renderer.IsVisible`, bounds i frustum test bez wymuszania synchronizacji GPU.
- [ ] LIB-145 Dodać screen/world conversions, ray z kamery i screen capture przez kontrolowaną async ścieżkę.
- [ ] LIB-146 Dodać test lifecycle render resource przy destroy, scene unload i asset reload.

## 12. Audio, haptics i multimedia

- [ ] LIB-147 Zdefiniować `AudioClip`, `AudioBus`, `AudioMixer`, `AudioSource`, `AudioListener` i `Snapshot`.
- [ ] LIB-148 Udostępnić `Audio.Play`, `Stop`, pause, seek, volume, pitch, loop, spatial blend i priority.
- [ ] LIB-149 Udostępnić one-shot z owner entity i automatycznym cleanupem, bez wycieku source.
- [ ] LIB-150 Udostępnić mixer parameter i snapshot transition z walidacją assetu.
- [ ] LIB-151 Udostępnić occlusion/obstruction tylko po istniejącym backendzie i z kontrolą kosztu raycastów.
- [ ] LIB-152 Udostępnić audio events/markers i synchronizację gameplay bez polegania na niedokładnym wall clock.
- [ ] LIB-153 Udostępnić haptics per device/user przez capability i limity platformy.
- [ ] LIB-154 Dodać test asset unload podczas odtwarzania, scene change i pooled one-shots.

## 13. Assety, prefaby, ładowanie i serializacja

- [ ] LIB-155 Udostępnić `Assets.Load<T>`, `LoadAsync<T>`, `IsLoaded`, `Unload` z ownership handle.
- [ ] LIB-156 Udostępnić `Assets.Find` wyłącznie po stable ID/logicznej ścieżce projektu, nigdy po fizycznej ścieżce OS.
- [ ] LIB-157 Dodać typowane referencje: mesh, material, texture, audio, prefab, scene, animation, graph, input map.
- [ ] LIB-158 Zdefiniować cache, reference count, weak reference i unload policy dla assetów runtime.
- [ ] LIB-159 Dodać walidację asset compatibility i czytelną diagnostykę brakującej zależności.
- [ ] LIB-160 Udostępnić prefab instantiate z parametrami, parentem, transformem i callbackiem ukończenia.
- [ ] LIB-161 Zdefiniować prefab variants/overrides oraz kolejność zastosowania override'ów.
- [ ] LIB-162 Udostępnić `SaveGame` z wersjonowanymi schema, migracją i atomowym zapisem.
- [ ] LIB-163 Rozdzielić SaveGame, ustawienia użytkownika, stan sceny i dane sieciowe.
- [ ] LIB-164 Dodać limit wielkości zapisu, integrity hash i diagnostykę uszkodzonego pliku.
- [ ] LIB-165 Dodać test round-trip serializacji dla każdej publicznej wartości i referencji assetu.

## 14. Animacja, skinned mesh i cinematic

- [ ] LIB-166 Zdefiniować `Animator`, `AnimationClip`, `AnimatorController`, parameters i layer mask.
- [ ] LIB-167 Udostępnić play/crossfade, speed, normalized time, bool/int/float/trigger parameter i state query.
- [ ] LIB-168 Udostępnić animation events przez wersjonowany, typowany payload, bez reflection invoke po stringu.
- [ ] LIB-169 Udostępnić root motion z jasnym właścicielem: animator, character controller albo rigidbody.
- [ ] LIB-170 Udostępnić blend trees, IK targets i rig constraints tylko po istniejącej implementacji runtime.
- [ ] LIB-171 Udostępnić timeline/sequencer: play, pause, seek, bindings, marker events i skip semantics.
- [ ] LIB-172 Dodać test unload clip/controller podczas animacji i rebind po prefab instantiate.

## 15. UI i interakcja użytkownika

- [ ] LIB-173 Zdefiniować runtime UI tree, `UIDocument`, element ID, style asset i data binding boundary.
- [ ] LIB-174 Udostępnić create/destroy/show/hide elementów przez kontrolowaną kolejkę UI.
- [ ] LIB-175 Udostępnić text, image, button, toggle, slider, list, input field, scroll view i modal dialog.
- [ ] LIB-176 Udostępnić eventy click, pointer, submit, changed, focus, navigation z ownerem i unsubscribe.
- [ ] LIB-177 Udostępnić `UI.Find` tylko jako setup API; hot path ma cache'ować typed handle.
- [ ] LIB-178 Udostępnić data binding jednokierunkowy/dwukierunkowy z ochroną przed feedback loop.
- [ ] LIB-179 Udostępnić list virtualization i pooling zamiast tworzenia setek elementów per frame.
- [ ] LIB-180 Dodać input routing UI/gameplay, focus management, gamepad navigation i accessibility hooks.
- [ ] LIB-181 Dodać localization keys, formatowanie pluralizacji i fallback language bez stringów hardcoded w logice.
- [ ] LIB-182 Dodać test UI event po destroy, async scene transition i multi-player viewport.

## 16. Nawigacja, AI i zachowania

- [ ] LIB-183 Zdefiniować `NavMesh`, `NavAgent`, `NavObstacle`, query filter i area cost.
- [ ] LIB-184 Udostępnić path request sync/async, destination, stop, velocity, path status i remaining distance.
- [ ] LIB-185 Udostępnić steering/avoidance z jednoznaczną relacją do physics/character controller.
- [ ] LIB-186 Udostępnić percepcję: sight, hearing, proximity, team/filter i eventy z ograniczonym kosztem.
- [ ] LIB-187 Zaprojektować behaviour tree/state machine/utility AI jako asset runtime, nie jako drugi scheduler skryptów.
- [ ] LIB-188 Udostępnić blackboard z typowanymi kluczami, scope entity/team/world i serializacją.
- [ ] LIB-189 Udostępnić GOAP tylko po osobnym benchmarku i decyzji o kosztach; nie mieszać z BT w MVP.
- [ ] LIB-190 Dodać deterministic seed dla AI i debug snapshot decyzji.
- [ ] LIB-191 Dodać test path invalidation, target destroy i navmesh scene unload.

## 17. Gameplay framework

- [ ] LIB-192 Zdefiniować runtime `GameInstance` żyjący ponad scenami i własność globalnych usług.
- [ ] LIB-193 Zdefiniować `GameMode` jako reguły serwera/single-player, nie komponent dowolnego entity.
- [ ] LIB-194 Zdefiniować `GameState` jako replikowalny stan meczu/partii.
- [ ] LIB-195 Zdefiniować `Player`, `PlayerController`, `Pawn/Character`, `PlayerState` i lifecycle join/leave.
- [ ] LIB-196 Zdefiniować `CameraManager` i policy camera possession/follow/spectate.
- [ ] LIB-197 Dodać `SpawnPoint`, respawn policy, team assignment i match phases jako asset/config.
- [ ] LIB-198 Dodać `Tag`, `Layer`, `Team`, `Faction` z jednolitymi filtrami dla AI, physics, render i damage.
- [ ] LIB-199 Dodać system damage/heal z typowanym `DamageEvent`, source, instigator, hit i odpornościami.
- [ ] LIB-200 Dodać health, attributes, inventory, equipment i pickup jako opcjonalne moduły, nie wbudowane pola entity.
- [ ] LIB-201 Dodać gameplay effects/abilities z cooldown, cost, target rules i cancellation.
- [ ] LIB-202 Dodać checkpoint, game flow, pause, restart, win/lose i transition scene.
- [ ] LIB-203 Dodać sample: third-person controller, top-down controller, platformer i simple shooter.

## 18. Sieć i multiplayer

Sieć wymaga oddzielnej decyzji architektonicznej. Nie wolno eksponować
pozornie działającego `Network.*`, dopóki nie ma authority, transportu,
serializacji i modelu replikacji.

- [ ] LIB-204 Wybrać model: authoritative server, listen server, peer-to-peer lub offline-only w pierwszym wydaniu.
- [ ] LIB-205 Zdefiniować `NetworkRole`, `NetworkObject`, owner, authority i lifecycle spawn/despawn.
- [ ] LIB-206 Zdefiniować schema replikacji komponentów z quantization, delta i wersjonowaniem.
- [ ] LIB-207 Udostępnić RPC reliable/unreliable z kierunkiem client→server/server→client i walidacją ownership.
- [ ] LIB-208 Udostępnić network variables z hookiem zmiany i bez automatycznej replikacji dowolnych obiektów.
- [ ] LIB-209 Zdefiniować input command, snapshot, interpolation, prediction i reconciliation przed API ruchu multiplayer.
- [ ] LIB-210 Wprowadzić kontrolę czasu, tick rate, packet budget i backpressure.
- [ ] LIB-211 Dodać zabezpieczenia: validate server side, rate limits, payload size, spoofing i deserialization bounds.
- [ ] LIB-212 Dodać replay/network simulation: latency, jitter, loss, reorder i disconnect.
- [ ] LIB-213 Dodać test host/client, late join, reconnect, despawn during RPC i schema mismatch.

## 19. Platforma, pliki, system i usługi

- [ ] LIB-214 Udostępnić `Platform` tylko przez capability: język, region, path do user data, clipboard, URL, vibration.
- [ ] LIB-215 Zakazać skryptom dowolnego odczytu/zapisu systemu plików; udostępnić sandbox `UserStorage`.
- [ ] LIB-216 Udostępnić atomowy `UserStorage.Read/Write/Delete/List` z quota i async API.
- [ ] LIB-217 Udostępnić settings audio/video/input z transakcją apply/revert i walidacją możliwości urządzenia.
- [ ] LIB-218 Udostępnić achievements, cloud save, DLC i platform user tylko jako opcjonalne adaptery capability.
- [ ] LIB-219 Udostępnić locale, time zone i safe date-time tylko w granicach potrzeb gry.
- [ ] LIB-220 Dodać test sandbox escape, quota, atomic write failure i capability unavailable.

## 20. Diagnostyka, debug i narzędzia twórcy

- [ ] LIB-221 Udostępnić `Log.Trace/Debug/Info/Warn/Error` z kategorią, entity, world i rate limiting.
- [ ] LIB-222 Udostępnić structured fields zamiast składania każdego loga przez format string.
- [ ] LIB-223 Udostępnić `Assert`, `Require`, `SoftFail` z polityką development/release i stack trace skryptu.
- [ ] LIB-224 Udostępnić `Debug.DrawLine/Ray/Box/Sphere/Text` z duration, channel i build guard.
- [ ] LIB-225 Udostępnić profiler scopes, counters, timeline events i allocation counters bez wpływu na release.
- [ ] LIB-226 Udostępnić console commands z typowaną argumentacją, permissions i help generowanym z manifestu.
- [ ] LIB-227 Udostępnić runtime inspector read-only dla entity, componentów, timerów, subscriptions i graph execution.
- [ ] LIB-228 Udostępnić controlled hot reload Lua/Visual Graph z migracją state albo jawnym restartem behaviour.
- [ ] LIB-229 Udostępnić breakpoints/step dla Visual Graph oraz source location dla Lua errors.
- [ ] LIB-230 Dodać crash report z wersją API, assetami, błędem i ostatnimi zdarzeniami bez danymi poufnymi.

## 21. Wydajność, wielowątkowość i determinism

- [ ] LIB-231 Oznaczyć każdą funkcję: main thread, worker-safe, render-thread command albo forbidden in phase.
- [ ] LIB-232 Zdefiniować read snapshots i command queues dla pracy równoległej bez data races.
- [ ] LIB-233 Dodać budget wykonania Lua/Visual Graph per frame/behaviour oraz policy przekroczenia.
- [ ] LIB-234 Dodać allocation budget i telemetry per moduł biblioteki.
- [ ] LIB-235 Przenieść konwersje string→ID poza hot path, cache'ować bindingi i query plans.
- [ ] LIB-236 Dodać `NonAlloc`/batch warianty raycast, find, event emit i transform read tam, gdzie benchmark to uzasadnia.
- [ ] LIB-237 Zdefiniować deterministic subset biblioteki: random, timers, order, input replay, fixed simulation.
- [ ] LIB-238 Oznaczyć API niedeterministyczne (wall time, platform, async IO, rendering) w metadata i Visual Graph.
- [ ] LIB-239 Dodać benchmarki spawn/query/transform/event/physics call dla 1k, 10k i 100k entity.
- [ ] LIB-240 Dodać test ThreadSanitizer/ASan lub odpowiedni Windows-equivalent dla kolejki komend i lifetime handles.

## 22. Parzystość C++, Lua i Visual Graph

- [ ] LIB-241 Ustalić kanoniczne nazwy modułów: `World`, `Entity`, `Transform`, `Time`, `Math`, `Input`, `Physics`, `Audio`, `Assets`.
- [ ] LIB-242 Wygenerować Lua module table z katalogu, a nie pisać ręczne aliasy odrębne od C++.
- [ ] LIB-243 Wygenerować definicje node/pin Visual Graph z tych samych sygnatur.
- [ ] LIB-244 Dodać typy konwersji graph tylko, gdy nie tracą informacji albo wyświetlają jawne ostrzeżenie.
- [ ] LIB-245 Dodać przykład i test każdego API w C++, Lua i graph, z wyjątkiem celowo native-only API.
- [ ] LIB-246 Oznaczyć native-only/authoring-only/server-only API w manifeście i ukryć je z niewłaściwego frontend-u.
- [ ] LIB-247 Dodać source map: node/pin → funkcja katalogu → implementacja runtime → dokumentacja.
- [ ] LIB-248 Dodać generator Markdown/reference z manifestu, aby dokumentacja nie starzała się niezależnie od kodu.
- [ ] LIB-249 Dodać autocompletion Lua i podpowiedzi node search z opisem, kategorią, przykładami i wersją.
- [ ] LIB-250 Dodać test, że nazwa, podpis i semantics opisane w docs odpowiadają manifestowi builda.

## 23. Testy, jakość i Definition of Done

- [ ] LIB-251 Stworzyć osobny target testów `Engine21kbLibrary` bez zależności od UI edytora.
- [ ] LIB-252 Dodać testy unit dla typów, marshalling, handles, errors i metadata.
- [ ] LIB-253 Dodać testy integracyjne runtime dla lifecycle, command buffer, scene i prefab.
- [ ] LIB-254 Dodać golden tests Visual Graph: graph asset → binding → runtime result.
- [ ] LIB-255 Dodać testy Lua dla błędów typu, życia entity, coroutine cancellation i stack trace.
- [ ] LIB-256 Dodać testy property/fuzz dla parserów, asset refs, event payload i serializacji save.
- [ ] LIB-257 Dodać test deterministycznego replay dla minimalnej sceny gameplay.
- [ ] LIB-258 Dodać testy soak: spawn/destroy, load/unload scene, subscribe/unsubscribe, hot reload.
- [ ] LIB-259 Dodać testy performance jako progi regresji, oddzielnie od testów funkcjonalnych.
- [ ] LIB-260 Wymagać clean build bez warningów własnego kodu i bez nowych warningów zależności w CI.
- [ ] LIB-261 Wymagać `git diff --check`, testów właściwego targetu i przeglądu manifest compatibility dla każdej zmiany API.
- [ ] LIB-262 Utrzymywać matrix platform/backend/frontend dla funkcji, zamiast deklarować wsparcie bez testu.

## 24. Kolejność realizacji

### M0 — kontrakt i działający pionowy wycinek

Wykonać LIB-001..015, 016..030, 031..040, 041..056, 065..092,
093..112, 113..122 oraz tylko niezbędne 123..134 i 155..161. Rezultat:
nowy projekt może stworzyć prefab gracza, sterować nim, wykonywać raycast,
reagować na kolizję, odtworzyć dźwięk, spawnąć prefab i przeładować scenę — w
Native, Lua i Visual Graph.

### M1 — kompletna gra single-player

Wykonać rendering/kamerę (135..146), UI (173..182), animację (166..172),
gameplay framework (192..203), save/settings (162..165, 214..220) oraz
diagnostykę (221..230). Rezultat: menu, gameplay loop, stan gry, zapis,
debugowanie i pakietowanie są funkcjami runtime, nie trikami edytora.

### M2 — skala i narzędzia produkcyjne

Wykonać AI/nawigację (183..191), pełną observability, hot reload, benchmarki,
budżety i parity docs (231..250). Rezultat: zespół może tworzyć i diagnozować
większą grę bez niekontrolowanego kosztu skryptów.

### M3 — multiplayer tylko po decyzji o modelu

Wykonać 204..213 po zatwierdzeniu transportu, authority i targetowych
platform. Multiplayer nie blokuje M0–M2 i nie może wymuszać przypadkowej
architektury na local gameplay.

## 25. Referencje projektowe

Poniższe dokumentacje są inspiracją dla zakresu i semantyki, nie specyfikacją
do kopiowania. Engine21kb utrzymuje własne kontrakty, nazwy i ograniczenia
zgodne z własnym runtime'em.

- [Unity — MonoBehaviour i lifecycle](https://docs.unity3d.com/2023.1/Documentation/ScriptReference/MonoBehaviour.html)
- [Unity — script execution order](https://docs.unity3d.com/Manual/execution-order.html)
- [Unreal Engine — Gameplay Framework](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-framework-in-unreal-engine)
- [Unreal Engine — Blueprint foundations](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-foundations)
- [Unreal Engine — gameplay systems](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-systems-in-unreal-engine)
- [Godot — overridable functions i lifecycle Node](https://docs.godotengine.org/en/stable/tutorials/scripting/overridable_functions.html)
- [Godot — high-level multiplayer](https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html)

## 26. Kryterium ukończenia biblioteki

Biblioteka nie jest „ukończona” po stworzeniu listy klas ani po pokazaniu
węzłów w edytorze. Dany moduł jest gotowy wyłącznie, gdy ma realny backend
runtime, jawne ownership i lifecycle, parity wspieranych frontendów,
diagnostykę, testy błędów oraz test integracyjny wykonany w Play Mode.
