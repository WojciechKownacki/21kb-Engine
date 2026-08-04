# Skeletal Mesh i Animator — notatka architektoniczna

**Status:** prywatna notatka lokalna. Plik znajduje się w ignorowanym przez Git
katalogu `others` i nie może trafić do repozytorium ani na GitHub.

## Cel analizy

Analiza porównuje bieżący system animacji Engine21kb z podziałem
odpowiedzialności użytym w lokalnym źródle referencyjnym Unreal Engine:

`F:\ref\UnrealEngine`

Unreal jest tutaj wzorcem odpowiedzialności i przepływu danych, a nie API,
nazewnictwa, drzewa klas ani interfejsu do kopiowania. Docelowa implementacja
21kb ma pozostać własna, lekka i dopasowana do architektury silnika.

## Najważniejszy wniosek

`SkeletalMesh` nie jest odpowiednikiem `Animatora`. W Unreal nie istnieje jedna
klasa odpowiadająca Unity-style `Animator`. Odpowiedzialność jest rozdzielona:

| Warstwa | Unreal | Odpowiedzialność |
| --- | --- | --- |
| Asset geometrii | `USkeletalMesh` | LOD-y, materiały, geometria deformowana, skin weights, morph targets, powiązanie z kośćmi i dane renderowania |
| Asset zgodności riga | `USkeleton` | Tożsamość szkieletu, zgodność animacji, dane potrzebne do współdzielenia klipów i podglądu |
| Komponent sceny | `USkeletalMeshComponent` | Umieszcza siatkę szkieletową w świecie, łączy ją z animacją i rendererem |
| Logika autorska | `UAnimBlueprint` | AnimGraph, maszyny stanów, parametry, blendy i reguły wyboru animacji |
| Instancja runtime | `UAnimInstance` | Stan wykonania animacji przypisany do konkretnego komponentu siatki |
| Narzędzie assetu | Skeletal Mesh Editor | Podgląd i authoring siatki, kości, LOD-ów, materiałów, morfów i danych riga |
| Narzędzie animacji | Animation Blueprint Editor | Graf animacji, state machines, parametry, debug oraz podgląd działającej pozy |
| Wspólny podgląd | Persona | Scena podglądowa i debugowy komponent siatki współdzielone przez edytory animacyjne |

Dlatego najbliższe odpowiedniki obecnych pojęć 21kb są następujące:

- `AnimatorController` odpowiada funkcjonalnie autorskiemu grafowi
  `UAnimBlueprint`/AnimGraph;
- stan wykonawczy wyprowadzany z komponentu `Animator` odpowiada roli
  `UAnimInstance`;
- przypięcie sterownika animacji do renderowanej postaci jest integrowane przez
  `USkeletalMeshComponent`, a nie przez asset `USkeletalMesh`;
- `Skeletal Mesh Editor` i `Animator Editor` muszą pozostać osobnymi
  narzędziami, choć powinny współdzielić tę samą scenę podglądową.

## Wzorzec runtime z Unreal Engine

### `USkeletalMesh`

Źródło referencyjne:

`Engine/Source/Runtime/Engine/Classes/Engine/SkeletalMesh.h`

Deklaracja `USkeletalMesh` znajduje się około linii 439. Jest to asset, nie
komponent i nie instancja odtwarzania animacji. Zawiera lub udostępnia dane
niezbędne rendererowi i importerowi, między innymi szkielet referencyjny, dane
LOD oraz render resource.

### `USkeleton`

Źródło referencyjne:

`Engine/Source/Runtime/Engine/Classes/Animation/Skeleton.h`

Deklaracja `USkeleton` znajduje się około linii 294. Unreal oddziela wspólną
tożsamość riga od konkretnej deformowanej siatki. Dzięki temu wiele siatek,
klipów i grafów animacji może używać zgodnego szkieletu. To rozdzielenie jest
wartościowe dopiero wtedy, gdy 21kb rzeczywiście obsługuje współdzielenie i
walidację kompatybilności; nie należy kopiować tej warstwy bez kontraktu.

### `USkeletalMeshComponent`

Źródło referencyjne:

`Engine/Source/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h`

Deklaracja znajduje się około linii 318. Komponent posiada między innymi:

- klasę instancji animacji (`AnimClass`, około linii 372);
- runtime `AnimScriptInstance` (około linii 376);
- tryb animacji (`AnimationMode`, około linii 620);
- dostęp do instancji przez `GetAnimInstance()` (około linii 1025).

To komponent sceny jest punktem integracji siatki, pozy i animacji. Nie oznacza
to jednak, że powinien przechowywać całą bieżącą paletę kości jako serializowany
stan. Paleta jest wynikiem ewaluacji i należy do cache'u runtime/renderera.

### `UAnimBlueprint` i `UAnimInstance`

Źródła referencyjne:

- `Engine/Source/Runtime/Engine/Classes/Animation/AnimBlueprint.h`;
- `Engine/Source/Runtime/Engine/Classes/Animation/AnimInstance.h`.

`UAnimBlueprint` jest autorskim assetem logiki. Wskazuje docelowy szkielet
(`TargetSkeleton`, około linii 91) oraz opcjonalną siatkę podglądową (około
linii 275). `UAnimInstance` jest obiektem przejściowym należącym do konkretnego
`USkeletalMeshComponent`; deklaracja znajduje się około linii 352.

W 21kb analogiczny stan wykonawczy powinien być wyprowadzany z serializowanego
komponentu i assetu sterownika. Playheady, aktywne przejścia, próbki blendów,
bieżące wartości pozy oraz palety kości nie mogą być drugim serializowanym
źródłem prawdy.

## Wzorzec edytorów

### Skeletal Mesh Editor

Źródła referencyjne:

- `Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditor.cpp`;
- `Engine/Source/Editor/SkeletalMeshEditor/Private/SkeletalMeshEditorMode.cpp`.

Układ z `SkeletalMeshEditorMode.cpp` dzieli okno na:

- toolbox po lewej;
- centralny viewport 3D;
- drzewo szkieletu, morph targets oraz metadane krzywych po prawej u góry;
- właściwości assetu, właściwości zaznaczenia i ustawienia zaawansowanego
  podglądu po prawej na dole.

Edytor uruchamia podgląd domyślnie w reference pose. Służy do inspekcji assetu,
kości, skin weights, materiałów, LOD-ów, morfów, socketów i zgodności riga. Nie
jest edytorem maszyny stanów.

### Animation Blueprint Editor — odpowiednik okna Animatora

Źródła referencyjne:

- `Engine/Source/Editor/AnimationBlueprintEditor/Private/AnimationBlueprintEditor.cpp`;
- `Engine/Source/Editor/AnimationBlueprintEditor/Private/AnimationBlueprintEditorMode.cpp`.

Układ okna zawiera:

- viewport postaci i ustawienia podglądu po lewej;
- dokument grafu, AnimGraph lub state machine w centrum;
- wyniki kompilacji Blueprintu i wyszukiwanie pod dokumentem — jest to
  mechanizm UE, a nie wymaganie dla 21kb;
- Details, ustawienia podglądu, asset browser i narzędzia debug po prawej.

To jest właściwy wzorzec dla przyszłego okna `Animator`/Animation Controller w
21kb. Okno powinno edytować asset sterownika, a nie komponent instancji.
Komponent w Inspectorze jedynie wybiera sterownik oraz ustawienia instancji.
W 21kb panel wyników powinien pokazywać diagnostykę automatycznej walidacji i
budowania assetu. Nie jest potrzebny jawny przycisk `Compile` kopiujący flow
Blueprintów UE.

### Wspólna scena podglądowa Persona

Źródła referencyjne:

- `Engine/Source/Editor/Persona/Private/PersonaToolkit.cpp`;
- `Engine/Source/Editor/Persona/Private/AnimationEditorPreviewScene.cpp`.

Persona tworzy wspólną scenę podglądową i używa
`UDebugSkelMeshComponent`. Potrafi przełączać reference pose, pojedynczy asset
animacji oraz Animation Blueprint. Współdzielenie infrastruktury podglądu jest
ważnym wzorcem: Skeletal Mesh Editor, Animation Clip Editor i Animator Editor
nie powinny posiadać trzech rozbieżnych implementacji renderowania i ewaluacji
pozy.

## Stan Engine21kb

### Obecny `Animator`

Źródło:

`sources/engine/include/engine/scene/AnimationAssets.hpp`

Aktualny komponent `Animator` jest małą, autorską konfiguracją:

- `controllerAssetId`;
- `speed`;
- `enabled`;
- `rootMotionOwner`.

`AnimatorController` posiada parametry, warstwy, stany, przejścia, maski,
jednowymiarowe blend tree oraz ograniczenia `TwoBoneIK`, `Aim` i
`CopyTransform`. Klipy animują `LocalTransform` encji adresowanych ścieżkami
nazw dzieci.

Jest to działający fundament sterownika transformów, ale nie pełny skeletal
animation runtime. Nie deformuje wierzchołków siatki i nie wiąże klipów z
kanoniczną hierarchią kości assetu.

### Bieżący Inspector i brak narzędzi

Inspector pokazuje dla `Animatora` tylko Controller, Speed, Enabled i Root
Motion. Obecny katalog paneli dokowanych zawiera Scene, Inspector, Assets,
Console, Script Editor, Plugins oraz Material Editor, ale nie zawiera osobnego
Animator Editor, Animation Clip Editor ani Skeletal Mesh Editor.

### Brak skinningu w rendererze

Źródła:

- `sources/renderer/include/kb/render/resources/RenderResources.hpp`;
- `sources/renderer/tests/RenderResourceRegistryTests.cpp`.

Format wierzchołka z joints i weights jest zarezerwowany, lecz runtime skinningu
nie istnieje. Testy świadomie wymagają odrzucenia skinned vertex format oraz
skinned glTF do czasu powstania pełnej obsługi. Obecny podgląd siatki statycznej
nie jest podglądem Skeletal Mesh.

## Mapowanie do `Components.md`

Najbliższe pozycje prywatnego backlogu:

- zadanie 42, **Wiązanie szkieletu** — asset hierarchii, mapa kości i reference
  pose; bieżąca paleta pozostaje w cache'u systemu;
- zadanie 43, **Geometria odkształcana** — renderowana geometria ze skinningiem,
  morfami i wiązaniem do pozy ewaluowanej poza komponentem;
- zadanie 45, **Reguła pozy szkieletu** — IK, aim, twist, limity i korekty pozy;
- obecny `Animator` — sterownik animacji wskazujący asset controller/graph;
  pozostaje osobną odpowiedzialnością od renderowanej geometrii.

Nie należy składać zadań 42 i 43 w jeden wielki komponent tylko dlatego, że
Unreal ma rozbudowany `USkeletalMeshComponent`. W 21kb należy zachować granice
autorytatywnego stanu zapisane w `Components.md`: konfiguracja instancji jest w
komponencie, assety są współdzielone, a bieżąca poza i paleta są stanem
wyprowadzonym.

## Docelowy podział w Engine21kb

### Assety

1. Asset szkieletu lub kanoniczna część assetu skeletal mesh:
   hierarchia kości, rodzice, nazwy/identyfikatory, local reference pose,
   inverse bind matrices oraz sygnatura kompatybilności.
2. Asset deformowanej geometrii:
   pozycje, normalne, tangenty, UV, indeksy, joint indices, joint weights,
   materiałowe sekcje, LOD-y i morph targets.
3. Animation Clip:
   skompresowane ścieżki kości, czas, zdarzenia i root motion.
4. Animator Controller/Graph:
   parametry, warstwy, maski, state machines, blendy, przejścia i reguły pozy.

### Komponenty sceny

1. Komponent wiązania szkieletu wskazuje kanoniczny asset riga oraz ustawienia
   instancji wymagające serializacji.
2. Komponent geometrii odkształcanej wskazuje asset siatki i materiały oraz
   pobiera pozę z jednoznacznego źródła.
3. `Animator` wskazuje controller/graph i zawiera wyłącznie ustawienia autorskie
   instancji, takie jak speed, enabled oraz polityka root motion.
4. Reguły pozy pozostają konfigurowalnymi ograniczeniami, wykonywanymi w jawnej
   fazie po blendowaniu klipów.

### Stan runtime, który nie jest komponentem

- wartości parametrów i triggerów;
- playheady;
- aktywne stany i przejścia;
- sampled local pose;
- component/model-space pose;
- globalne macierze kości;
- finalna paleta skinningu;
- uchwyty buforów GPU;
- kolejki i scratch memory ewaluatora.

Ten stan powinien należeć do systemu animacji i renderera, mieć jawny czas życia
i być odtwarzalny z autorytatywnej konfiguracji.

## Krytyczna implementacja — bez obejść

Nie wolno uznać animowania hierarchii zwykłych encji za implementację
Skeletal Mesh. Prawdziwa krytyczna ścieżka obejmuje cały przepływ:

1. importer odczytuje skin, joints, weights, inverse bind matrices, animacje i
   morph targets oraz waliduje indeksy, sumy wag i zgodność hierarchii;
2. asset pipeline tworzy deterministyczny asset szkieletu i deformowanej
   geometrii, zachowując jedno źródło prawdy dla mapowania kości;
3. animation runtime sampluje klipy do lokalnej pozy kości;
4. controller wykonuje state machine, transitions, layers, masks i blends;
5. constraints/IK korygują pozę w jawnej, stabilnej kolejności;
6. system wylicza component/model-space pose i finalną paletę z inverse bind;
7. renderer przekazuje paletę do właściwego GPU skinning path i rysuje sekcje
   siatki bez ścieżki statycznego fallbacku;
8. bounding volumes oraz culling uwzględniają deformację lub konserwatywne
   granice assetu;
9. root motion ma jednego jawnego właściciela zapisu transformacji;
10. Skeletal Mesh Editor, Clip Editor i Animator Editor używają dokładnie tego
    samego runtime podglądu, który działa w grze;
11. testy obejmują importer, kompatybilność riga, sampling, blending, paletę,
    skinning renderera, serializację, undo/redo i scenariusze headless edytora;
12. wydajność jest potwierdzona pomiarami dla wielu animowanych postaci,
    batchingiem aktualizacji i kontrolowanymi alokacjami.

Niedopuszczalne obejścia:

- osobna encja sceny dla każdej kości jako docelowy skeletal runtime;
- CPU-transformowanie całej siatki tylko po to, aby ominąć brak backendu GPU;
- renderowanie bind pose po cichym niepowodzeniu ewaluacji;
- akceptowanie skinned assetu i ignorowanie joints/weights;
- przechowywanie finalnej palety jako serializowanego komponentu;
- osobny, uproszczony renderer działający wyłącznie w edytorze;
- tekstowa edycja assetu udająca docelowe okno Animatora;
- deklarowanie ukończenia po samym imporcie, bez deformacji w runtime.

## Decyzja dla dalszych prac

Docelowo potrzebne są trzy osobne okna assetowe korzystające ze wspólnego
preview runtime:

1. **Skeletal Mesh Editor** — mesh, skeleton tree, weights, LOD, materials,
   morphs, sockets i reference pose;
2. **Animation Clip Editor** — timeline, scrub, curves, events, root motion i
   podgląd wybranego riga;
3. **Animator/Animation Controller Editor** — parametry, warstwy, state graph,
   transitions, blend trees/graph, constraints i debug aktywnej instancji.

Inspector komponentu `Animator` nie zastępuje Animator Editora. Inspector ma
pozostać prostym miejscem przypięcia assetu controller/graph oraz ustawień
instancji. Skeletal Mesh Editor nie zastępuje Animator Editora i nie powinien
przejmować authoringu maszyn stanów.

## Zweryfikowane punkty źródłowe

- `SkeletalMesh.h`: `USkeletalMesh`, reference skeleton, render resource i LOD;
- `Skeleton.h`: `USkeleton` i preview skeletal mesh;
- `SkeletalMeshComponent.h`: `USkeletalMeshComponent`, `AnimClass`,
  `AnimScriptInstance`, `AnimationMode`, `GetAnimInstance()`;
- `AnimBlueprint.h`: `UAnimBlueprint`, `TargetSkeleton`, preview mesh;
- `AnimInstance.h`: przejściowa instancja związana z komponentem;
- `SkeletalMeshEditor.cpp` i `SkeletalMeshEditorMode.cpp`: osobny edytor assetu
  oraz jego panele;
- `AnimationBlueprintEditor.cpp` i `AnimationBlueprintEditorMode.cpp`: osobny
  edytor grafu, state machine i debug;
- `PersonaToolkit.cpp` i `AnimationEditorPreviewScene.cpp`: współdzielony
  podgląd reference pose, animation asset i Animation Blueprint;
- `AnimationAssets.hpp`: aktualny zakres `Animatora` w 21kb;
- `DockTypes.hpp`: brak paneli Animator/Skeletal Mesh/Animation Clip;
- `RenderResources.hpp` i `RenderResourceRegistryTests.cpp`: brak działającego
  skinning runtime i zamierzone odrzucanie skinned assets.

## Audyt implementacji 21kb — 2026-08-02

Audyt wykonany równolegle dla runtime, renderera, edytora i user flow
potwierdził, że pełna implementacja Skeletal Mesh/Animator nie istnieje.
Obecnego rozwiązania nie należy oznaczać jako „częściowy Skeletal Mesh”. Jest to
osobny, wartościowy system animacji hierarchii encji.

### Macierz kompletności

| Obszar | Stan | Dowód / konsekwencja |
| --- | --- | --- |
| Controller transformów | zaawansowany fundament | Parametry, stany, transitions, layers/masks, blend 1D, events, hot reload, root motion i constraints są wykonywane w `SceneAnimatorService.cpp` |
| Animation Clip | tylko transform hierarchy | Track wskazuje ścieżkę nazw dzieci, nie indeks kości ani tożsamość szkieletu |
| Skeleton asset | brak | Brak parent indices, stable bone IDs, reference pose, inverse bind i compatibility signature |
| Skeletal Mesh asset | brak | Brak LOD bone maps, skin sections, morph targets i autorytatywnego powiązania z rigiem |
| Import glTF skin | jawnie odrzucony | `RenderMeshGltfImporter.cpp:79-101` odrzuca `JOINTS/WEIGHTS`, a `:248-251` odrzuca node z `skin` |
| Compact pose runtime | brak | Bieżące bindingi wskazują `SceneEntity`; wynik jest zapisywany do `TransformComponent` |
| GPU skinning | brak | Format wierzchołka jest zarezerwowany, ale rejestr i scene pipeline go odrzucają; brak bone palette i shader permutations |
| Animated bounds/culling | brak | Widoczność korzysta ze statycznej sfery assetu transformowanej macierzą obiektu |
| Skeletal Mesh Editor | brak | Nie istnieje panel kind, dokument, viewport, Skeleton Tree ani asset details |
| Animation Clip Editor | brak | Brak timeline, scrub, transportu, curves/events authoringu i preview pose |
| Animator/Controller Editor | brak | Asset controllera otwiera się jako tekst w Script Editorze |
| Live animation debug | brak | Runtime udostępnia query stanu, ale UI nie wybiera debug targetu i nie wizualizuje wag ani przejść |
| UE5-like flow | brak | Brak typed asset editor dispatch, workspace'ów, wspólnego Persona-like preview i semantycznego undo |

### Najważniejsze dowody runtime

- `AnimationAssets.hpp:17-24`: binding klipu jest tekstową ścieżką encji;
- `SceneAnimatorService.cpp:46-65`: resolver skanuje dzieci po nazwie i odrzuca
  niejednoznaczność;
- `SceneAnimatorService.cpp:242-268`: sampling TRS;
- `SceneAnimatorService.cpp:573-617`: ewaluacja stanu/blendu;
- `SceneAnimatorService.cpp:911-1066`: transitions, asset binding i runtime;
- `SceneAnimatorService.cpp:1316-1548`: update, root motion i zapis pozy;
- `SceneState.hpp:68-124`: poprawne oddzielenie autorskiej konfiguracji od
  transient playheads, parameters, bindings i IK targets;
- `AnimationRuntimeTests.cpp:35-981`: szerokie testy istniejącego Animatora
  transformów.

### Ryzyka wydajnościowe obecnego modelu dla skeletonu

Obecna implementacja jest właściwa dla niewielkiej hierarchii encji, ale nie
może być rozszerzona mechanicznie na setki kości:

- `EvaluateState` przeszukuje bindingi i tracki w układzie prowadzącym do kosztu
  zbliżonego do `bindings × tracks`;
- sampling wykonuje `upper_bound` per track bez skompresowanego klipu i cursorów;
- IK modyfikuje scenowe Transformy i wielokrotnie synchronizuje całą hierarchię;
- kość jako `SceneEntity` zwiększyłaby koszty ECS, hierarchy sync i cache misses;
- nie istnieje benchmark Animatora dla 100/1000 rigów po 100–200 kości.

Skeletal runtime wymaga ciągłego, indeksowanego pose bufferu. Kości nie powinny
być encjami sceny. Encjami mogą być właściciel postaci i jawnie eksportowane
sockety/attachment points.

### Najważniejsze dowody edytora

- `DockTypes.hpp:17-28`: brak `SkeletalMeshEditor`, `AnimationClipEditor` i
  `AnimatorControllerEditor`;
- `DefaultDockWorkspace.cpp:9-30`: brak odpowiednich workspace'ów;
- `EditorAssetBrowserDoubleClickHandler.cpp:74-79`: klip i controller są
  kierowane do wspólnego `OpenAnimationAsset`;
- `EditorSceneContext.cpp:8889-8905`: `OpenAnimationAsset` wywołuje wyłącznie
  `scriptEditor_.Open(...)`;
- `InspectorPanelRenderer.cpp:1462-1487`: Inspector Animatora pokazuje tylko
  Controller, Speed, Enabled i Root Motion;
- `AnimationAssetIO.cpp:394-474`: controller serializuje dane logiczne, ale nie
  stabilne node IDs, graph layout, komentarze, grupy ani AnimGraph;
- `EditorMeshPreviewService` generuje statyczne preview/thumbnail, nie
  interaktywną scenę skeletal;
- `EditorMaterialPreviewScene` i Material Editor są użytecznym wzorcem dla
  izolowanej preview scene, working copy, dirty/save/revert i per-document undo.

### Testy utrwalające obecne obejście

Obecne testy UI nie mogą zostać zachowane jako kryterium ukończenia:

- `EditorSelfTest.cpp:931-934` uznaje controller otwarty w Script Editorze za
  zapisywalną powierzchnię assetu animacji;
- `HeadlessAutomationScenario.json:115-118` przechwytuje panel `script_editor`
  pod nazwą checkpointu `animation-controller-editor`.

Przy krytycznej implementacji testy te trzeba zastąpić testami dedykowanego
workspace. Script Editor nie może pozostać cichym fallbackiem dla uszkodzonego
lub nieobsługiwanego assetu animacji.

### Zweryfikowany układ i flow UE5

Skeletal Mesh Editor:

- lewa kolumna około 20%: toolbox;
- centrum około 60%: interaktywny viewport;
- prawa kolumna około 20%: Skeleton Tree/Morph Targets/Curves u góry oraz Asset
  Details/Details/Advanced Preview na dole;
- reference pose jako domyślny tryb otwarcia assetu.

Animator/Animation Controller Editor:

- lewa kolumna około 25%: preview i preview properties/Pose Watch;
- centrum około 55%: document graph/state machine oraz diagnostics/find
  results;
- prawa kolumna około 20%: Details/Advanced Preview oraz Asset Browser;
- breadcrumbs, state/transition navigation, parameters/layers i live debug.

Animation Clip Editor:

- Skeleton Tree i asset details;
- viewport z kompatybilnym skeletal meshem;
- timeline/outliner z transportem, dokładnym scrubem, snappingiem, curves i
  events;
- Details, Advanced Preview i kompatybilny Asset Browser.

Proporcje są wzorcem domyślnego layoutu, a nie sztywnymi pikselami. Splittery i
taby muszą zachowywać się jak pełny docking workspace oraz zapamiętywać układ.

### Krytyczny flow użytkownika

1. Import skeletal glTF/FBX tworzy lub wskazuje Skeleton asset, Skeletal Mesh,
   materiały, morphs i Animation Clips; raportuje niezgodności zamiast je
   ignorować.
2. Double-click każdego typed assetu otwiera właściwy dedykowany dokument;
   ponowne otwarcie fokusuje istniejący dokument.
3. Skeletal Mesh Editor pokazuje reference pose, Skeleton Tree, weights,
   sections/LOD/materials, morphs, sockets i diagnostics importu.
4. Animation Clip Editor pozwala wybrać kompatybilny preview mesh, odtwarzać,
   pauzować, krokować i scrubować dokładną pozę.
5. Przeciągnięcie klipu z Asset Browsera na state graph tworzy stan; Entry i
   default state są jawne.
6. Łączenie stanów tworzy transition; jego conditions są edytowane w Details.
7. Double-click stanu przechodzi do jego motion/pose graph, a breadcrumbs
   pozwalają wrócić.
8. Save automatycznie waliduje asset i buduje jego reprezentację runtime;
   diagnostyka wskazuje błędny węzeł, parametr albo niezgodny rig.
9. Preview Instance i żywa encja są wybieralnymi debug targets; aktywne stany,
   transitions, parameters, weights i elapsed time są widoczne bez modyfikacji
   assetu.
10. Po udanej walidacji Save atomowo zapisuje asset i automatycznie przeładowuje
    runtime. Błędna wersja nie zastępuje ostatniej poprawnej instancji. Dirty
    marker, undo/redo, copy/paste, rename i close prompt działają per dokument.

### Save, walidacja i automatyczny reload — bez jawnego Compile

Obecny `AnimatorController` 21kb jest assetem danych, a nie Blueprintem,
wygenerowaną klasą ani skryptem wymagającym ręcznej kompilacji. Docelowy flow
nie powinien mieć obowiązkowego przycisku `Compile`.

Właściwy kontrakt operacji Save:

1. edytor waliduje semantycznie working copy;
2. system automatycznie buduje zoptymalizowaną, wyprowadzoną reprezentację
   runtime; jeżeli przyszły AnimGraph wymaga kompilacji grafu, odbywa się ona
   w tym kroku w tle;
3. błędy są przypisane do konkretnego węzła, przejścia, parametru, klipu albo
   niezgodności riga;
4. dopiero po pełnym sukcesie asset jest zapisywany atomowo;
5. Asset Manager zwiększa generację i wykonuje automatyczny hot reload;
6. aktywne instancje przechodzą na nową reprezentację tylko wtedy, gdy jest
   poprawna i kompatybilna;
7. przy błędzie ostatnia poprawna wersja runtime pozostaje aktywna, a błędna
   working copy pozostaje w edytorze do naprawy.

Sam Save + Reload bez walidacji jest niedopuszczalny, ponieważ mógłby podmienić
działającą instancję na uszkodzony graf albo rig. „Build/compile” jest tutaj
wewnętrznym etapem przygotowania danych runtime, nie osobną czynnością autora.

### Krytyczny flow danych bez obejść

1. Wersjonowane `SkeletonAsset`, `SkeletalMeshAsset` i `AnimationClip` ze
   skeleton identity oraz stabilnym bone bindingiem.
2. Deterministyczny importer hierarchy/ref pose/inverse bind/JOINTS/WEIGHTS,
   sections, morphs i animation channels.
3. Komponent geometrii deformowanej oraz mały `Animator`; bez encji per kość.
4. Derived `AnimatorInstance`: prebinding indeksów, contiguous local pose,
   state/layer sampling, constraints na pose i jeden hierarchy solve.
5. Finalna paleta `componentPose * inverseBind`, current/previous double buffer
   oraz jawny root motion owner.
6. Renderer: palette allocator/upload, proxy handle, shader permutations dla
   base, GBuffer, depth, shadow, selection i motion vectors.
7. Animated bounds, LOD/required bones, morph curves, sockets oraz culling.
8. Wspólna preview scene używająca dokładnie tego samego runtime i renderera co
   gra.
9. Warstwowe testy importer/pose/palette/GPU/bounds/serialization/editor oraz
   benchmark wielu postaci.

## Obowiązkowy tryb testów — HEADLESS ONLY

Wszystkie testy tej implementacji muszą działać bez uruchamiania widocznego
okna edytora, przejmowania fokusu, pokazywania popupów lub otwierania
zewnętrznych programów. Praca użytkownika w innych aplikacjach nie może być
zakłócana.

Kanoniczna dokumentacja i implementacja runnera:

- `sources/editor/tests/HeadlessAutomation.md`;
- `sources/editor/tests/HeadlessAutomationScenario.json`;
- `sources/editor/src/app/main.cpp`;
- `sources/editor/src/app/EditorAutomationScenarioRunner.cpp`;
- `sources/editor/src/app/EditorHeadlessAutomation.cpp`;
- `sources/editor/CMakeLists.txt`;
- commit `21a9b3bd` — `Add production headless editor scenario runner`.

Preferowane uruchomienie wersjonowanego scenariusza repozytorium:

```powershell
ctest --test-dir build -C Debug -R "^kb_editor_headless_automation_scenario$" --output-on-failure
```

Przed testem buduj wyłącznie najmniejszy target obejmujący ostatnie zmiany i jego konieczne zależności. Nie uruchamiaj pełnego builda silnika po każdej edycji; pełny build jest dopuszczalny tylko, gdy jest technicznie konieczny i został uzasadniony.

Uruchomienie scenariusza zadaniowego na Windows musi jawnie czekać na proces
GUI-subsystem i dodatkowo wymuszać ukryty styl procesu:

```powershell
$editorPath = (Resolve-Path ".\\build\\editor\\Debug\\kb_editor.exe").Path
$scenarioPath = (Resolve-Path ".\\SelfTest\\SK-input\\scenario.json").Path
$selfTestRoot = (Resolve-Path ".\\SelfTest").Path
$arguments = @(
    "--selftest-scenario", $scenarioPath,
    "--selftest-root", $selfTestRoot,
    "--selftest-task", "SK-headless"
)
$process = Start-Process -FilePath $editorPath -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Get-Content -LiteralPath ".\\SelfTest\\SK-headless\\report.txt"
    throw "Headless editor scenario failed with exit code $($process.ExitCode)."
}
```

Kontrakt bezpieczeństwa:

1. `kb_editor.exe` wolno uruchamiać wyłącznie z `--selftest` albo
   `--selftest-scenario`; zwykłe uruchomienie edytora jest zakazane.
2. `main.cpp` kieruje self-test do runnera przed inicjalizacją normalnego
   `EditorApplication`, głównego okna i interaktywnej pętli edytora.
3. Headless runner może tworzyć wyłącznie niewidoczny, nieaktywowany host
   renderera potrzebny do produkcyjnego GPU readback; nie wolno wywoływać dla
   niego `ShowWindow` ani przejmować fokusu.
4. Obrazy paneli powstają offscreen przez memory DC, a runtime capture przez
   produkcyjny GPU readback. Nie wolno otwierać widocznego edytora w celu
   wykonania screenshotu.
5. Scenariusz musi używać produkcyjnych operacji runnera, Play Mode,
   serializacji i runtime. Osobny mock albo test-only implementacja nie jest
   dowodem integracji.
6. Każdy scenariusz zapisuje izolowany projekt i artefakty w
   `SelfTest/<task>/`: `report.txt`, `manifest.txt`, `trace.jsonl`, snapshoty
   oraz żądane BMP/PNG. `SelfTest/` pozostaje ignorowany przez Git.
7. Wynik ocenia się z exit code, `RESULT: PASS`, `assert_no_errors`, trace,
   snapshotów i capture zapisanych na dysku; ich inspekcja nie może uruchamiać
   zewnętrznego GUI.
8. Testy authoringu muszą wykonać Save, reload/reopen i ponowną asercję
   produkcyjnego stanu; sam screenshot nie wystarcza.
9. `tests/run-render-smoke.ps1`, zwykły `kb_editor.exe`, ręczne klikanie i
   widoczne testy okien są dla tego backlogu zakazane, chyba że użytkownik
   później jawnie zmieni tę decyzję.
10. Golden screenshots dla różnych rozdzielczości/DPI są generowane przez
    headless panel renderer do plików, nigdy przez pokazanie okna.

Historyczne prywatne artefakty potwierdzają ten flow między innymi w
`SelfTest/LIB-166-animator-runtime`, `LIB-167-animator-controls`,
`LIB-169-root-motion`, `LIB-170-blend-ik` i `LIB-172-animator-unload`:
scenariusz tworzy assety, wykonuje Save/reload, uruchamia produkcyjny Play
Mode, zapisuje capture/trace/snapshot i kończy `assert_no_errors`.

## Sprinty implementacyjne

Każdy checkbox oznacza kompletny, produkcyjny kontrakt i zawiera obowiązkowy
znacznik `SSOT · RUNTIME · ZERO-STUB · HEADLESS`. Znacznik jest częścią kryteriów odbioru
każdego zadania i zawsze oznacza łącznie:

- **SSOT:** istnieje dokładnie jedno autorytatywne źródło prawdy; zakazane są
  równoległe kopie stanu, rozbieżne modele edytora/runtime i cache udający
  dane autorskie;
- **RUNTIME:** wynik jest wpięty end-to-end w produkcyjny runtime, asset
  pipeline, serializację, edytor i testy; osobna ścieżka demonstracyjna,
  preview-only albo editor-only nie realizuje zadania;
- **ZERO-STUB:** zakazane są martwy kod, stuby, placeholdery, TODO,
  półimplementacje, osierocone API, ciche fallbacki i niedokończone ścieżki.
- **HEADLESS:** wszystkie testy, capture i scenariusze odbiorowe działają bez
  widocznego okna, aktywacji aplikacji i zakłócania pracy użytkownika.

Nie wolno oznaczać zadania jako ukończonego po dodaniu samego typu, UI albo
testu. Sprinty są zależnościowe i powinny być realizowane w podanej kolejności.

### Sprint SK-01 — kontrakty assetów i wersjonowanie

1. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zdefiniować kanoniczny `SkeletonAsset`: stabilne identyfikatory kości,
   parent indices, nazwy, lokalna reference pose, inverse bind matrices i
   sygnatura kompatybilności.
   Dowód 2026-08-02: `SkeletonAsset` jest ładowany przez produkcyjny registry/runtime; `kb_engine_tests.exe skeleton-assets` oraz headless `SK-001-skeleton-asset` przeszły.
2. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zdefiniować `SkeletalMeshAsset`: sekcje materiałowe, LOD-y, skin weights,
   bone maps, conservative bounds, morph targets i referencja szkieletu.
   Dowód 2026-08-02: kanoniczny asset `SkeletalMeshAsset` ładuje production registry/runtime; `kb_engine_tests.exe skeletal-mesh-assets` oraz headless `SK-002-skeletal-mesh-asset` przeszły.
3. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Rozszerzyć `AnimationClip` o docelowy szkielet, stabilne bone bindings,
   curves/morph channels oraz jawny kontrakt root motion.
   Dowód 2026-08-02: kanoniczny clip waliduje i serializuje target rig, stable bone IDs, morph/curve tracks i root motion; `kb_engine_tests.exe animation-runtime` oraz headless `SK-003-skeletal-animation-clip` przeszły.
4. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Wprowadzić magic, schema version i migracje formatów Skeleton,
   SkeletalMesh, AnimationClip i AnimatorController.
   Dowód 2026-08-02: schema 1 używa nagłówka `21kb <type> <version>`; legacy bez nagłówka migruje przy odczycie, a niezgodne nagłówki są odrzucane. Testy assetów oraz headless SK-001–004 przeszły.
5. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zapewnić deterministyczną serializację, walidację finite/range oraz
   jednoznaczne komunikaty błędów każdego assetu.
   Dowód 2026-08-02: serializer używa locale klasycznego i precyzji round-trip; loadery przekazują walidację albo rekord/linię błędu. `kb_engine_tests.exe skeleton-assets skeletal-mesh-assets animation-runtime` oraz headless SK-004 przeszły.
6. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zdefiniować reguły kompatybilności i reimportu bez wiązania klipów do
   tekstowych ścieżek encji.
   Dowód 2026-08-02: AssetManager waliduje deklarowane Skeleton dependencies i ich sygnatury; test silnika pokrywa reimport niezgodnego riga, a headless SK-006 wykrywa niezwiązaną referencję bez ścieżek encji.
7. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać round-trip, migration, corruption i deterministic-byte tests.
   Dowód 2026-08-02: testy Skeleton, SkeletalMesh, AnimationClip i AnimatorController pokrywają identyczne bajty, legacy migration i odrzucenie schema corruption; `kb_engine_tests.exe skeleton-assets skeletal-mesh-assets animation-runtime` oraz headless SK-004 przeszły.

### Sprint SK-02 — produkcyjny importer skeletal

8. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimportować z glTF hierarchię kości, lokalną reference pose, inverse
   bind matrices, `JOINTS_0` i `WEIGHTS_0`.
   Dowód 2026-08-02: produkcyjny `SkeletalMeshGltfImporter` importuje glTF skin do kanonicznych Skeleton/SkeletalMesh assetów; `kb_engine_tests.exe skeletal-mesh-assets` oraz headless SK-008 przeszły.
9. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Walidować indeksy kości, skończoność i nieujemność wag, deterministycznie
   normalizować/przycinać wpływy oraz odrzucać zerowe wiązania.
   Dowód 2026-08-02: importer łączy JOINTS/WEIGHTS 0/1, deterministycznie wybiera cztery wpływy i normalizuje; test silnika odrzuca zero binding. `kb_engine_tests.exe skeletal-mesh-assets` oraz headless SK-008 przeszły.
10. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimportować animation channels, morph targets, sekcje materiałowe i
    jawnie konwertować osie, handedness oraz jednostki.
    Dowód 2026-08-02: importer importuje liniowe kanały bone i weights-morph, morph deltas oraz sekcje przez jawny resolver Material assetów; domyślnie konwertuje glTF RH Y-up w metrach do silnika LH Y-up, z konfiguracją permutacji osi i skali jednostek. `kb_engine_tests.exe skeletal-mesh-assets` oraz headless SK-010 (9/9) przeszły.
11. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zbudować reguły tworzenia lub ponownego użycia kompatybilnego
    `SkeletonAsset` podczas importu i reimportu.
    Dowód 2026-08-02: czysty `SkeletalMeshGltfImportPlanner` deterministycznie reużywa Skeleton o kanonicznej sygnaturze albo planuje nową ścieżkę/ID, bez publikacji plików; test silnika obejmuje oba warianty, a headless SK-011 przeszedł.
12. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zapewnić atomowy import/reimport: błąd nie może uszkodzić ostatnich
    poprawnych assetów ani ich referencji.
    Dowód 2026-08-02: publikator etapuje Skeleton/Mesh/Clip, zachowuje backupy i odtwarza je przy błędzie, a registry odświeża dopiero po publikacji; test silnika sprawdza zachowanie ostatniej poprawnej siatki przy błędnym reimporcie, a headless SK-012 przeszedł.
13. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać raport importu z ostrzeżeniami i błędami prowadzącymi do
    konkretnej siatki, węzła, kości albo kanału.
    Dowód 2026-08-02: importer zwraca strukturalny raport Warning/Error ze źródłem i kontekstem mesh/node/bone/primitive/channel; test silnika weryfikuje ostrzeżenia defaultowanych atrybutów i błąd zerowego wiązania, a headless SK-013 przeszedł.
14. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać golden glTF fixtures oraz testy błędnych wag, macierzy, cykli,
    brakujących kości, wielu skinów i reimportu.
    Dowód 2026-08-02: deterministyczne fixture’y glTF testów silnika pokrywają zero-weight, NaN inverse bind, cykl jointów, brakujący joint, wiele skinów i stabilne ID reimportu; headless SK-014 przeszedł.
15. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dopiero po pełnej obsłudze zastąpić testy wymagające odrzucania skinned
    glTF testami poprawnego importu.

### Sprint SK-03 — komponenty sceny i trwałość

16. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować komponent wiązania szkieletu zgodny z zadaniem 42 z
    `Components.md`, przechowujący wyłącznie autorytatywną konfigurację.
17. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować komponent geometrii odkształcanej zgodny z zadaniem 43:
    asset siatki, materiały, LOD/bounds flags i źródło pozy.
18. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zachować mały komponent `Animator` jako referencję controllera, speed,
    enabled i jawnego właściciela root motion.
19. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować Add Component, Inspector, remove, duplicate, prefab
    overrides, undo/redo i typowane asset pickery obu nowych komponentów.
    Dowód lokalny (2026-08-02): `kb_engine_tests skeletal-mesh-assets` PASS;
    `SK-019-skeletal-component-authoring` PASS (53 kroki, save/reload/Play Mode,
    manifest i zrzuty w `SelfTest/SK-019-skeletal-component-authoring/`).
20. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać wersjonowaną serializację scen/prefabów i walidację referencji
    Skeleton ↔ SkeletalMesh ↔ Clip ↔ Controller.
    Dowód lokalny (2026-08-02): `kb_engine_tests skeletal-mesh-assets` oraz
    `kb_engine_tests animation-runtime` PASS; headless
    `SK-020-pose-source-serialization` PASS (28 kroków, scena i prefab
    round-trip, reload oraz Play Mode).
21. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zapewnić hot reload/reimport bez utraty stabilnych referencji i bez
    cichego przejścia do bind pose.
22. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać testy headless pełnego Add Component i round-trip scen/prefabów.

### Sprint SK-04 — compact pose i AnimatorInstance

23. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zbudować derived `AnimatorInstance` z prebindingiem stabilnych bone IDs
    do ciągłych indeksów kości.
24. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Wprowadzić contiguous/SoA local pose, component-space pose oraz
    current/previous double buffer bez encji sceny per kość.
25. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Przenieść skeletal sampling z `SceneEntity/Transform` na indeksowane
    tracki i pose buffers.
26. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować state/layer/transition/mask/blend evaluation na compact
    pose, zachowując deterministyczne zachowanie obecnego controllera.
27. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Wyliczać dokładnie raz na ewaluację local-to-component hierarchy solve
    oraz `skinMatrix = componentPose * inverseBind`.
28. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Wydzielić root motion przed finalnym pose solve i przekazywać go tylko
    do jednego jawnego właściciela Transform/CharacterController/Rigidbody.
29. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Przenieść TwoBoneIK, Aim i CopyTransform na pose buffer; constraints nie
      mogą synchronizować całej sceny per kość.
      Dowód lokalny (2026-08-03): stabilne bone IDs są serializowane w
      AnimatorController schema 2 z migracją schema 1; TwoBoneIK, Aim i
      CopyTransform działają na compact pose i aktualizują wyłącznie dotknięte
      poddrzewa/palety bez encji per kość oraz bez synchronizacji sceny.
      `kb_engine_tests.exe animation-runtime` PASS.
30. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać deterministic sampling, known-pose, blending, transition, root
    motion, IK i hot-reload tests dla prawdziwego szkieletu.

### Sprint SK-05 — GPU skinning i wszystkie passy

31. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Włączyć rejestrację skinned vertex/index resources z walidacją layoutu,
    zakresu joint indices i limitów backendu.
32. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować palette allocator, upload ring/double buffering,
    lifetime, fences i jawne zachowanie po przekroczeniu limitów.
33. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać palette handle i current/previous pose do render proxy oraz draw
    command bez serializowania uchwytów GPU w komponencie.
34. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować shaderową deformację pozycji, normalnej i tangenta.
35. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać skinned permutations do base/forward, GBuffer, depth, shadow,
    selection oraz motion-vector passów.
36. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zapewnić poprawne materiały `PreSkinnedPosition/Normal` dla deformowanej
    geometrii.
37. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować batching świadomy siatki, materiału i palety bez
    niekontrolowanych alokacji per draw.
38. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać GPU readback/pixel tests deformacji dla wszystkich wymaganych
    passów oraz lifetime/reload tests.

### Sprint SK-06 — bounds, LOD, morphs i attachment points

39. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować conservative imported bounds i jawny fixed-bounds mode.
40. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać aktualizowane animated bounds przez per-bone/per-clip bounds albo
    równoważną mierzalną metodę.
41. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Włączyć deformowane bounds do visibility, frustum/occlusion culling i
    shadow culling.
42. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować LOD bone maps, required bones oraz bezpieczne
    przełączanie LOD bez niezgodności palety.
43. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować morph weights/curves i połączyć je z klipem,
    AnimatorInstance oraz rendererem.
44. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować socket/attachment queries wyprowadzane z component-space
    pose bez tworzenia stałej encji dla każdej kości.
45. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać testy animated culling, LOD switching, morph deformation i socket
    transforms.

### Sprint SK-07 — wspólna scena podglądowa

46. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zbudować jeden Persona-like `AnimationPreviewContext` współdzielony
    przez wszystkie edytory animacyjne.
47. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Preview musi używać dokładnie produkcyjnego AnimatorInstance, GPU
    skinningu, bounds i materiałów z runtime gry.
48. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać kompatybilny preview Skeleton/SkeletalMesh/Clip/Controller,
    reference pose i animated pose.
49. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować orbit, pan, zoom, focus, kamerę, podłogę, światło,
    environment i stabilną ekspozycję.
50. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać play, pause, loop, step, speed i deterministyczny scrub wspólnego
    playheada.
51. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać overlay kości, nazw, socketów, root motion, bounds, LOD i normals.
52. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zapewnić bezpieczny lifecycle native child surface podczas resize,
    docking, floating, DPI, minimalizacji i Alt-Tab.
53. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać testy preview state, dokładnej pozy po scrubie i lifecycle hosta.

### Sprint SK-08 — Skeletal Mesh Editor

54. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać typed asset dispatch i dedykowany workspace
    `Skeletal Mesh Editor`; ponowne otwarcie fokusuje istniejący dokument.
55. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Odtworzyć domyślny flow UE5 `20/60/20`: toolbox, centralny viewport oraz
    prawa kolumna Skeleton Tree/asset details.
56. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować Skeleton Tree z wyszukiwaniem, hierarchią, bone/socket
    selection i dwukierunkowym viewport picking.
57. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować Details dla assetu, kości, socketu, sekcji materiałowej,
    LOD, bounds i import settings.
58. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać panele Morph Targets, curves i Advanced Preview.
59. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać reference pose, LOD/material visualization, bone weights oraz
    diagnostics importu/reimportu.
60. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować working copy, dirty marker, Save, Revert, atomic
    reimport, per-document undo/redo i close prompt.
61. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać headless authoring tests i golden screenshots layoutu.

### Sprint SK-09 — Animation Clip Editor

62. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać typed asset dispatch i dedykowany `Animation Clip Editor` ze
    wspólnym preview context.
63. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować timeline/outliner: bone tracks, curves, morph channels,
    events/notifies i root motion.
64. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać transport, exact scrub, frame/time display, zoom, pan, snapping,
    loop range i step forward/back.
65. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zsynchronizować timeline selection ↔ Skeleton Tree ↔ viewport ↔ Details.
66. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować operacje kluczy i eventów z grupowanym undo/redo,
    walidacją czasu oraz deterministycznym sortowaniem.
67. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać wybór wyłącznie kompatybilnego preview mesh/skeleton i czytelną
    diagnostykę niezgodności.
68. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Save ma automatycznie walidować, budować runtime clip i hot reloadować
    go dopiero po atomowym sukcesie.
69. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać headless tests scrub/evaluation/edit/save/reopen oraz golden
    screenshots timeline.

### Sprint SK-10 — Animator/Controller Editor

70. [x] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać typed asset dispatch i dedykowany `Animator Editor`; usunąć
    kierowanie controllera do Script Editora.
71. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Odtworzyć domyślny flow UE5 `25/55/20`: preview, centralny dokument
    graph/state machine oraz Details/Asset Browser.
72. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować Parameters, Layers, Entry/default state, state nodes,
    transitions, conditions i transition Details.
73. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać stabilne node/edge IDs, serializowany graph layout, comments,
    groups, copy/paste, rename, delete i multi-selection.
74. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować drag Animation Clip z kompatybilnego Asset Browsera na
    graph oraz automatyczne utworzenie stanu.
75. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować double-click state, motion/blend document oraz
    breadcrumbs/back navigation.
76. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Rozszerzyć 1D blend tree i pose/motion graph tylko o węzły posiadające
    produkcyjny runtime contract; bez UI-only node'ów.
77. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zaimplementować working copy, per-document undo/redo, dirty marker,
    Save/Revert i close prompt.
78. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Save automatycznie waliduje i buduje reprezentację runtime bez jawnego
    przycisku Compile; błąd wskazuje dokładny node/transition/parameter.
79. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Atomowy hot reload aktywuje nowy controller wyłącznie po sukcesie;
    ostatnia poprawna instancja pozostaje aktywna przy błędzie.
80. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zastąpić testy nazywające Script Editor „animation-controller-editor”
    prawdziwymi testami workspace, authoringu i serializacji grafu.

### Sprint SK-11 — live debug i diagnostyka

81. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać wybór Preview Instance albo żywej encji jako debug targetu.
82. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Pokazywać live parameters, triggers, aktywne stany, previous state,
    transition progress, elapsed time i wagi warstw.
83. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Podświetlać aktywne state/transition nodes bez zapisywania debug state do
    assetu.
84. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać pose/bone diagnostics, final pose, constraint targets, root motion
    trail, palette/bounds/LOD status i błędy kompatybilności.
85. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zapewnić thread-safe snapshot debug bez blokowania animation hot path.
86. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać testy przełączania debug targetu, odpinania encji, reloadu i
    zgodności UI z runtime snapshotem.

### Sprint SK-12 — wydajność, stabilność i odbiór produktu

87. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać benchmark 100 i 1000 rigów po 100–200 kości dla sampling, blend,
    constraints, hierarchy solve, palette upload i draw submission.
88. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zmierzyć alokacje, cache misses, czas worker/main/render thread,
    batching oraz koszty LOD/update-rate.
89. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Wprowadzić równoległą ewaluację i update-rate optimization dopiero na
    podstawie pomiarów, zachowując deterministyczny wynik.
90. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać stress tests reload/reimport, asset eviction, scene unload,
    renderer reset i zniszczenie encji w trakcie debugowania.
91. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Dodać screenshot tests 1920×1080, 1366×768 i 150% DPI dla wszystkich
    trzech edytorów, docked i floating.
92. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Zweryfikować brak overlayów nad inną aplikacją po Alt-Tab, minimalizacji
    i deaktywacji oraz brak wycieku viewportu podczas resize/move/DPI.
93. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Uruchomić pełny build i test suite, usunąć regresje oraz przejrzeć diff
    bez cichych fallbacków i osieroconego API.
94. [ ] **[SSOT · RUNTIME · ZERO-STUB · HEADLESS]** Oznaczyć zadania 42, 43 i 45 w `Components.md` jako ukończone dopiero po
    spełnieniu ich pełnych kryteriów runtime, edytora, serializacji i testów.

### Kryteria akceptacji wyglądu i lifecycle

- wspólne tokeny stylu dla graph, timeline, tree, details, transportu i stanów
  interakcji; brak rozproszonych lokalnych kolorów i metryk;
- layouty Skeletal Mesh `20/60/20` i Animator `25/55/20` przy pierwszym
  otwarciu, z resizable/persisted splitters;
- dwukierunkowa selekcja Skeleton Tree ↔ viewport ↔ Details oraz graph ↔ Details;
- orbit/pan/zoom/focus, bone/socket/root-motion/LOD overlays i ref/animated pose;
- brak jakiegokolwiek elementu pozostającego nad inną aplikacją po Alt-Tab,
  minimalizacji lub deaktywacji;
- brak wycieku native child surface poza aktualny dock podczas resize, move,
  zmiany DPI i przenoszenia na inny monitor;
- popupy są częścią lifecycle hosta i chowają się przy `WM_ACTIVATEAPP`/utracie
  właściciela; nie wolno mnożyć `WS_POPUP` ustawianych niezależnie na wierzchu;
- screenshot/golden tests dla 1920×1080, 1366×768 i 150% DPI oraz wariantów
  docked/floating.

### Wynik uruchomionej weryfikacji

- `kb_engine_tests.exe animation-runtime`: **PASS**;
- `kb_editor_tests.exe`: **PASS** (`EditorMaterialGraphCanvasTests passed`);
- pełny `kb_engine_tests.exe`: **FAIL** na dwóch istniejących, niezwiązanych z
  tym audytem problemach: `LIB-134 determinism rigs diverged` oraz brakujący
  komponent w fixture wygenerowanych accessorów Script API;
- nie istnieją testy produktu ani benchmarki dla Skeletal Mesh, ponieważ ta
  funkcja nie jest jeszcze zaimplementowana.

Audyt nie zmieniał kodu produktu. Jedyną zmianą jest rozszerzenie tej prywatnej,
ignorowanej przez Git notatki.
