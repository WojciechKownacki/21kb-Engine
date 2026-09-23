# Audyt ECS — 23 września 2026

## Zakres i sposób pomiaru

Sprawdzono katalog encji, magazyn archetypów, migracje komponentów, zapytania, bufor komend, odtwarzanie snapshotów i wybrane ścieżki harmonogramu. Pomiary wykonano kolejno, bez równoległych benchmarków, na Windows, MSVC 17.12, Intel Core i5-7300HQ (4 rdzenie), 23,9 GiB RAM. Czasy w tabeli pochodzą z konfiguracji Debug; końcowy scenariusz miliona encji zmierzono też w Release. Zestawy testowe używały wskazanych rozmiarów i konfiguracji świata. Wyniki różnych scenariuszy nie są zamiennymi miarami przepustowości.

## Potwierdzone wąskie gardła i poprawki

| Ścieżka | Przyczyna | Wynik przed → po | Zmiana |
| --- | --- | --- | --- |
| Pojedyncze niszczenie encji z katalogiem, 20 tys. | `vector::erase` przeszukiwał i przesuwał katalog przy każdym usunięciu, co dawało koszt kwadratowy dla serii. | 738 → 23 ms, około 32× szybciej; tworzenie 52 → 50 ms. | Indeks gęsty dla kolejnych ID, mapa dla ID z lukami, obcą generacją lub kolizją; usuwanie przez zamianę z ostatnim wpisem. |
| `CommandBuffer::PlaybackTrusted`, 100 tys. tworzonych encji | Bezwarunkowe budowanie zbioru ID, choć bez ogólnego `SetParents` nikt go nie odczytywał. | Mediana 125 → 29 ms, około 4,3× szybciej. | Zbiór powstaje tylko dla poleceń wymagających rozpoznania encji utworzonych w tym odtworzeniu. |
| Częściowa delta snapshotu, 1 mln encji, jeden zmieniany komponent | Wywołanie mutacji i ponowne rozwiązywanie typu dla każdego wiersza, ponadto powtórzone kontrole w zbiorczym API. | 27,28 → 8,13 s, około 3,4× szybciej. | Rozwiązanie typu na chunk, mutacja kolumnowa i usunięcie powtórnego sprawdzania już zweryfikowanych składników. |
| Pełna delta snapshotu, 1 mln istniejących encji, dwa komponenty, bez usuwania składników | Osobna mutacja każdego wiersza mimo braku zmian struktury. | 28,93 → 10,85 s, około 2,7× szybciej. | Zbiorcza ścieżka dla chunków, których encje istnieją i które nie wymagają usuwania składników. |
| Pełna delta tworząca 1 mln nowych encji | Adoptowanie każdego wiersza osobno omijało istniejącą ścieżkę zbiorczą. | Bazowy przebieg przekroczył 120 s i został przerwany; po zmianie 5,06 s, czyli co najmniej 23× szybciej względem tej granicy. | Zbiorcza adopcja całego chunka, gdy wszystkie jego ID są nowe. |
| Pełna delta mieszana, 1 mln encji, 10% odtwarzanych ID | Chunk przechodził na pojedyncze aktualizacje i adopcje dla każdego wiersza. | Bazowy przebieg przekroczył 120 s i został przerwany; po grupowaniu 11,80 s w Debug, co daje co najmniej 10× względem tej granicy. Szczyt przyrostu pamięci testu wyniósł około 520 MB. | Podział wierszy na istniejące i nowe, spakowanie kolumn oraz zbiorcza aktualizacja, usuwanie składników i adopcja. |
| Telemetria zapytania przy wielu archetypach | Liczenie unikalnych archetypów porównywało każdy rekord z wcześniejszymi, koszt O(R×A) dla R chunków i A archetypów. | Dwa archetypy i 1466 rekordów: 12,20 → 1,54–1,88 ms. Dla jednego archetypu i 7813 rekordów około 9 ms przed i po. | Liczenie przejść między sąsiednimi archetypami w już pogrupowanych rekordach, O(R). |
| Cache 4096 dynamicznych planów zapytań | Liniowe szukanie każdego planu i brak ograniczenia liczby wpisów. | 20,313 → 0,198 s łącznie, około 103× szybciej; sam lookup 20,109 s → 6,60 ms. | Indeks haszowany z pełnym porównaniem i LRU, domyślnie maksymalnie 1024 wpisy. |
| Wyszukanie archetypu, 30 tys. powtórzeń przy 1024 składach | Liniowe przeglądanie wszystkich tablic. | Mediana 2041 → 932 ms, około 2,2× szybciej; pamięć prywatna +0,43 MiB. | Indeks haszowany z pełnym porównaniem typów, rozmiaru, wyrównania i klasy przechowywania. |
| Masowe niszczenie 1024 encji z 2048 tabel backendu | Liniowe szukanie każdej kolejnej tablicy na liście już widzianych. | 4,09 → 3,41 ms; przy 512 tabelach zysk nie był istotny. | Mała lista dla maksymalnie 16 tablic, później zbiór. |
| Transakcyjne tworzenie 1 mln encji przez bufor komend | Indeksowanie utworzonych ID mimo braku operacji, które go odczytują. | 1220,66 → mediana 287,00 ms, około 4,25× szybciej. | Warunkowe budowanie zbioru ID. Wynik obejmuje też równoległe zmiany magazynu. |
| `WasDestroyed`, 10 tys. odczytów po zniszczeniu 10 tys. encji | Liniowe wyszukiwanie w wyniku bufora. | 258,04 → 14,77 ms, około 17,5× szybciej. | Posortowany indeks wyniku dla większych zbiorów. |
| Graf harmonogramu, 2000 niezależnych systemów | Porównywanie wszystkich par i skan gotowych systemów w każdej iteracji. | 2089,8 → 13,47 ms, około 155× szybciej; 500 konfliktujących: 309,84 → 26,54 ms. | Historia dostępu do składników i kolejka gotowych systemów. |
| Profilowana klatka 2000 systemów | Ponowne budowanie grafu odwrotnego na klatkę oraz liniowe wyszukiwanie liczników. | 1316,09 → 28,10 ms, około 46,8× szybciej. | Cache grafu i bezpośredni indeks liczników. |
| 100 tys. drobnych paczek `WorkerPool` z preferowanym pracownikiem | Kolejka i wspólny mutex dla paczek o regularnym przydziale. | 64,50 → 3,36 ms, około 19,2× szybciej. | Statyczny podział, gdy preferencja odpowiada `index % workerCount`. |
| Pojedyncze migracje 100 tys. encji | Powtarzane alokacje list typów i klucza przejścia nawet przy trafieniu w cache. | Dodawanie: mediana 1296 → 1005 ms; usuwanie: 1198 → 864 ms w Debug. | Ponowne użycie buforów typów i heterogeniczny lookup krawędzi bez tworzenia wektora przy trafieniu. |
| Pełna inspekcja 1 mln encji z jednym komponentem reflektowanym, po 10% churnie | Pośredni `EntityInspection` alokował i kopiował listę metadanych komponentów, którą builder zaraz odrzucał. | Mediana 21,39 → 15,09 s w Debug, około 29,5% szybciej na identycznym scenariuszu. | Serializacja komponentów wprost z listy zarejestrowanych typów, bez pośredniego rekordu. |
| Graf 4000 systemów z gęstymi barierami | Każda bariera dodawała krawędzie do wszystkich wcześniejszych i późniejszych systemów. | 4,72–4,77 s → 27–29 ms w Debug, około 170× szybciej. | Krawędzie tylko do sąsiednich segmentów; globalny porządek wynika z przechodniości. |
| Mutowalne zapytanie ruchu, 1 mln encji | Po przetworzeniu każdego chunka oznaczano jako zmienione wszystkie chunki archetypu, co powodowało koszt kwadratowy w liczbie chunków. | Mediana 21,79 → 1,83 ms na krok w Release, około 11,9× szybciej. | Oznaczanie tylko chunka, którego dane zapisano; analogiczna poprawka w kompatybilnym iteratorze mutowalnym. |

Po zmianie katalogu tworzenie i pojedyncze niszczenie **1 mln** encji trwały 2,68–3,27 i 1,12–1,27 s w przebiegach Debug, a **2 mln**: 5,21 i 2,29 s. Szczyt pamięci prywatnej procesu w pierwotnym teście katalogu wyniósł odpowiednio około 85 i 170 MiB. Zewnętrzne i nieciągłe ID zachowują poprawność dzięki mapie pomocniczej. Tworzenie encji cofa teraz wpis katalogu, jeśli dalsza część operacji zgłosi błąd.

Po zmianie bufor komend utworzył **1 mln** encji przez `PlaybackTrusted` w 283–294 ms; szczytowy working set wyniósł około 167 MiB. Wariant transakcyjny `Playback` zachowuje obsługę wycofywania zmian i buduje indeks utworzonych ID, gdy polecenia mogą go potrzebować.

Wierszowy `ForEach` dla 500 tys. encji miał przed zmianą pięć pomiarów 56,8/55,9/53,7/51,3/49,5 ms, po ponownym użyciu scratch 55,8/47,8/51,6/49,3/49,0 ms. Zysk jest mały względem rozrzutu; główny efekt tej zmiany to ograniczenie powtarzanych alokacji. Oddzielne instancje `Query` mogą odczytywać stabilny świat równolegle z włączoną telemetrią; liczniki są synchronizowane. Stan telemetrii ma stabilny adres po przeniesieniu `World`.

Pełna delta usuwająca jeden komponent ze wszystkich **1 mln** istniejących encji trwała **20,67 s w Debug**. Test sprawdził zachowanie pozycji i brak usuniętego komponentu u każdej encji. To pomiar po zbiorczej migracji, bez osobnego bazowego przebiegu; delta nadal zapisuje pozostałe kolumny i odzwierciedla zmianę w backendzie.

Pierwotny pomiar pełnej inspekcji edytora po 10% churnie, bez reflektowanego komponentu, trwał **6,49 s dla 1 mln encji** i osiągnął około **390 MiB pamięci prywatnej**. Samo pobranie katalogu żywych ID zajęło 0,585 s. Próba pominięcia kontroli życia pogorszyła wynik do 6,71 s i została wycofana. Nowe porównanie A/B używało jednego reflektowanego komponentu i dlatego nie jest bezpośrednio porównywalne z wynikiem 6,49 s; w tym samym A/B po zmianie czas spadł z 21,39 do 15,09 s.

Końcowy test snapshotu i katalogu w **Release** zakończył się poprawnie. Przy 1 mln encji utworzenie świata, zapis i odczyt snapshotu trwały 0,157 / 0,347 / 0,267 s; częściowa delta, pełna aktualizacja, pełna adopcja, delta mieszana i delta usuwająca komponent trwały 0,555 / 0,679 / 0,499 / 1,327 / 1,914 s. Utworzenie i pojedyncze usunięcie 1 mln encji z katalogiem trwały 0,119 / 0,100 s. Szczyt przyrostu working set wyniósł około 432 MB. Są to czasy po zmianach, bez osobnego bazowego pomiaru Release.

## Powtarzalny benchmark obciążenia CPU

`kb_ecs_workload_benchmark` tworzy encje z pozycją i prędkością, wykonuje 10 kroków rozgrzewki i 120 mierzonych kroków `pozycja += prędkość / 60`, a następnie dodaje trzeci komponent do wszystkich encji przez bufor komend. Weryfikuje pozycje próbek i liczbę encji z nowym komponentem. Podaje medianę i 95. percentyl czasu kroku. Domyślny wariant używa pełnej konfiguracji `DesktopDefault`; opcja `native-only` wyłącza lustro backendu i katalog encji. Czasy utworzenia obejmują wywołanie zbiorcze, czasy dodania obejmują zapis polecenia i jego odtworzenie. Pomiar nie obejmuje renderowania ani fizyki.

Uruchomienie: `cmake --build build --config Release --target kb_ecs_workload_benchmark` oraz `build/engine/Release/kb_ecs_workload_benchmark.exe 1000000 120` (opcjonalnie trzeci argument `native-only`). Na i5-7300HQ dla 1 mln encji pełny świat uzyskał 107,09 ms utworzenia, 1,95 ms mediany aktualizacji i 920,22 ms zbiorczego dodania komponentu; sam magazyn natywny: 38,48 / 1,88 / 774,48 ms. Dla 5 mln encji w pełnym świecie odpowiednie czasy wyniosły 464,02 / 10,38 / 4823,84 ms. Są to pojedyncze przebiegi na lokalnej maszynie, bez gwarancji przenoszenia na inny sprzęt. Przed poprawką oznaczania zmian ten sam benchmark dla 1 mln encji dawał 21,79 ms mediany aktualizacji.

## Zachowanie magazynu przy milionach encji

Zbiorcze operacje magazynu natywnego skalowały się blisko liniowo w badanym zakresie:

| Liczba encji | Utworzenie | Dodanie komponentu | Usunięcie komponentu | Zniszczenie |
| ---: | ---: | ---: | ---: | ---: |
| 1 mln | 288 ms | 2582 ms | 2323 ms | 190 ms |
| 2 mln | 489 ms | 4563 ms | 4750 ms | 381 ms |

Po dalszej optymalizacji pojedyncze migracje 100 tys. encji trwały około 1005 ms przy dodawaniu i 864 ms przy usuwaniu komponentu. Operacje zbiorcze pozostają właściwą drogą dla dużych zbiorów.

## Weryfikacja

Zbudowano w Debug targety `kb_ecs_api_tests`, `kb_ecs_scheduler_correctness_tests`, `kb_ecs_deterministic_replay_tests`, `kb_ecs_stress_tests` oraz `kb_ecs_snapshot_scale_tests`. Po końcowych zmianach wszystkie **5/5** zestawów Debug przeszło w dwóch kolejnych wywołaniach `ctest`. Target `kb_ecs_snapshot_scale_tests` zbudowano i uruchomiono także w Release z poprawnym wynikiem. Test skali weryfikuje 1 mln encji, sumy komponentów po zapisie/odczycie, częściowej, pełnej, mieszanej i usuwającej delcie oraz adopcji. Testy API obejmują kolizje generacji ID, luki, różne klasy przechowywania, usuwanie zbiorcze, cache zapytań, równoległą telemetrię, przenoszenie świata, harmonogram i relacje rodzic–dziecko.

## Otwarte koszty i granice wyniku

- Zbiorcze dodanie komponentu do 1 mln encji trwało 920 ms w pełnym świecie i 774 ms przy samym magazynie natywnym w Release. Dominujący koszt pozostaje w migracji archetypów, w tym kopiowaniu danych i aktualizacji położeń encji.
- Pełna inspekcja edytora nadal materializuje i serializuje milion rekordów; w scenariuszu z reflektowanym komponentem po optymalizacji trwała 15,09 s w Debug. Częste pełne odświeżanie wymagałoby kontraktu pobierania mniejszych zakresów.
- Profil delty usuwającej komponent z 1 mln encji (20,52 s w Debug) rozdzielił czas na `AddComponents` 6,85 s, `RemoveComponents` 11,27 s i pozostałe około 2,4 s. Próba skrócenia walidacji pogorszyła czas do 21,61 s i została wycofana. Backend nie oferuje publicznej operacji zbiorczego usunięcia z zachowaniem zdarzeń, więc milion encji nadal przechodzi milion migracji.
- Cache grafu odwrotnego harmonogramu zużywa pamięć proporcjonalną do liczby krawędzi. Arbitralne preferencje pracowników niespełniające `index % workerCount` nadal używają kolejki i mutexa; próba licznika oczekujących paczek dała około 2% różnicy w granicach szumu i została wycofana.
- Jedna instancja `Query` nadal nie może być wywoływana równolegle, a mutowalne zapytania nie mogą nakładać się z innymi. Test dwóch oddzielnych odczytowych instancji z telemetrią przeszedł; lokalnie brak ThreadSanitizer dla MSVC/Windows ogranicza wniosek o współbieżności.
- Cache planów ma limit 1024 lub większy `reserveQueryCache`; aktywne `Query` mogą nadal trzymać własne plany poza limitem. Wierszowy `ForEach` nie wykazał stabilnego dużego zysku w pomiarach 500 tys. encji.
- Pomiar 1–2 mln encji sprawdza opisane scenariusze, a nie wszystkie możliwe układy komponentów, relacji i wzorce dostępu.
