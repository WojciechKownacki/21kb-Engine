# Components — liniowy backlog ról obiektowych Engine21kb

**Status:** tajny dokument lokalny. Nie jest częścią produktu, publicznego API
ani materiałów przeznaczonych do publikacji.

## Werdykt po doprecyzowaniu celu

Poprzednia wersja odpowiadała na inne pytanie: które komponenty są konieczne do
uruchomienia samego kernela. Odpowiedź „zero” była poprawna dla kernela, ale nie
dla katalogu edytora silnika do tworzenia gier. Ten dokument opisuje to, co
autor gry może dodać do obiektu, aby nadać mu standardową funkcję silnikową.

Nie istnieje jedna literalna lista nazw i typów wspólna dla wszystkich
produktów. Ta sama zdolność bywa komponentem, węzłem, obiektem specjalnym,
zasobem albo ustawieniem świata. Przyjęty tutaj wynik to kompletna unia
powtarzalnych **ról dołączanych do obiektów** z dojrzałych, ogólnego
przeznaczenia silników 2D/3D, po:

1. scaleniu typów różniących się wyłącznie wariantem danych;
2. rozdzieleniu ścieżek 2D i 3D tam, gdzie mają inny układ danych lub hot path;
3. usunięciu assetów, usług, cache'y i metadanych udających komponenty;
4. usunięciu komponentów specyficznych dla gatunku albo konkretnej gry;
5. zastąpieniu cudzych nazw własnym słownikiem i identyfikatorem projektu.

W tym znaczeniu audyt bazowy wykazał **207 ról katalogowych**. Aktywny backlog
zawiera **205 nieukończonych ról** po usunięciu dwóch pozycji spełniających cały
kontrakt. „Obowiązkowy” oznacza: rola nie może zostać pominięta z katalogu
kompletnego silnika, jeżeli silnik deklaruje obsługę odpowiadającego jej modułu.
Nie oznacza to, że każdy build zawiera każdy moduł ani że każdy obiekt ma każdy
komponent. Pozycje klasy `N` są częścią katalogu edytora, ale nie są obowiązkowe
w runtime ani w wydaniu.

## Granica komponentu

Pozycja trafia do listy tylko wtedy, gdy ma autorytatywny stan instancji, który
można dołączyć do konkretnego obiektu lub elementu UI. Konfigurację, wartości
autorskie i stan wymagany przez zapis gry można serializować i edytować.
Obliczony stan klatki, wynik zapytania i dane urządzenia pozostają w systemie
albo cache'u, nawet gdy edytor pokazuje je obok komponentu. Komponent nie jest
kontenerem na wszystko, co dotyczy danej funkcji.

## Obowiązek pełnego podpięcia komponentu

Każdy wpis backlogu jest funkcją, którą autor gry może dołączyć do konkretnego
obiektu przez rzeczywisty workflow Add Component edytora. Jeżeli komponent lub
część jego kontraktu nie istnieje, należy najpierw produkcyjnie utworzyć jego
kanoniczne dane oraz niezbędne API, rejestrację, zapis, odczyt, migrację,
system runtime, backend albo narzędzie offline. Ten sam autorytatywny stan musi
następnie zostać udostępniony w Inspectorze do konfiguracji, usunięcia,
duplikacji i undo/redo. Sama definicja typu, przechowywanie danych, test
serializacji albo sama pozycja UI nie realizują wpisu. Zakończenie wymaga
działającej ścieżki produkcyjnej oraz wszystkich wymaganych testów, w tym
headlessowego scenariusza edytora.

## Mapowanie technicznych typów implementacyjnych

Poniższe nazwy wskazują jeden kanoniczny typ komponentu dla implementacji,
nie etykietę widoczną dla autora.
Wpisy już obecne w repozytorium używają dokładnej nazwy deklarowanego typu C++;
pozostałe są neutralnymi, wewnętrznymi nazwami docelowymi, a nie kopiami API
innego silnika. Każda otwarta pozycja ma obecnie odrębną odpowiedzialność i
dlatego nie ma powtórzeń wymagających odwołania „Zadanie dotyczy X”.

### Nazwy Add Component i Inspectora

Autor gry nie może widzieć nazwy technicznego typu, identyfikatora serializacji,
przestrzeni nazw ani nazwy backendu. Pogrubiona nazwa każdej pozycji jest źródłem
etykiety autora (z dopuszczalną lokalizacją). Add Component, wyszukiwarka,
nagłówek Inspectora, undo/redo i test headless używają tej samej przyjaznej nazwy
produktowej. Nazwa techniczna z tabeli służy wyłącznie kodowi.
Sufiks `Component` oraz techniczne segmenty wymiaru nie są częścią etykiety
autora, o ile nie zostały jawnie zatwierdzone jako nazwa produktowa.

| Zadanie | Komponent |
| --- | --- |
| 1 | TagsComponent |
| 2 | VisibilityComponent |
| 3 | SceneRegionFormComponent |
| 4 | SceneGuideCurveComponent |
| 5 | SceneContentInstanceComponent |
| 6 | SceneStreamFocusComponent |
| 7 | CameraComponent |
| 8 | MeshRendererComponent |
| 9 | WorldBackdropComponent |
| 10 | LightComponent |
| 11 | LightAmbientRadianceComponent |
| 12 | SceneDetailSwitchComponent |
| 13 | SceneVisibilityBlockerComponent |
| 14 | SceneVisibilityCellComponent |
| 15 | SceneRegionPortalComponent |
| 16 | ViewAuxFrameComponent |
| 17 | DrawD3GeometrySwarmComponent |
| 18 | DrawD3SurfaceCastComponent |
| 19 | DrawD3FacingPanelComponent |
| 20 | DrawD3SpaceStrokeComponent |
| 21 | DrawD3HistoryRibbonComponent |
| 22 | DrawD3LensEchoComponent |
| 23 | DrawD3WorldGlyphComponent |
| 24 | LightReflectionSampleComponent |
| 25 | LightBounceLightFieldComponent |
| 26 | ViewImageGradeZoneComponent |
| 27 | WorldFogMediumZoneComponent |
| 28 | LightBakePriorityZoneComponent |
| 29 | WorldAirShellComponent |
| 30 | WorldCloudLayerComponent |
| 31 | DrawD3DensityVolumeComponent |
| 32 | SceneD2OrderGroupComponent |
| 33 | DrawD2ImageComponent |
| 34 | DrawD2ShapeComponent |
| 35 | DrawD2GlyphComponent |
| 36 | DrawD2MaskComponent |
| 37 | DrawD2ParallaxSheetComponent |
| 38 | DrawD2SwarmComponent |
| 39 | DrawD2TileFieldComponent |
| 40 | LightD2RadianceEmitterComponent |
| 41 | LightD2ShadowFormComponent |
| 42 | MotionSkeletonBindingComponent |
| 43 | DrawD3DeformedGeometryComponent |
| 44 | DrawD2DeformerComponent |
| 45 | MotionSkeletonRuleComponent |
| 46 | SceneFrameRuleComponent |
| 47 | MotionDeformCachePlayerComponent |
| 48 | RigidbodyComponent |
| 49 | ColliderComponent |
| 50 | PhysicsD3CastProbeComponent |
| 51 | PhysicsD3DynamicsActuatorComponent |
| 52 | JointComponent |
| 53 | CharacterControllerComponent |
| 54 | PhysicsD3InfluenceFieldComponent |
| 55 | PhysicsD3WheelContactComponent |
| 56 | PhysicsD3VehicleAssemblyComponent |
| 57 | PhysicsD3JointedChainNodeComponent |
| 58 | PhysicsD3FractureResponseComponent |
| 59 | PhysicsD3BoneDynamicsComponent |
| 60 | PhysicsD3SoftSheetComponent |
| 61 | PhysicsD3FlexStrandComponent |
| 62 | PhysicsD3SoftVolumeComponent |
| 63 | ViewMotionRigComponent |
| 64 | PhysicsD2KineticFrameComponent |
| 65 | PhysicsD2ContactShellComponent |
| 66 | PhysicsD2CastProbeComponent |
| 67 | PhysicsD2DynamicsActuatorComponent |
| 68 | PhysicsD2MotionBondComponent |
| 69 | PhysicsD2AvatarDriveComponent |
| 70 | PhysicsD2InfluenceFieldComponent |
| 71 | FxEffectInstanceComponent |
| 72 | FxEffectForceZoneComponent |
| 73 | FxEffectContactZoneComponent |
| 74 | WorldAirflowZoneComponent |
| 75 | WorldGroundFieldComponent |
| 76 | WorldGroundStampComponent |
| 77 | WorldVoxelGroundComponent |
| 78 | WorldBiomeScatterComponent |
| 79 | WorldBiomeInfluenceComponent |
| 80 | DrawD3SurfaceFibersComponent |
| 81 | WorldFlowReachComponent |
| 82 | WorldFlowCutoutComponent |
| 83 | WorldFlowGridComponent |
| 84 | WorldFloatCouplingComponent |
| 85 | WorldWakeStampComponent |
| 86 | WorldWeatherZoneComponent |
| 87 | BehaviourComponent |
| 88 | LogicEventRouteComponent |
| 89 | LogicModeWeaveComponent |
| 90 | LogicLifetimePolicyComponent |
| 91 | InputComponent |
| 92 | InputActionSinkComponent |
| 93 | InputHapticRouteComponent |
| 94 | AudioListenerComponent |
| 95 | AudioSourceComponent |
| 96 | AudioSurfaceSkinComponent |
| 97 | AudioEnvironmentZoneComponent |
| 98 | AudioAcousticGateComponent |
| 99 | AudioAcousticProbeComponent |
| 100 | AudioSignalIntakeComponent |
| 101 | RouteBakeSourceComponent |
| 102 | RouteD3BakeBoundsComponent |
| 103 | RouteD3RuleZoneComponent |
| 104 | RouteD3SpanComponent |
| 105 | RouteD3WalkFieldComponent |
| 106 | NavObstacle |
| 107 | NavAgent |
| 108 | RouteD2BakeBoundsComponent |
| 109 | RouteD2RuleZoneComponent |
| 110 | RouteD2SpanComponent |
| 111 | RouteD2WalkFieldComponent |
| 112 | RouteD2BlockerComponent |
| 113 | RouteD2SeekerComponent |
| 114 | RouteCrowdMemberComponent |
| 115 | LogicMotionDirectiveComponent |
| 116 | AiDecisionMemoryComponent |
| 117 | AiSenseEmitterComponent |
| 118 | AiSenseReceiverComponent |
| 119 | AiDecisionRunnerComponent |
| 120 | AiSteeringPolicyComponent |
| 121 | UIDocumentComponent |
| 122 | UiPlacementComponent |
| 123 | UiCellComponent |
| 124 | UiFlowComponent |
| 125 | UiAspectGuardComponent |
| 126 | UiPaintComponent |
| 127 | UiGlyphComponent |
| 128 | UiClipComponent |
| 129 | UiGroupEffectComponent |
| 130 | InputPointerTargetComponent |
| 131 | InputGestureSinkComponent |
| 132 | UiFocusTargetComponent |
| 133 | InputFocusScopeComponent |
| 134 | UiAccessComponent |
| 135 | UiDataLinkComponent |
| 136 | UiStateMotionComponent |
| 137 | UiWorldBridgeComponent |
| 138 | UiPressComponent |
| 139 | UiChoiceLatchComponent |
| 140 | UiChoiceGroupComponent |
| 141 | UiValueRailComponent |
| 142 | UiMeterComponent |
| 143 | UiWritingFieldComponent |
| 144 | UiKeyCaptureComponent |
| 145 | UiNumberEditComponent |
| 146 | UiScrollAreaComponent |
| 147 | UiRepeatComponent |
| 148 | UiVirtualCollectionComponent |
| 149 | UiDragSourceComponent |
| 150 | UiDropTargetComponent |
| 151 | UiAnchoredFloatComponent |
| 152 | UiTooltipComponent |
| 153 | UiSelectComponent |
| 154 | UiBranchBrowserComponent |
| 155 | UiPageSwitchComponent |
| 156 | UiBusyMarkComponent |
| 157 | InputScreenActionComponent |
| 158 | AuthorDebugShapeComponent |
| 159 | AuthorVisualAidComponent |
| 160 | AuthorSpawnMarkComponent |
| 161 | AuthorBlockoutShapeComponent |
| 162 | AuthorBakePolicyComponent |
| 163 | MotionPropertyWeaveComponent |
| 164 | SequenceBindingComponent |
| 165 | SequenceSignalSinkComponent |
| 166 | SequenceRunnerComponent |
| 167 | LocaleScopeComponent |
| 168 | LocaleTextComponent |
| 169 | LocalePropertyBindingComponent |
| 170 | LocaleVariantBindingComponent |
| 171 | MediaFeedPlayerComponent |
| 172 | MediaFrameTargetComponent |
| 173 | MediaSoundTapComponent |
| 174 | MediaSubtitleComponent |
| 175 | MediaSyncGroupComponent |
| 176 | NetPresenceComponent |
| 177 | NetStateRouteComponent |
| 178 | NetEventRouteComponent |
| 179 | NetCommandRouteComponent |
| 180 | NetInterestRuleComponent |
| 181 | NetSpawnGateComponent |
| 182 | NetMotionRouteComponent |
| 183 | NetBodyRouteComponent |
| 184 | NetPoseRouteComponent |
| 185 | ImmersiveOriginComponent |
| 186 | ImmersiveDevicePoseFeedComponent |
| 187 | ImmersiveWorldPinComponent |
| 188 | ImmersiveHandFeedComponent |
| 189 | ImmersiveBodyFeedComponent |
| 190 | ImmersiveEyeFeedComponent |
| 191 | ImmersiveFaceScanComponent |
| 192 | ImmersivePointCloudScanComponent |
| 193 | ImmersivePlaneScanComponent |
| 194 | ImmersiveRoomMeshScanComponent |
| 195 | ImmersiveMarkerScanComponent |
| 196 | ImmersiveKnownFormScanComponent |
| 197 | ImmersiveAmbientScanComponent |
| 198 | ImmersiveBackgroundBlendComponent |
| 199 | ImmersiveCompositorPanelComponent |
| 200 | ImmersiveDepthOcclusionComponent |
| 201 | ImmersiveActionTargetComponent |
| 202 | ImmersiveInteractorComponent |
| 203 | ImmersiveTravelAreaComponent |
| 204 | ImmersiveBodyGuardComponent |
| 205 | ImmersiveMoveDriverComponent |

Ta lista jest skończona, ponieważ obejmuje wyłącznie wielokrotnego użytku
funkcje dostarczane przez silnik. Zdrowie, ekwipunek, broń, zadanie, drużyna,
dialog, ekonomia, zdolność postaci i podobne pojęcia należą do gry. Ich możliwa
liczba nie ma górnej granicy i nie są częścią tego katalogu.

## Własne nazewnictwo

Każda pozycja ma roboczy identyfikator serializacji zaczynający się od
`kb21.`. To identyfikator projektu, a nie alias ani tłumaczenie publicznego typu
z innego produktu. Pogrubiona polska nazwa jest etykietą opisową w tym
dokumencie, nie publicznym symbolem C++. Segmenty `d2` i `d3` zawsze oznaczają
odpowiednio odrębny kontrakt 2D albo 3D; rola wspólna dla obu profili nie ma
segmentu wymiaru.

Zasady przed publikacją:

1. zachować własny prefiks, słownik oraz układ modułów;
2. nie dodawać aliasów zgodności z cudzymi typami;
3. nie kopiować cudzego drzewa klas ani podziału jeden do jednego;
4. ponownie sprawdzić dokładne i łudząco podobne nazwy w kodzie, pakietach,
   produktach i rejestrach znaków;
5. wykonać niezależny przegląd prawny — ten audyt nie jest opinią prawną.

Generyczne terminy matematyczne i branżowe mogą pozostać nazwami pól, gdy są
potrzebne do jednoznacznego kontraktu. Kod, komentarze, testy, assety, ikony,
układ interfejsu i materiały zewnętrzne nie mogą być kopiowane.

## Klasy katalogu i stan obecny

Pierwsza litera przy pozycji określa zakres docelowy:

| Kod | Znaczenie |
| --- | --- |
| `P` | powszechna podstawa odpowiedniego profilu 2D, 3D albo UI |
| `M` | element potrzebny do kompletnego, dojrzałego modułu |
| `S` | funkcja specjalistyczna, włączana tylko w wymagającym jej produkcie |
| `N` | komponent wyłącznie narzędziowy lub autorski |

Druga litera określa audyt bieżącego repozytorium:

| Kod | Znaczenie |
| --- | --- |
| `J` | zarejestrowana rola obiektowa o zasadniczo zgodnej odpowiedzialności |
| `C` | istnieje powiązany asset, usługa albo część funkcji, lecz nie pełny docelowy komponent |
| `B` | brak zarejestrowanej roli oraz konkretnej, właściwej dla niej części funkcji |

Bieżący rejestr sceny zawiera 18 typów. Pełny audyt bazowy oznaczył 18 ról jako
`J`, 56 jako `C` i 133 jako `B`. Po usunięciu dwóch ukończonych ról aktywny
backlog zawiera 16 pozycji `J`, 56 pozycji `C` i 133 pozycje `B`. Nie jest to
odwzorowanie jeden do jednego: jedna obecna rola może mieć węższy kontrakt niż
docelowy wpis. `J` nie jest oceną jakości, kompletności implementacji,
wydajności ani gotowości produkcyjnej.

Pozycja otrzymuje `[X]` dopiero wtedy, gdy audyt potwierdzi cały opisany
kontrakt: schemat i rejestrację, trwałą serializację, wykonanie w runtime,
workflow authoringu w edytorze oraz testy. Samo istnienie klasy `J` nie
wystarcza. Podczas aktywnego celu ukończone zadania pozostają na liście jako
lokalny ślad wykonania; ich usunięcie jest osobną operacją porządkową.

## Reguły scalania wariantów

Wariant nie staje się osobnym komponentem, jeśli ma tę samą własność, czas
życia, kolejność aktualizacji i sposób serializacji. Edytor pokazuje warianty
w selektorze jednego komponentu.

### Ujęcia

`kb21.view.frame` zawiera projekcję perspektywiczną, równoległą,
asymetryczną/niestandardową i tryb optyki fizycznej. `kb21.view.motion-rig`
jest jedynym własnym komponentem sterowania ujęciem. Jego asset może składać
moduły: podążanie, celowanie, orbitę, widok pierwszoosobowy, widok znad
ramienia, prowadnicę, kadrowanie grupy, tłumienie, omijanie przeszkód, szum,
impuls, wstrząs, cięcie i mieszane przejście. Nie powstaje osobny komponent dla
każdego zachowania. `kb21.scene.region-form` dostarcza współdzieloną, zimną
geometrię stref: prostokąt, koło, wielokąt lub łańcuch 2D oraz pudełko, kulę,
kapsułę, walec, graniastosłup, siatkę albo złożenie 3D. Dane wielokąta, łańcucha
i siatki są typowaną referencją assetu, nie buforem geometrii w komponencie.
Kształt jest wariantem, nie osobnym komponentem. Każda lokalna strefa z listy wskazuje tę formę zamiast
utrzymywać drugą kopię geometrii; wyspecjalizowany komponent może wskazać
zewnętrzne pole wysokości, głębi, odległości lub geometrię sceny.

### Oświetlenie

| Komponent | Pełny zestaw wariantów |
| --- | --- |
| `kb21.light.d3.radiance-emitter` | równoległa wiązka z nieskończonej odległości, punkt, powierzchnia kulista, stożek, dysk kierunkowy, prostokąt, odcinek/rura/kapsuła, powierzchnia wielokątna |
| `kb21.light.ambient-radiance` | stały kolor, półsfera/gradient, mapa otoczenia, procedura radiancji, przechwyt sceny lub estymacja oświetlenia otoczenia |
| `kb21.light.d2.radiance-emitter` | globalne 2D, radialne, stożkowe, swobodny wielokąt/splajn, maska obrazowa |

Tryb ruchomy, nieruchomy albo wypiekany, barwa, temperatura, jednostka
fotometryczna, moc, zasięg, rozmiar źródła, cień, maski kanałów, profil
fotometryczny, maska projekcyjna, udział w mgle i świetle pośrednim są polami,
nie kolejnymi komponentami. Emisja dowolnej geometrii jest cechą materiału, nie
osobnym analitycznym źródłem.

`kb21.view.image-grade-zone` wskazuje profil zawierający między innymi:
ekspozycję, mapowanie tonalne, korekcję barw, poświatę, głębię ostrości,
winietę, ziarno, aberrację, rozmycie ruchu, okluzję ekranową, odbicia ekranowe,
oraz inne wartości poprawnie mieszane przestrzennie. Wygładzanie krawędzi i
skalowanie obrazu są ustawieniami kadru lub pipeline'u. Efekty profilu nie są
osobnymi komponentami.

### Obraz 2D i 3D

`kb21.draw.d2.image` ma tryby: pojedynczy obraz, klatka atlasu, animowana
sekwencja, dziewięciopolowe skalowanie i kafelkowanie. `kb21.draw.d2.tile-field`
obsługuje układ prostokątny, izometryczny i sześciokątny.
`kb21.view.aux-frame` ma przechwyt zwykły płaski, lustrzany względem
płaszczyzny z osobnym odcięciem, sześcienny i panoramiczny.
`kb21.draw.d3.solid-geometry` przyjmuje geometrię importowaną albo generowaną;
generator nie wymaga drugiego prezentera.

### Fizyka

| Komponent | Pełny zestaw wariantów |
| --- | --- |
| `kb21.physics.d3.kinetic-frame` | nieruchomy, sterowany, symulowany |
| `kb21.physics.d3.contact-shell` | pudełko, kula, kapsuła, walec, płaszczyzna/granica, otoczka wypukła, siatka trójkątów, pole wysokości, złożenie |
| `kb21.physics.d3.motion-bond` | stałe, kulowe, zawiasowe, suwakowe, stożkowo-skrętne, dystansowe, lina, sprężyna, sześć osi |
| `kb21.physics.d2.kinetic-frame` | nieruchomy, sterowany, symulowany |
| `kb21.physics.d2.contact-shell` | prostokąt, koło, kapsuła, wielokąt wypukły, odcinek, jednostronny kontur/platforma, łańcuch krawędzi, granica świata, promień separacji, złożenie |
| `kb21.physics.d2.motion-bond` | punkt obrotu, suwak, dystans, lina, sprężyna, spaw, koło/zawieszenie, silnik/serwo, przekładnia, chwyt wskaźnika |

Powłoka kontaktu ma tryb stały, czujnikowy, tylko-zapytania albo wyłączony.
Przesunięcie, orientacja, materiał fizyczny, warstwa, maska, margines, kierunek
jednostronnego kontaktu, styczna prędkość powierzchni i generowanie zdarzeń są
polami. Nie powstają osobne komponenty dla każdej bryły, wyzwalacza ani trybu
ciała. Więzy mają osie zablokowane, wolne lub ograniczone
oraz wspólne limity, napędy, sprężyny, tłumienie, próg zerwania i politykę
kolizji. Stabilizowany chwyt jest presetem więzi sześciu osi albo serwa, nie
osobnym komponentem.

### Dźwięk, woda, efekty i interakcja

`kb21.audio.voice` ma tryb nieprzestrzenny, punktowy 2D, punktowy 3D,
stożkowy oraz wielopozycyjny. Odtwarzanie, pętla, głośność, wysokość, magistrala,
priorytet, wirtualizacja, zasięg, tłumienie i efekt ruchu są polami.

`kb21.world.flow-reach` ma topologię ograniczonego zbiornika, koryta,
nieograniczonej powierzchni albo własnej siatki. Ocean, jezioro, rzeka i basen
nie są czterema komponentami.

`kb21.fx.effect-instance` wskazuje graf efektu. Wybór CPU/GPU, 2D/3D/UI,
lokalnej/światowej symulacji, jednorazowego/pętlowego czasu oraz rysowania
obrazem, geometrią, linią lub wstęgą należy do assetu efektu. Tryb UI respektuje
przycięcie, kolejność i przestrzeń swojej powierzchni interfejsu.

Kontrolki UI, interaktory immersyjne i cele interakcji są wariantami własnych,
spójnych rodzin tylko wtedy, gdy współdzielą stan i cykl życia. Lista końcowa
pozostawia osobne komponenty dla odpowiedzialności, które wymagają odrębnej
serializacji, routingu zdarzeń albo aktualizacji.

Odbiorniki obrazu, dźwięku i napisów medium pozostają osobne, ponieważ mogą
znajdować się na innych obiektach niż źródło i tworzyć relacje wiele-do-wielu.
Specjalistyczne trasy sieciowego ruchu, fizyki i pozy działają w innych fazach
oraz hot pathach; ogólna trasa stanu jawnie ich nie obejmuje.

## Co świadomie nie jest komponentem

| Rodzaj | Przykłady |
| --- | --- |
| asset | geometria, materiał, tekstura, program GPU, font, klip i graf ruchu, graf efektu, dźwięk, mikser, dokument i motyw UI, dane przejścia, sekwencja, tabela językowa, schemat sieci, profil urządzenia, scena i prototyp |
| system lub usługa | rejestr i hierarchia, scheduler i czas, renderer, świat fizyki i zapytania, mikser, wejście urządzeń, wyszukiwanie drogi, ewaluacja ruchu, układ i zdarzenia UI, transport i rollback, dekodowanie mediów, formatowanie językowe, runtime immersyjny, streaming i serializacja |
| metadane | ID, nazwa i notatka autora, odnośniki dokumentacyjne, aktywność wykonania encji, rodzic i dzieci, przynależność do sceny, proweniencja z prototypu, ogólne flagi statyczności, ukrycie edytorowe, blokada i zaznaczenie |
| cache lub wynik | macierz świata, granice po ewaluacji, uchwyty GPU, bieżąca paleta kości, kontakty, geometria znalezionej trasy, klatka śledzenia urządzenia, bazowe migawki sieciowe |

Globalny manager, backend, importer, builder, bake, bank danych, profil ustawień
ani debuger nie trafia do obiektu tylko po to, aby wydłużyć katalog.

## Wymagania architektoniczne

1. Komponent przechowuje wyłącznie autorytatywny stan instancji; system posiada
   algorytm, kolejki, stan pochodny, cache i globalne zasoby.
2. Referencje są typowane przez komponent, który ich używa. Nie ma ogólnego
   komponentu „referencja do encji”.
3. Moduł rejestruje i wyrejestrowuje wyłącznie własne schematy. Profil bez
   modułu nie linkuje jego systemów.
4. Każdy schemat ma stabilny identyfikator, wersję, jawnego właściciela,
   walidację, migrację i deterministyczną serializację.
5. Dane 2D i 3D pozostają rozdzielone, gdy ich scalenie psuje locality, rozmiar
   komponentu, wektoryzację albo kolejność wykonania.
6. Wariant używa enumu albo tagowanej unii; nie wolno utrzymywać jednocześnie
   kilku nieaktywnych konfiguracji.
7. Nawigacja zwraca pożądaną prędkość lub trasę. Nie przejmuje własności pozycji
   od systemu ruchu i fizyki.
8. Ruch korzenia, fizyka, nawigacja i sterowanie sieciowe mają jednego jawnego
   właściciela zapisu pozycji w danej fazie.
9. Narzędziowe komponenty `N` nie wchodzą do danych wydania, chyba że build
   diagnostyczny żąda ich jawnie.
10. Bieżąca poza, trasa, kontakt, pomiar urządzenia i wynik śledzenia nigdy nie
    są serializowaną konfiguracją komponentu.
11. Nazwa z tego pliku nie staje się publicznym API bez osobnego przeglądu
    technicznego, licencyjnego i prawnego.

## Ochrona tego pliku

Katalog pozostaje wyłącznie w lokalnym katalogu `others`. Ochrona jest częścią
kryteriów odbioru:

1. reguła ignorowania obejmuje cały katalog;
2. zapytanie o śledzone pliki nie może zwracać tego pliku;
3. nie wolno używać wymuszonego dodawania do kontroli wersji;
4. treści ani fragmentów nie wolno przenosić do śledzonej dokumentacji,
   zgłoszeń lub opisów zmian bez wyraźnej decyzji właściciela.

## Sprint — liniowy backlog wdrożeniowy

Sprinty wyznaczają kolejność zależności i bramy jakości, a nie czas trwania.
Zadania realizuje się od góry. Do następnego sprintu przechodzi się dopiero
po oznaczeniu wszystkich pozycji bieżącego sprintu jako `[X]` zgodnie z
kryterium powyżej.

Format pozycji: `[ ]` albo `[X]`, następnie
`[klasa docelowa / stan obecny] nazwa — identyfikator: kontrakt`.

Po zakończeniu pojedynczej pozycji, jej testach i zmianie statusu na `[X]`, a
przed rozpoczęciem następnej pozycji, agent wykonuje mały, spójny commit
obejmujący wyłącznie to zadanie i natychmiast push na bieżącą gałąź. Commit ma
być krótki; nie może zawierać `others/`, `SelfTest/` ani pracy następnego zadania.

### Sprint 01 — fundament sceny i klasyfikacja

1. [X] `[P/J]` **Klasyfikacja obiektu** — `kb21.scene.classification`: trwałe tagi i kategorie semantyczne używane przez runtime, zapis gry i edytor; maski fizyki, światła oraz widzialności należą wyłącznie do swoich komponentów.
2. [X] `[P/J]` **Brama widzialności** — `kb21.scene.visibility-gate`: jawne włączenie, ukrycie, dziedziczenie i maska widoczności obiektu.
3. [X] `[M/C]` **Forma regionu** — `kb21.scene.region-form`: współdzielona figura 2D lub bryła 3D dla stref, wpływów, niefizycznych wyzwalaczy i narzędzi.
4. [X] `[M/B]` **Krzywa prowadząca** — `kb21.scene.guide-curve`: współdzielona krzywa lub ścieżka dla ruchu, renderowania, AI i narzędzi.
5. [X] `[M/C]` **Instancja zawartości** — `kb21.scene.content-instance`: aktywne umieszczenie i czas życia prototypu, podsceny albo fragmentu świata; sama proweniencja zwykłego obiektu pozostaje metadaną.
6. [X] `[M/B]` **Ognisko strumieniowania** — `kb21.scene.stream-focus`: pozycja, promienie, priorytet i maski ładowania danych wokół obiektu.

### Sprint 02 — podstawowy obraz 3D i widzialność

7. [X] `[P/J]` **Kadr widoku** — `kb21.view.frame`: projekcja, optyka, płaszczyzny odcięcia, viewport, maski i cel obrazu.
8. [X] `[P/J]` **Geometria sztywna** — `kb21.draw.d3.solid-geometry`: zasób geometrii, powierzchnie, warstwy, cienie, granice i polityka szczegółowości.
9. [X] `[P/C]` **Tło świata** — `kb21.world.backdrop`: kolor, gradient, mapa otoczenia albo procedura widocznego tła.
10. [X] `[P/J]` **Emiter blasku 3D** — `kb21.light.d3.radiance-emitter`: równoległe, punktowe, stożkowe i wszystkie powierzchniowe warianty źródła 3D.
11. [X] `[P/C]` **Blask otoczenia** — `kb21.light.ambient-radiance`: wkład oświetleniowy stały, gradientowy, obrazowy, proceduralny, przechwycony albo estymowany z otoczenia.
12. [X] `[P/C]` **Przełącznik szczegółowości** — `kb21.scene.detail-switch`: wybór i histereza poziomu szczegółowości koordynowanego dla grupy obiektów.
13. [X] `[M/B]` **Przesłona widzialności** — `kb21.scene.visibility-blocker`: uproszczona geometria przeznaczona wyłącznie do odrzucania zasłoniętych obiektów.
14. [X] `[M/B]` **Komórka widzialności** — `kb21.scene.visibility-cell`: referencja formy regionu, członkostwo, portalowe połączenia i wymuszenia widoczne/niewidoczne.
15. [X] `[M/B]` **Portal regionu** — `kb21.scene.region-portal`: kierunkowe przejście widzialności, streamingu albo symulacji wskazujące formę otworu oraz połączone regiony.

### Sprint 03 — prezentacja i przechwytywanie 3D

16. [X] `[M/C]` **Kadr wtórny** — `kb21.view.aux-frame`: render zwykły płaski, lustrzany względem płaszczyzny, sześcienny albo panoramiczny do celu obrazu.
17. [X] `[M/C]` **Rój geometrii** — `kb21.draw.d3.geometry-swarm`: wiele instancji jednego zestawu geometrii z danymi per instancja i pośrednim rysowaniem.
18. [X] `[M/B]` **Rzut powierzchniowy** — `kb21.draw.d3.surface-cast`: projekcja szczegółu lub materiału na odbiorców ograniczona wskazaną formą regionu i kolejnością.
19. [X] `[M/B]` **Plansza zwrócona** — `kb21.draw.d3.facing-panel`: płaska geometria zwrócona do widoku, punktu, osi albo zachowująca stałą orientację.
20. [X] `[M/B]` **Kreska przestrzenna** — `kb21.draw.d3.space-stroke`: polilinia, splajn, wiązka albo przewód generowany z punktów.
21. [X] `[M/B]` **Wstęga historii** — `kb21.draw.d3.history-ribbon`: ustawienia czasu życia, szerokości i próbkowania śladu; bufor próbek oraz wygenerowana wstęga należą do systemu.
22. [X] `[S/B]` **Echo soczewki** — `kb21.draw.d3.lens-echo`: kontrolowany artefakt optyczny wskazujący źródło, profil i reguły przesłaniania.
23. [ ] `[M/B]` **Glif świata** — `kb21.draw.d3.world-glyph`: kształtowany tekst umieszczony bezpośrednio w przestrzeni 3D.
24. [ ] `[M/C]` **Próbka odbicia** — `kb21.light.reflection-sample`: lokalny przechwyt odbić wskazujący formę regionu, mieszanie, korekcję paralaksy i opcjonalne wejście z próbnika otoczenia.
25. [ ] `[M/B]` **Pole światła odbitego** — `kb21.light.bounce-light-field`: ręczne, regularne albo adaptacyjne próbki światła pośredniego ograniczone wskazaną formą regionu.
26. [ ] `[M/C]` **Strefa strojenia obrazu** — `kb21.view.image-grade-zone`: profil końcowej obróbki, tryb globalny albo referencja formy regionu, priorytet, waga i zasięg mieszania.
27. [ ] `[M/B]` **Strefa ośrodka mgielnego** — `kb21.world.fog-medium-zone`: mgła globalna, wysokościowa albo lokalna wskazująca formę regionu i własne parametry rozpraszania.
28. [ ] `[N/B]` **Strefa priorytetu wypieku** — `kb21.light.bake-priority-zone`: autorskie ograniczenie jakości, gęstości i zakresu obliczeń offline wskazujące formę regionu.
29. [ ] `[M/B]` **Powłoka atmosferyczna** — `kb21.world.air-shell`: rozpraszanie atmosferyczne, perspektywa powietrzna oraz parametry planety i źródła dziennego.
30. [ ] `[S/B]` **Warstwa chmur** — `kb21.world.cloud-layer`: objętościowe chmury, pogoda, cienie i własna polityka jakości.
31. [ ] `[S/B]` **Objętość rysowana** — `kb21.draw.d3.density-volume`: prezentacja danych gęstości, pola odległości lub materiału objętościowego.

### Sprint 04 — prezentacja i światło 2D

32. [ ] `[M/B]` **Grupa płaskiego porządku** — `kb21.scene.d2.order-group`: wspólna warstwa i kolejność rysowania potomnych elementów 2D.
33. [ ] `[P/B]` **Obraz płaski** — `kb21.draw.d2.image`: obraz pojedynczy, atlasowy, animowany, dziewięciopolowy albo kafelkowany.
34. [ ] `[P/B]` **Kształt płaski** — `kb21.draw.d2.shape`: obrys, wielokąt, krzywa lub wypełnienie z materiałem 2D.
35. [ ] `[P/B]` **Glif płaski** — `kb21.draw.d2.glyph`: kształtowany tekst sceny 2D poza drzewem interfejsu.
36. [ ] `[P/B]` **Maska płaska** — `kb21.draw.d2.mask`: maskowanie wybranej grupy odbiorców 2D.
37. [ ] `[M/B]` **Arkusz paralaksy** — `kb21.draw.d2.parallax-sheet`: współczynnik głębi, przesunięcie, skala i zawijanie warstwy względem widoku.
38. [ ] `[M/B]` **Rój płaski** — `kb21.draw.d2.swarm`: duża liczba instancji 2D współdzielących zasoby i partię rysowania.
39. [ ] `[P/B]` **Pole kafli** — `kb21.draw.d2.tile-field`: wielowarstwowa mapa prostokątna, izometryczna albo sześciokątna z porcjowaniem i kolizją źródłową.
40. [ ] `[P/B]` **Blask płaski** — `kb21.light.d2.radiance-emitter`: globalne, radialne, stożkowe, swobodne i obrazowe źródło światła 2D.
41. [ ] `[P/B]` **Forma cienia płaskiego** — `kb21.light.d2.shadow-form`: geometria przesłaniająca źródła 2D niezależnie od geometrii rysowanej.

### Sprint 05 — rig, deformacja i reguły pozy

42. [ ] `[P/C]` **Wiązanie szkieletu** — `kb21.motion.skeleton-binding`: asset hierarchii, mapa kości i pozy spoczynkowe; bieżąca paleta należy do cache'u systemu ruchu.
43. [ ] `[P/B]` **Geometria odkształcana** — `kb21.draw.d3.deformed-geometry`: geometria ze szkieletem, morfami i wiązaniem do pozy ewaluowanej poza komponentem.
44. [ ] `[M/B]` **Odkształcacz płaski** — `kb21.draw.d2.deformer`: szkieletowe albo siatkowe odkształcanie obrazu 2D.
45. [ ] `[M/C]` **Reguła pozy szkieletu** — `kb21.motion.skeleton-rule`: ograniczenie kości: celowanie, IK łańcucha, skręt, limit, sprężyna albo korekta przestrzeni.
46. [ ] `[M/C]` **Reguła ramy** — `kb21.scene.frame-rule`: constraint ewaluowany w fazie korekty ramy: kopiowanie pozycji, obrotu lub skali, celowanie, podpięcie do obiektu albo kości oraz osadzenie na krzywej; nie generuje prędkości ruchu.
47. [ ] `[S/B]` **Odtwarzacz pamięci deformacji** — `kb21.motion.deform-cache-player`: animacja wierzchołków lub innych deformacji bez bieżącej ewaluacji szkieletu.

### Sprint 06 — fizyka 3D i ruch kolizyjny

48. [ ] `[P/J]` **Rama kinetyczna 3D** — `kb21.physics.d3.kinetic-frame`: tryb ciała, masa, środek masy, bezwładność, prędkości, tłumienie, grawitacja, sen, ciągła detekcja i blokady osi.
49. [ ] `[P/J]` **Powłoka kontaktu 3D** — `kb21.physics.d3.contact-shell`: jedna lub złożona bryła kontaktu z materiałem, filtrami, jednostronnością, styczną prędkością powierzchni i trybem stałym, czujnikowym albo zapytaniowym.
50. [ ] `[M/C]` **Sonda rzutu 3D** — `kb21.physics.d3.cast-probe`: trwała konfiguracja zapytania promieniem, kulą, pudełkiem, kapsułą, kształtem, nakładaniem albo sweepem; trafienia są wynikiem systemu.
51. [ ] `[M/C]` **Napęd siłowy 3D** — `kb21.physics.d3.dynamics-actuator`: ciągła siła, moment, ciąg albo serwo przykładane do wskazanego ciała; jednorazowy impuls jest poleceniem.
52. [ ] `[P/J]` **Więź ruchu 3D** — `kb21.physics.d3.motion-bond`: konfigurowalne ograniczenie dwóch ciał z presetami, limitami, napędami i progiem zerwania.
53. [ ] `[P/J]` **Napęd postaci 3D** — `kb21.physics.d3.avatar-drive`: ruch postaci ze stokiem, stopniem, skórą, uziemieniem, platformą, ślizgiem i odzyskiwaniem po penetracji.
54. [ ] `[M/B]` **Pole oddziaływania 3D** — `kb21.physics.d3.influence-field`: profil siły i referencja formy regionu działające wyłącznie na ciała fizyczne 3D; wariant kierunkowy, radialny, wirowy, oporowy albo własny.
55. [ ] `[M/B]` **Kontakt koła** — `kb21.physics.d3.wheel-contact`: zawieszenie, promień, przyczepność, poślizg, napęd, hamulec i skręt pojedynczego koła.
56. [ ] `[M/B]` **Zespół pojazdu** — `kb21.physics.d3.vehicle-assembly`: podwozie, układ napędowy, sterowanie, hamowanie i rozdział sił.
57. [ ] `[S/B]` **Węzeł łańcucha sprzężonego** — `kb21.physics.d3.jointed-chain-node`: korzeń lub dziecko łańcucha ciał rozwiązywanego jako jedna struktura we współrzędnych zredukowanych.
58. [ ] `[S/B]` **Odpowiedź na pęknięcie** — `kb21.physics.d3.fracture-response`: próg, źródło fragmentów, przekaz pędu i reguły aktywacji zniszczenia.
59. [ ] `[M/B]` **Dynamika kości** — `kb21.physics.d3.bone-dynamics`: ragdoll, fizyczna animacja i symulacja wybranych łańcuchów szkieletu.
60. [ ] `[M/B]` **Płachta miękka** — `kb21.physics.d3.soft-sheet`: tkanina lub cienka powierzchnia z więzami, kolizją i kotwami.
61. [ ] `[M/B]` **Elastyczne pasmo** — `kb21.physics.d3.flex-strand`: jeden solver 3D liny, kabla, łańcucha lub włosa z segmentami, kotwami i kolizją; prezentacja 2D używa ograniczenia do płaszczyzny.
62. [ ] `[S/B]` **Bryła miękka** — `kb21.physics.d3.soft-volume`: odkształcalna objętość zachowująca masę, ciśnienie albo kształt.
63. [ ] `[M/C]` **Splot ruchu widoku** — `kb21.view.motion-rig`: asset modułów, priorytet i parametry prowadzenia, kadrowania, kolizji, wstrząsów oraz przejść; wykonanie należy do systemu widoku.

### Sprint 07 — fizyka 2D

64. [ ] `[P/B]` **Rama kinetyczna 2D** — `kb21.physics.d2.kinetic-frame`: tryb ciała, masa, bezwładność, prędkości, tłumienie, grawitacja, sen, ciągła detekcja i blokady osi.
65. [ ] `[P/B]` **Powłoka kontaktu 2D** — `kb21.physics.d2.contact-shell`: jedna lub złożona figura kontaktu z materiałem, filtrami, platformą jednostronną, styczną prędkością powierzchni i trybem stałym, czujnikowym albo zapytaniowym.
66. [ ] `[M/B]` **Sonda rzutu 2D** — `kb21.physics.d2.cast-probe`: trwała konfiguracja zapytania promieniem, figurą, nakładaniem albo sweepem; trafienia są wynikiem systemu.
67. [ ] `[M/B]` **Napęd siłowy 2D** — `kb21.physics.d2.dynamics-actuator`: ciągła siła, moment, ciąg albo serwo przykładane do wskazanego ciała; jednorazowy impuls jest poleceniem.
68. [ ] `[P/B]` **Więź ruchu 2D** — `kb21.physics.d2.motion-bond`: konfigurowalne połączenie dwóch ciał z presetami, limitami, sprężyną, napędem i progiem zerwania.
69. [ ] `[P/B]` **Napęd postaci 2D** — `kb21.physics.d2.avatar-drive`: ruch postaci 2D ze stokiem, stopniem, uziemieniem, platformą, ślizgiem i separacją.
70. [ ] `[M/B]` **Pole oddziaływania 2D** — `kb21.physics.d2.influence-field`: profil siły i referencja formy regionu działające wyłącznie na ciała fizyczne 2D; wariant kierunkowy, radialny, wirowy, oporowy albo własny.

### Sprint 08 — efekty i wpływy środowiskowe

71. [ ] `[P/C]` **Instancja efektu** — `kb21.fx.effect-instance`: odtwarzanie grafu cząstek lub efektu 2D, 3D albo UI z parametrami instancji.
72. [ ] `[M/B]` **Strefa sił efektu** — `kb21.fx.effect-force-zone`: profil siły i referencja formy regionu działające wyłącznie na instancje efektów; warianty obejmują kierunek, przyciąganie, odpychanie, wir, opór, turbulencję i pole wektorowe.
73. [ ] `[M/B]` **Strefa kontaktu efektu** — `kb21.fx.effect-contact-zone`: parametry kontaktu oraz referencja formy regionu albo zewnętrznego pola wysokości, głębi, odległości bądź geometrii sceny.
74. [ ] `[M/B]` **Strefa przepływu powietrza** — `kb21.world.airflow-zone`: profil prędkości i referencja formy regionu konsumowane przez efekty, roślinność i miękką fizykę; nie przykłada siły do ciał sztywnych.

### Sprint 09 — teren, woda i pogoda

75. [ ] `[M/B]` **Pole gruntu** — `kb21.world.ground-field`: kafelkowana powierzchnia wysokościowa z poziomami szczegółowości, streamingiem i materiałami warstw.
76. [ ] `[M/B]` **Stempel gruntu** — `kb21.world.ground-stamp`: lokalna modyfikacja wysokości, otworu, warstwy albo maski, opcjonalnie pobierająca kształt z krzywej prowadzącej.
77. [ ] `[S/B]` **Grunt objętościowy** — `kb21.world.voxel-ground`: strumieniowana, edytowalna geometria objętościowa z powierzchnią wyznaczaną z pola.
78. [ ] `[M/B]` **Rozsiew biomu** — `kb21.world.biome-scatter`: deterministyczne rozmieszczanie instancji według gatunków, reguł, masek i budżetów.
79. [ ] `[M/B]` **Wpływ biomu** — `kb21.world.biome-influence`: referencja formy regionu dodającej, blokującej lub modyfikującej reguły roślinności.
80. [ ] `[S/B]` **Włókna powierzchni** — `kb21.draw.d3.surface-fibers`: włosy, futro albo gęste krótkie źdźbła z profilem pasm, poziomami szczegółowości i cieniem.
81. [ ] `[M/B]` **Zasięg przepływu** — `kb21.world.flow-reach`: powierzchnia i objętość wody o topologii zbiornika, koryta, obszaru bez granic albo własnej siatki; wariant ograniczony wskazuje formę regionu.
82. [ ] `[S/B]` **Wycięcie przepływu** — `kb21.world.flow-cutout`: lokalne usunięcie albo ograniczenie powierzchni i objętości wody wskazujące formę regionu.
83. [ ] `[S/B]` **Siatka nurtu** — `kb21.world.flow-grid`: lokalne pole prędkości, głębokości i kierunku dla rzek, prądów i rozgrywki.
84. [ ] `[M/B]` **Sprzężenie wyporu** — `kb21.world.float-coupling`: próbki zanurzenia, siła wyporu, opór, unoszenie i sprzężenie z falami.
85. [ ] `[S/B]` **Stempel kilwateru** — `kb21.world.wake-stamp`: źródło fal, śladu i piany związane z poruszającym się obiektem.
86. [ ] `[M/B]` **Strefa pogody** — `kb21.world.weather-zone`: profil opadów, wilgotności i temperatury, priorytet oraz referencja formy regionu; mieszanie i sterowanie chmurami, mgłą i przepływem wykonuje system pogody.

### Sprint 10 — logika i wejście

87. [ ] `[P/J]` **Gniazdo logiki** — `kb21.logic.slot`: instancja własnego modułu logiki, faza, kolejność, aktywność i serializowane parametry.
88. [ ] `[M/C]` **Trasa zdarzeń** — `kb21.logic.event-route`: deklaratywne, typowane połączenia zdarzenie–akcja, gdy wspólna magistrala nie wystarcza.
89. [ ] `[M/C]` **Splot trybów** — `kb21.logic.mode-weave`: wykonanie skompilowanego grafu trybów lub statechartu z parametrami instancji.
90. [ ] `[M/B]` **Polityka czasu życia** — `kb21.logic.lifetime-policy`: aktywacja, wygaszenie, recykling albo usunięcie po czasie, zdarzeniu lub opuszczeniu zakresu.
91. [ ] `[P/J]` **Miejsce wejścia** — `kb21.input.seat`: lokalny użytkownik, mapa akcji, priorytet i aktywność; bieżące parowanie urządzeń posiada usługa wejścia.
92. [ ] `[P/C]` **Odbiornik akcji** — `kb21.input.action-sink`: deklaratywne, typowane punkty odbioru akcji i ich faz dla obiektu.
93. [ ] `[M/C]` **Trasa haptyczna** — `kb21.input.haptic-route`: wybór urządzenia właściciela, kanału, skali, priorytetu i wzorca sprzężenia.

### Sprint 11 — dźwięk

94. [ ] `[P/J]` **Ucho akustyczne** — `kb21.audio.ear`: pozycja i orientacja odsłuchu, priorytet oraz powiązanie z lokalnym użytkownikiem.
95. [ ] `[P/J]` **Głos akustyczny** — `kb21.audio.voice`: odtwarzanie nieprzestrzenne lub przestrzenne z assetem, parametrami, routingiem, tłumieniem i wirtualizacją.
96. [ ] `[M/B]` **Skóra akustyczna** — `kb21.audio.surface-skin`: przypisanie assetu materiału akustycznego do powierzchni lub sekcji geometrii oraz rzadkie nadpisania absorpcji, transmisji i rozpraszania.
97. [ ] `[M/C]` **Strefa akustyczna** — `kb21.audio.environment-zone`: wyłączna referencja geometrii do formy regionu, priorytet, mieszanie, pogłos, echo, filtracja i parametry środowiska.
98. [ ] `[M/B]` **Brama akustyczna** — `kb21.audio.acoustic-gate`: przejście dźwięku między strefami z transmisją, tłumieniem i otwarciem.
99. [ ] `[S/B]` **Próbka akustyczna** — `kb21.audio.acoustic-probe`: umieszczenie, zasięg i profil próbki; wypieczona propagacja, odbicia i okluzja pozostają assetem.
100. [ ] `[S/B]` **Dopływ sygnału akustycznego** — `kb21.audio.signal-intake`: wejście mikrofonowe lub wewnętrzna magistrala kierowana do bufora, pliku albo analizy.

### Sprint 12 — nawigacja i ruch

101. [ ] `[N/B]` **Źródło budowania trasy** — `kb21.route.bake-source`: udział geometrii 2D/3D w budowaniu, lokalne wykluczenie, typ obszaru i maski profili.
102. [ ] `[N/B]` **Granice wypieku trasy 3D** — `kb21.route.d3.bake-bounds`: referencja formy regionu, rozdzielczość i profile budowania danych nawigacji.
103. [ ] `[M/C]` **Strefa reguł trasy 3D** — `kb21.route.d3.rule-zone`: lokalny koszt, zakaz, typ obszaru i maski agentów ograniczone wskazaną formą regionu.
104. [ ] `[M/C]` **Przęsło trasy 3D** — `kb21.route.d3.span`: kierunkowe połączenie rozłącznych obszarów z kosztem i akcją przejścia.
105. [ ] `[M/C]` **Pole przejścia 3D** — `kb21.route.d3.walk-field`: obszar i źródło ustawień dla danych przechodnich; wygenerowana reprezentacja pozostaje assetem albo cache'em.
106. [ ] `[M/J]` **Bloker trasy 3D** — `kb21.route.d3.blocker`: dynamiczna przeszkoda, unikanie i opcjonalna modyfikacja danych przejścia.
107. [ ] `[M/J]` **Poszukiwacz trasy 3D** — `kb21.route.d3.seeker`: profil agenta, filtr obszarów, cel i parametry ruchu; geometria trasy oraz uchwyt żądania są wynikami systemu.
108. [ ] `[N/B]` **Granice wypieku trasy 2D** — `kb21.route.d2.bake-bounds`: referencja formy regionu, rozdzielczość i profile budowania danych nawigacji 2D.
109. [ ] `[M/B]` **Strefa reguł trasy 2D** — `kb21.route.d2.rule-zone`: lokalny koszt, zakaz, typ obszaru i maski agentów ograniczone wskazaną formą regionu.
110. [ ] `[M/B]` **Przęsło trasy 2D** — `kb21.route.d2.span`: kierunkowe połączenie rozłącznych obszarów z kosztem i akcją przejścia.
111. [ ] `[M/B]` **Pole przejścia 2D** — `kb21.route.d2.walk-field`: obszar i źródło ustawień dla danych przechodnich; wygenerowana reprezentacja pozostaje assetem albo cache'em.
112. [ ] `[M/B]` **Bloker trasy 2D** — `kb21.route.d2.blocker`: dynamiczna przeszkoda, unikanie i opcjonalna modyfikacja danych przejścia.
113. [ ] `[M/B]` **Poszukiwacz trasy 2D** — `kb21.route.d2.seeker`: profil agenta, filtr obszarów, cel i parametry ruchu; geometria trasy oraz uchwyt żądania są wynikami systemu.
114. [ ] `[M/C]` **Uczestnik tłumu** — `kb21.route.crowd-member`: zimny profil promienia, priorytetu i sąsiedztwa powiązany z poszukiwaczem 2D albo 3D, bez zapisu pozycji.
115. [ ] `[M/B]` **Dyrektywa ruchu** — `kb21.logic.motion-directive`: profil ruchu liniowego, obrotowego, orbitalnego, samonaprowadzającego, podążającego albo prowadzonego krzywą; zwraca żądaną prędkość lub przyspieszenie i nie zapisuje lokalnej ramy.

### Sprint 13 — AI i sterowanie

116. [ ] `[M/C]` **Pamięć decyzji** — `kb21.ai.decision-memory`: typowana, inspekcyjna i opcjonalnie trwała pamięć agenta.
117. [ ] `[M/C]` **Nadajnik bodźców** — `kb21.ai.sense-emitter`: kategorie, siła, zasięg i dynamiczne cechy wykrywalnego obiektu.
118. [ ] `[M/C]` **Odbiornik zmysłów** — `kb21.ai.sense-receiver`: zestaw zmysłów, zakresy, filtry, pamięć bodźców i reguły zapominania.
119. [ ] `[M/C]` **Wykonawca decyzji** — `kb21.ai.decision-runner`: wykonanie grafu zachowania, użyteczności albo decyzji z jawnym budżetem.
120. [ ] `[M/C]` **Polityka sterowania** — `kb21.ai.steering-policy`: składanie celów, unikania, separacji, wyrównania i ograniczeń w pożądaną prędkość.

### Sprint 14 — fundament UI

121. [ ] `[P/J]` **Powierzchnia interfejsu** — `kb21.ui.surface`: korzeń ekranowego albo światowego drzewa UI, cel obrazu, skala, kolejność i polityka wejścia.
122. [ ] `[P/B]` **Umieszczenie interfejsu** — `kb21.ui.placement`: kotwy, pivot, rozmiar, offset, lokalna rama i kolejność warstwy elementu.
123. [ ] `[P/B]` **Komórka układu** — `kb21.ui.cell`: minimum, preferencja, maksimum, flex, marginesy, wyrównanie i kolejność dziecka.
124. [ ] `[P/B]` **Przepływ układu** — `kb21.ui.flow`: wiersz, kolumna, siatka, wrap, overlay albo pozycjonowanie jawne.
125. [ ] `[P/B]` **Strażnik proporcji** — `kb21.ui.aspect-guard`: proporcje, dopasowanie treści i bezpieczny obszar ekranu.
126. [ ] `[P/C]` **Malowanie interfejsu** — `kb21.ui.paint`: kolor, obraz, wektor, dziewięciopolowe skalowanie, kafelkowanie albo materiał.
127. [ ] `[P/C]` **Glif interfejsu** — `kb21.ui.glyph`: tekst kształtowany, style zakresowe, obrazy, odnośniki, interaktywne zakresy, zawijanie, wyrównanie i przepełnienie.
128. [ ] `[P/B]` **Przycięcie interfejsu** — `kb21.ui.clip`: prostokątna, zaokrąglona, obrazowa albo wektorowa maska potomków.
129. [ ] `[M/B]` **Efekt grupy interfejsu** — `kb21.ui.group-effect`: przezroczystość grupy, filtr obrazu i opcjonalne blokowanie interakcji.
130. [ ] `[M/C]` **Cel wskaźnika** — `kb21.input.pointer-target`: trafienie, hover, naciśnięcie, klik, przewijanie, przeciąganie i filtrowanie.
131. [ ] `[M/B]` **Odbiornik gestu** — `kb21.input.gesture-sink`: tapnięcie, przytrzymanie, przesunięcie, szczypanie i obrót.
132. [ ] `[P/C]` **Cel skupienia** — `kb21.ui.focus-target`: fokusowalność, kolejność, jawni sąsiedzi, stan disabled i domyślna akcja.
133. [ ] `[M/C]` **Zakres skupienia** — `kb21.input.focus-scope`: modalny obszar skupienia, przechwycenie i przywracanie poprzedniego celu.
134. [ ] `[P/B]` **Dostępność interfejsu** — `kb21.ui.access`: rola, nazwa, opis, wartość, akcje, live-region i kolejność odczytu.
135. [ ] `[M/C]` **Łącze danych UI** — `kb21.ui.data-link`: typowane wiązanie źródła z właściwością, kierunek i konwersja.
136. [ ] `[M/B]` **Ruch stanu UI** — `kb21.ui.state-motion`: przejścia stanów hover, focus, pressed, selected, checked i disabled.
137. [ ] `[M/B]` **Most interfejsu świata** — `kb21.ui.world-bridge`: wspólne rzutowanie źródeł wskaźnikowych, dotykowych i immersyjnych na powierzchnię UI w świecie.

### Sprint 15 — kontrolki i kompozycje UI

138. [ ] `[P/C]` **Aktywator interfejsu** — `kb21.ui.press`: przycisk, powtórzenie, aktywacja klawiaturą i komenda.
139. [ ] `[P/C]` **Zatrzask wyboru** — `kb21.ui.choice-latch`: stan dwu- albo trójwartościowy i reguły zmiany.
140. [ ] `[P/B]` **Grupa wyboru** — `kb21.ui.choice-group`: wzajemnie wykluczające się opcje, wymagany wybór i nawigacja.
141. [ ] `[P/C]` **Prowadnica wartości** — `kb21.ui.value-rail`: jedna lub dwie wartości ciągłe, krok, kierunek i uchwyty.
142. [ ] `[P/B]` **Miernik interfejsu** — `kb21.ui.meter`: postęp, zdrowie, ładowanie albo inna wartość tylko do prezentacji.
143. [ ] `[P/C]` **Pole pisania** — `kb21.ui.writing-field`: tekst jedno- lub wielowierszowy, selekcja, walidacja, hasło, schowek i IME.
144. [ ] `[M/C]` **Przechwyt klawisza** — `kb21.ui.key-capture`: kontrolowane nasłuchiwanie i zapis nowego przypisania klawisza lub przycisku.
145. [ ] `[M/B]` **Edytor liczby** — `kb21.ui.number-edit`: wartość liczbowa, krok, zakres, przeciąganie i format jednostki.
146. [ ] `[P/C]` **Przewijany obszar** — `kb21.ui.scroll-area`: viewport, treść, osie, bezwładność, ograniczenia i wskaźniki przewijania.
147. [ ] `[M/B]` **Powielacz interfejsu** — `kb21.ui.repeat`: niewirtualizowane tworzenie elementów z kolekcji z kluczem stabilnej tożsamości.
148. [ ] `[M/C]` **Wirtualna kolekcja** — `kb21.ui.virtual-collection`: recykling widocznych wierszy, komórek, siatki albo tabeli dużego zbioru.
149. [ ] `[M/B]` **Źródło przeciągania** — `kb21.ui.drag-source`: ładunek, podgląd, próg, dozwolone operacje i anulowanie.
150. [ ] `[M/B]` **Cel upuszczania** — `kb21.ui.drop-target`: akceptowane typy, negocjacja operacji, podświetlenie i odbiór ładunku.
151. [ ] `[M/C]` **Okno kotwiczone** — `kb21.ui.anchored-float`: menu, okno kontekstowe lub popup z kotwą, ograniczeniem ekranu i modalnością.
152. [ ] `[M/B]` **Podpowiedź interfejsu** — `kb21.ui.tooltip`: treść, opóźnienie, kotwa, warunki pokazania i zamknięcia.
153. [ ] `[P/B]` **Selektor interfejsu** — `kb21.ui.select`: lista rozwijana, wyszukiwanie, wybór pojedynczy lub wielokrotny.
154. [ ] `[M/B]` **Przegląd gałęzi** — `kb21.ui.branch-browser`: hierarchiczne dane, rozwinięcie, selekcja, wirtualizacja i operacje wiersza.
155. [ ] `[M/B]` **Sterownik stron** — `kb21.ui.page-switch`: zakładki, aktywna strona, leniwe tworzenie i historia przejść.
156. [ ] `[M/B]` **Wskaźnik zajętości** — `kb21.ui.busy-mark`: nieokreślony albo określony stan oczekiwania i opcjonalna blokada obszaru.
157. [ ] `[M/B]` **Ekranowy nadajnik akcji** — `kb21.input.screen-action`: dotykowe źródło dyskretne, skalarne albo wektorowe z martwą strefą, powrotem i mapowaniem na akcję.

### Sprint 16 — diagnostyka i authoring

158. [ ] `[N/C]` **Kształt diagnostyczny** — `kb21.author.debug-shape`: kontrolowane linie, bryły, tekst i czas życia wizualizacji w edytorze lub buildzie diagnostycznym.
159. [ ] `[N/B]` **Wizualizacja autora** — `kb21.author.visual-aid`: ikona, strzałka, promień, stożek, pomiar lub uproszczony obszar zaznaczenia jako wariant jednej pomocy edytorowej.
160. [ ] `[N/B]` **Znacznik tworzenia** — `kb21.author.spawn-mark`: typowany punkt, orientacja, grupa i reguły bootstrapu tworzonego obiektu.
161. [ ] `[N/B]` **Bryła makiety** — `kb21.author.blockout-shape`: szybka geometria blokowa możliwa do wypieczenia do docelowych assetów.
162. [ ] `[N/B]` **Polityka wypieku** — `kb21.author.bake-policy`: decyzja udziału, maska procesów, kanały i typowane nadpisania obiektu w obliczeniach offline.

### Sprint 17 — sekwencje i lokalizacja

163. [ ] `[M/C]` **Odtwarzacz splotu właściwości** — `kb21.motion.property-weave`: lekka animacja krzywych jednej encji dla ramy, materiału, światła, UI, zdarzeń i innych typowanych pól.
164. [ ] `[M/C]` **Wiązanie sekwencji** — `kb21.sequence.binding`: logiczne sloty ścieżek powiązane z obiektami i typowanymi właściwościami sceny.
165. [ ] `[M/C]` **Odbiornik sygnału sekwencji** — `kb21.sequence.signal-sink`: markery i zdarzenia osi czasu kierowane do obiektu.
166. [ ] `[M/C]` **Wykonawca sekwencji** — `kb21.sequence.runner`: asset osi czasu, zegar, zakres, szybkość, autoplay, pętla, mieszanie i przywracanie stanu.
167. [ ] `[M/B]` **Zakres języka** — `kb21.locale.scope`: lokalne nadpisanie języka dla poddrzewa, użytkownika albo wyświetlacza w świecie.
168. [ ] `[M/C]` **Tekst językowy** — `kb21.locale.text`: tabela, klucz, kontekst, argumenty formatu i docelowa właściwość tekstowa.
169. [ ] `[M/B]` **Właściwość językowa** — `kb21.locale.property-binding`: typowane warianty wartości innej niż tekst i asset zależne od języka oraz kultury.
170. [ ] `[M/B]` **Wiązanie wariantu językowego** — `kb21.locale.variant-binding`: asynchroniczny wybór obrazu, fontu, dźwięku, modelu albo medium zależnie od języka.

### Sprint 18 — media

171. [ ] `[M/B]` **Odtwarzacz dopływu medium** — `kb21.media.feed-player`: źródło lub playlista, odtwarzanie, pętla, szybkość, zegar, dostępne ścieżki i polityka buforowania.
172. [ ] `[M/B]` **Cel klatki medium** — `kb21.media.frame-target`: typowana referencja odtwarzacza dopływu, wybór ścieżki obrazu oraz kierowanie klatek do materiału, UI, celu renderowania albo powierzchni świata.
173. [ ] `[M/B]` **Odczep dźwięku medium** — `kb21.media.sound-tap`: typowana referencja odtwarzacza dopływu, wybór ścieżki audio oraz kierowanie jej do miksera albo przestrzennego głosu.
174. [ ] `[M/B]` **Napisy medium** — `kb21.media.subtitle`: typowana referencja odtwarzacza dopływu, wybór ścieżki napisów, styl i cel UI powiązany z lokalizacją; bieżący wpis czasowy jest wynikiem systemu.
175. [ ] `[S/B]` **Węzeł synchronizacji medium** — `kb21.media.sync-group`: wspólny zegar i korekta dryfu dla wielu odtwarzaczy dopływu; pojedynczy odtwarzacz wybiera zegar bez tego komponentu.

### Sprint 19 — sieć i replikacja

176. [ ] `[M/C]` **Obecność sieciowa** — `kb21.net.presence`: opt-in obiektu, stabilna identyfikacja, własność, autorytet i podstawowa polityka aktualizacji.
177. [ ] `[M/C]` **Trasa stanu sieciowego** — `kb21.net.state-route`: typowane pola niespecjalistyczne, częstotliwość, niezawodność, warunki i kwantyzacja; wyklucza ruch, fizykę i pozę.
178. [ ] `[M/C]` **Trasa sygnału sieciowego** — `kb21.net.event-route`: przelotne wywołania klient–serwer, serwer–właściciel i multicast z kolejnością, niezawodnością, walidacją autorytetu i limitami.
179. [ ] `[M/C]` **Trasa poleceń sieciowych** — `kb21.net.command-route`: kanał uporządkowanego wejścia klienta, reguły walidacji oraz limity historii predykcji; numery sekwencji i próbki historii należą do systemu.
180. [ ] `[M/B]` **Reguła zainteresowania** — `kb21.net.interest-rule`: promień, grupy, właściciel, regiony i inne warunki istotności obiektu.
181. [ ] `[M/B]` **Brama tworzenia sieciowego** — `kb21.net.spawn-gate`: obiektowe albo regionowe zezwolenie, budżet i warunki tworzenia z sieci.
182. [ ] `[M/C]` **Trasa ruchu sieciowego** — `kb21.net.motion-route`: synchronizacja ramy, rodzica, teleportu, interpolacji i ekstrapolacji.
183. [ ] `[M/C]` **Trasa fizyki sieciowej** — `kb21.net.body-route`: stan ciała lub postaci, autorytet, predykcja, korekta i rollback.
184. [ ] `[M/B]` **Trasa pozy sieciowej** — `kb21.net.pose-route`: osobna faza i kodek replikacji parametrów, stanów i wyzwalaczy ruchu szkieletowego.

### Sprint 20 — fundament immersyjny i percepcja

185. [ ] `[S/B]` **Początek przestrzeni śledzonej** — `kb21.immersive.origin`: mapowanie przestrzeni urządzenia, podłogi albo siedzenia do świata gry.
186. [ ] `[S/B]` **Dopływ pozy urządzenia** — `kb21.immersive.device-pose-feed`: wybór głowy, kontrolera lub trackera, mapowanie osi, filtr i faza późnej aktualizacji; pomiar jest cache'em.
187. [ ] `[S/B]` **Pinezka świata** — `kb21.immersive.world-pin`: trwała kotwa obiektu w rozpoznanej przestrzeni fizycznej.
188. [ ] `[S/B]` **Dopływ dłoni** — `kb21.immersive.hand-feed`: wybór strony, mapowanie stawów, progi pewności i gesty niskiego poziomu; klatka stawów jest wynikiem.
189. [ ] `[S/B]` **Dopływ ciała** — `kb21.immersive.body-feed`: wybór źródła, mapowanie stawów, progi jakości i docelowy rig; klatka ciała jest wynikiem.
190. [ ] `[S/B]` **Dopływ spojrzenia** — `kb21.immersive.eye-feed`: wybór źródła, zgoda użytkownika, filtr i progi fiksacji; bieżący promień pozostaje wynikiem.
191. [ ] `[S/B]` **Czytnik mimiki** — `kb21.immersive.face-scan`: wybór źródła, profil topologii, mapowanie ekspresji i progi pewności; bieżąca topologia oraz wartości współczynników są wynikami runtime.
192. [ ] `[S/B]` **Kartograf punktowy otoczenia** — `kb21.immersive.point-cloud-scan`: zakres, żądana gęstość, filtr i tempo próbkowania; bieżąca chmura punktów jest wynikiem runtime.
193. [ ] `[S/B]` **Kartograf płaszczyzn** — `kb21.immersive.plane-scan`: wybór klasyfikacji, minimalnych granic i polityki aktualizacji; wykryte płaszczyzny tworzy runtime.
194. [ ] `[S/B]` **Kartograf powierzchni pomieszczenia** — `kb21.immersive.room-mesh-scan`: zakres, gęstość, klasyfikacja i polityka kolizji; wykryta geometria jest wynikiem runtime.
195. [ ] `[S/B]` **Czytnik znaczników przestrzennych** — `kb21.immersive.marker-scan`: biblioteka obrazów lub kodów, rozmiary, filtry i polityka śledzenia; bieżące pozy są wynikami.
196. [ ] `[S/B]` **Czytnik znanych form** — `kb21.immersive.known-form-scan`: biblioteka znanych obiektów 3D, rozmiary, klasyfikacja i polityka śledzenia.
197. [ ] `[S/B]` **Trasa próbek otoczenia** — `kb21.immersive.ambient-scan`: tryb próbkowania, częstotliwość, progi i odbiorcy blasku otoczenia lub odbić; estymację światła i przechwyty wykonuje system.
198. [ ] `[S/B]` **Mieszanie tła rzeczywistego** — `kb21.immersive.background-blend`: tryb obrazu tła, przezroczystość, maska i reguły przepuszczania bez własności głębi.
199. [ ] `[S/B]` **Panel kompozytora** — `kb21.immersive.compositor-panel`: warstwa płaska, cylindryczna albo panoramiczna wysyłana bezpośrednio do kompozytora.
200. [ ] `[S/B]` **Okluzja głębi otoczenia** — `kb21.immersive.depth-occlusion`: źródło głębi, kalibracja, filtr, bias i zasady zasłaniania niezależne od obrazu tła.

### Sprint 21 — interakcja i lokomocja immersyjna

201. [ ] `[S/B]` **Cel immersyjny** — `kb21.immersive.action-target`: wariant chwytalny, aktywowany albo dokowany z punktami, filtrami i regułami zaznaczenia.
202. [ ] `[S/B]` **Interaktor immersyjny** — `kb21.immersive.interactor`: wariant bezpośredni, promieniowy, wciskający albo dokujący z filtrem i stanem chwytu.
203. [ ] `[S/B]` **Obszar podróży** — `kb21.immersive.travel-area`: punkt, powierzchnia lub odcinek wspinaczki dostępny dla własnego systemu lokomocji.
204. [ ] `[S/B]` **Strażnik ciała** — `kb21.immersive.body-guard`: kapsuła użytkownika, granice przestrzeni, schylanie i ograniczenie ruchu świata.
205. [ ] `[S/B]` **Napęd lokomocji** — `kb21.immersive.move-driver`: ruch ciągły, krokowy obrót, teleport, łuk, wspinaczka i priorytet dostawcy.
