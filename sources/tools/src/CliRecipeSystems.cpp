#include "CliRecipes.hpp"

#include <array>

// The systems (a camera, a player, health, a menu ...) made of bricks, and the games made of systems. A line `use <id> name=value ...`
// runs another recipe: `target=<Object>` points it at an object that exists, `name=<Name>` and `at=x,y,z` name and place the object it
// makes, any other name=value is one of its values.
namespace kb::cli::recipes {
namespace {

constexpr std::array kSystems{
    // ---- the player and the camera ----
    Recipe{ "player-controls", Kind::System, "Controls for your own object", "Makes an object of your own (a cube, a capsule, a model) walk with WASD and jump with Space.",
        "player controls control own object character keys wasd space walk jump move sterowanie wlasny obiekt postac klawisze chodzic skakac",
        "Player:Capsule@0,1,0", "speed=5|jumpSpeed=7",
        R"(
use walk target={target} speed={speed}
use jump target={target} jumpSpeed={jumpSpeed}
)",
        "", true, "run --frames 120 --press W@5+60 (the moved: line shows it walked)" },
    Recipe{ "player-third-person", Kind::System, "Third person character", "A ready character the player walks and jumps with, with its camera behind it (WASD or the arrows, Space).",
        "third person character player camera behind walking ready preset trzecia osoba postac gracz kamera za plecami gotowa",
        "", "position=0,1,0",
        R"(
entity add --create 'Third Person Player' --position {position}
)",
        "", false, "run --frames 60 (0 diagnostics)" },
    Recipe{ "player-first-person", Kind::System, "First person character", "A ready character seen through its own eyes, walking with WASD and looking with the mouse.",
        "first person fps eyes mouse look character player view pierwsza osoba oczy mysz rozgladanie postac widok",
        "", "position=0,1,0",
        R"(
entity add --create 'First Person Player' --position {position}
)",
        "", false, "run --frames 60 (0 diagnostics)" },
    Recipe{ "player-platformer-2d", Kind::System, "2D platformer character", "A ready side-view character that runs and jumps, with a camera that follows it.",
        "platformer side view 2d character runs jumps sprite player skacz platformowka widok z boku postac biega",
        "", "position=0,2,0",
        R"(
entity add --create 'Platformer Player' --position {position}
)",
        "", false, "run --frames 60 (0 diagnostics)" },
    Recipe{ "player-top-down-2d", Kind::System, "2D top-down character", "A ready character seen from above that walks in four or eight directions, with a camera that follows it.",
        "top down overhead 2d character walks sprite player eight directions z gory widok postac chodzi osiem kierunkow",
        "", "position=0,1,0",
        R"(
entity add --create 'Top Down Player' --position {position}
)",
        "", false, "run --frames 60 (0 diagnostics)" },
    Recipe{ "camera-third-person", Kind::System, "Camera behind the player", "Makes the camera follow an object from behind and a little above, looking at it.",
        "camera third person behind follow player chase view kamera trzecia osoba za graczem sledzic",
        "", "follow=Player",
        R"(
use camera-follow target={target} follow={follow} offsetY=4 offsetZ=-7
)",
        "", true, "run --frames 120 (the camera's moved: line shows it followed)" },
    Recipe{ "camera-top-down", Kind::System, "Camera from above", "Makes the camera hang high above an object and look down at it, like a map or a strategy game.",
        "camera top down above overhead map strategy high birds eye kamera z gory nad widok mapa strategia",
        "", "follow=Player",
        R"(
use camera-follow target={target} follow={follow} offsetY=14 offsetZ=-6
)",
        "", true, "run --frames 120 (the camera's moved: line shows it followed)" },
    Recipe{ "camera-side-2d", Kind::System, "Camera from the side", "Makes the camera follow an object along a side view without turning, as in a 2D platformer.",
        "camera side scroller sideways flat 2d view platformer kamera z boku bok przewijana plaska widok",
        "", "follow=Player",
        R"(
use camera-follow target={target} follow={follow} offsetY=2 offsetZ=-14 lookAt=false
)",
        "", true, "run --frames 120 (the camera's moved: line shows it followed)" },
    Recipe{ "car-driving", Kind::System, "A car to drive", "A car the player drives with WASD and a camera that follows it from behind and above.",
        "car driving drive vehicle camera behind samochod jazda jezdzic pojazd kamera za",
        "", "name=Car",
        R"(
use drive-car name={name}
use camera-follow target='Main Camera' follow={name} offsetY=5 offsetZ=-10
)",
        "", false, "run --frames 120 --press W@5+90 (the moved: line shows the car drove)" },
    Recipe{ "lane-runner", Kind::System, "A runner on a road", "A character that runs forward by itself on a road, steers with A and D, jumps with Space, with a camera behind it.",
        "runner endless running road lanes forward jump dodge character bieg bieganie droga pas przeszkody unikac postac",
        "", "name=Runner",
        R"(
use run-forward name={name}
use jump target={name}
use camera-follow target='Main Camera' follow={name} offsetY=4 offsetZ=-8
)",
        "", false, "run --frames 120 (the moved: line shows it ran forward)" },

    // ---- life, enemies and dangers ----
    Recipe{ "health-and-damage", Kind::System, "Health with a counter and a hit sound", "Gives an object health, shows it on the screen and plays a sound when it is hurt.",
        "health damage life lives hit points counter screen sound hurt player zycie obrazenia licznik ekran dzwiek gracz trafiony",
        "Player:Capsule@0,1,0", "maxHealth=100",
        R"(
use health target={target} maxHealth={maxHealth}
use health-hud of={target}
use sound-hurt target={target}
)",
        "", true, "run --frames 60 (0 diagnostics)" },
    Recipe{ "enemy-chaser", Kind::System, "An enemy that hunts the player", "An enemy that runs after the player and hits it, can be beaten, can be found by towers and weapons and gives points when it falls.",
        "enemy enemies monster zombie goblin chase chases hunts attacks fights beaten wrog wrogowie potwor goni poscig atakuje walka pokonany",
        "Enemy:Capsule@4,1,4#27AE60", "follow=Player|speed=3|damage=10|maxHealth=50|points=10",
        R"(
use chase target={target} follow={follow} speed={speed} damage={damage}
use health target={target} team=2 maxHealth={maxHealth} destroyOnDeath=true points={points}
use tag-object target={target}
)",
        "", true, "run --frames 120 (the moved: line shows it went after the player)" },
    Recipe{ "enemy-patrol", Kind::System, "A guard that walks its round", "A guard that walks back and forth and hurts whoever touches it.",
        "guard patrol enemy round walks back forth touch hurts danger straznik patrol wrog obchod dotyk rani",
        "Sentry:Cube@6,1,0#8E44AD*1,2,1", "distance=4|speed=2|damage=10",
        R"(
use patrol target={target} distance={distance} speed={speed}
use hurt-on-touch target={target} damage={damage}
)",
        "", true, "run --frames 120 (the moved: line shows it went back and forth)" },
    Recipe{ "checkpoint-respawn", Kind::System, "Checkpoint and coming back", "A checkpoint the player touches, and the player coming back there after a fall or a death.",
        "checkpoint checkpoints respawn fall return back come dying dies death save point restart flag punkt kontrolny odrodzenie upadek smierc powrot wraca",
        "", "who=Player",
        R"(
use checkpoint who={who}
use respawn target={who}
)",
        "", false, "run --frames 120 (0 diagnostics)" },
    Recipe{ "enemy-waves", Kind::System, "Waves of enemies", "Makes an enemy prefab that heads for a base and spawns a copy every few seconds.",
        "enemy waves wave spawn base attack horde prefab copies wrogowie fala fale baza atak horda kopie",
        "", "base=Base|enemy=Invader|every=4|max=20",
        R"(
entity add --create Capsule --name {enemy} --position 0,1,-20 --color '#C0392B'
use chase target={enemy} follow={base} speed=2 damage=5 stopAt=2 range=100
use health target={enemy} team=2 maxHealth=30 destroyOnDeath=true points=10
use tag-object target={enemy}
prefab new --from-entity {enemy} --name {enemy}
entity remove --name {enemy}
use spawner name={enemy}Spawner at=0,1,-20 prefab=/Game/Prefabs/{enemy}.kbprefab every={every} max={max} spread=6
)",
        "", false, "run --frames 600 (0 diagnostics); scene-list shows the copies" },

    // ---- things to collect and open ----
    Recipe{ "coin-collecting", Kind::System, "Coins with a counter", "A row of coins the player collects by touching them, with a Coins counter on the screen.",
        "coins coin collect counter row gold money touch count monety moneta monet monetami zbierac zbieranie zbierania licznik licznikiem rzad zloto pieniadze dotyk",
        "", "item=Coin|count=5|label=Coins|position=0,1,3|side=no",
        R"(
pickup new --name {item} --count {count} --label {label} --position {position} --side-view {side}
)",
        "", false, "run --frames 150 --press W@5+90 (the log shows the count)" },
    Recipe{ "inventory-items", Kind::System, "Items to carry", "A potion and a key lying about that the player picks up, and a list of what is carried on the screen.",
        "inventory backpack bag potion key items carry carrying list pick up ekwipunek plecak mikstura klucz przedmioty niesie lista",
        "", "who=Player",
        R"(
use collectible name=Potion item=Potion who={who} at=-3,1,4
use collectible name=Key item=Key who={who} at=3,1,4
use inventory-hud
)",
        "", false, "run --frames 150 (0 diagnostics)" },
    Recipe{ "key-and-door", Kind::System, "A key and a locked door", "A key the player finds and a door that stays shut until the key is picked up.",
        "key door locked lock unlock find open gate klucz drzwi zamkniete zamek odblokowac znalezc brama",
        "", "who=Player",
        R"(
use collectible name=DoorKey item=Key who={who} at=-4,1,3
use door name=LockedDoor needs=Key at=4,1.75,6
)",
        "", false, "run --frames 150 (0 diagnostics)" },
    Recipe{ "pressure-puzzle", Kind::System, "A box on a plate opens the door", "A pressure plate, a box to push onto it and a door that opens while the plate is pressed.",
        "puzzle box push plate pressure door solve crate zagadka skrzynia pchac plyta nacisk drzwi rozwiazac",
        "", "",
        R"(
use pressure-plate name=PuzzlePlate item=Plate at=-3,0.05,6
entity add --create Cube --name PuzzleBox --position 0,1,4 --color '#B9770E' --body Dynamic
use door name=PuzzleDoor needs=Plate at=4,1.75,6
)",
        "", false, "run --frames 150 (0 diagnostics)" },
    Recipe{ "level-flow", Kind::System, "Levels one after another", "A second level, a goal that leads to it and the build set up so the built game can open it.",
        "level next levels goal exit stage world progress transition poziom nastepny cel wyjscie etap przejscie kolejny",
        "", "next=Level2|who=Player",
        R"(
scene-new --name {next} --template Basic3D
project-settings scenes --add {next}
use level-exit who={who} scene=/Game/Scenes/{next}.21kbscene
)",
        "", false, "run --frames 150 (0 diagnostics)" },
    Recipe{ "time-limit", Kind::System, "A time limit", "A countdown clock on the screen; when it reaches zero the game is over with a Restart button.",
        "time limit countdown clock seconds lose timeout hurry deadline czas limit odliczanie zegar przegrana pospiech",
        "", "seconds=60|scene=/Game/Scenes/Main.21kbscene",
        R"(
use timer-hud seconds={seconds}
use game-over-screen scene={scene}
)",
        "", false, "run --frames 120 (0 diagnostics)" },
    Recipe{ "main-menu", Kind::System, "A start menu", "A title screen with a Play button that opens the first level.",
        "main menu start title screen play begin button game menu glowne startowe ekran tytulowy graj zacznij przycisk",
        "", "next=Level1",
        R"(
scene-new --name {next} --template Basic3D
project-settings scenes --add {next}
entity add --create 'Main Menu'
entity remove --name 'Canvas/Main Menu/Settings'
entity remove --name 'Canvas/Main Menu/Quit'
use button-load-scene target='Canvas/Main Menu/Play' scene=/Game/Scenes/{next}.21kbscene
)",
        "", false, "run --frames 12 --click 'Canvas/Main Menu/Play'@5 (the log shows the scene opening)" },
    Recipe{ "game-audio", Kind::System, "Music and sounds", "Background music and a coin sound for everything the player collects.",
        "audio music sound sounds background effects coin game audio muzyka dzwiek dzwieki tlo efekty gra",
        "", "",
        R"(
use music-loop
use sound-collect
)",
        "", false, "run --frames 60 (0 diagnostics)" },

    // ---- shooting, clicking and the world ----
    Recipe{ "gun-shooting", Kind::System, "A gun with a crosshair", "A gun that fires straight ahead from the object, hurting what it hits, with a crosshair in the middle of the screen.",
        "gun crosshair aim shooting rifle pistol weapon fire fps bron celownik strzelanie karabin pistolet celowac",
        "", "damage=25",
        R"(
use shoot target={target} damage={damage}
entity add --create Text --name Crosshair
component set --entity Crosshair --type Text --set 'content=+' --set fontSize=40
@place Crosshair center 40 40
)",
        "", true, "run --frames 60 --press F@10 (0 diagnostics)" },
    Recipe{ "clicker-core", Kind::System, "Click button with a count", "A button to click that counts up, a number that grows by itself every second, and the count kept between plays.",
        "click clicks button count counter grows every second automatic przycisk licznik klikniecie klikanie rosnie",
        "", "perClick=1|perSecond=1",
        R"(
use click-counter perClick={perClick}
use idle-income perSecond={perSecond}
)",
        "", false, "run --frames 60 --click Canvas/ClickButton@10 (0 diagnostics)" },
    Recipe{ "platformer-level-2d", Kind::System, "A 2D level of platforms", "A long ground and three platforms rising to the right, with 2D physics, for a side-view character.",
        "platforms level ground 2d side view platformer stairs jump map poziom platformy ziemia widok z boku schody skok mapa",
        "", "",
        R"(
entity add --create Cube --name Ground --position 12,-0.5,0 --scale 50,1,1 --color '#4C8C3A' --body2d Static
entity add --create Cube --name Platform1 --position 6,1.5,0 --scale 4,0.5,1 --color '#8B5A2B' --body2d Static
entity add --create Cube --name Platform2 --position 13,3,0 --scale 4,0.5,1 --color '#8B5A2B' --body2d Static
entity add --create Cube --name Platform3 --position 20,4.5,0 --scale 4,0.5,1 --color '#8B5A2B' --body2d Static
)",
        "", false, "run --frames 60 (0 diagnostics)" },
    Recipe{ "platforms-3d", Kind::System, "Platforms to jump on", "Three platforms rising in steps in front of the player.",
        "platforms steps stairs jump climb 3d ledges blocks platformy schody skakac wspinac stopnie bloki",
        "", "",
        R"(
entity add --create Cube --name Step1 --position 0,0.75,6 --scale 4,1.5,3 --color '#8B5A2B'
entity add --create Cube --name Step2 --position 0,1.5,10 --scale 4,3,3 --color '#A0522D'
entity add --create Cube --name Step3 --position 0,2.25,14 --scale 4,4.5,3 --color '#B0623D'
)",
        "", false, "run --frames 60 (0 diagnostics)" },
    Recipe{ "arena-walls", Kind::System, "Walls round an arena", "Four walls round the floor so nothing leaves the play area.",
        "arena walls fence border boundary room closed area bounds sciany plot granica pokoj zamkniety obszar ogrodzenie",
        "", "half=5|length=11",
        R"(
entity add --create Cube --name WallNorth --position 0,1,{half} --scale {length},2,1 --color '#5D6D7E'
entity add --create Cube --name WallSouth --position 0,1,-{half} --scale {length},2,1 --color '#5D6D7E'
entity add --create Cube --name WallEast --position {half},1,0 --scale 1,2,{length} --color '#5D6D7E'
entity add --create Cube --name WallWest --position -{half},1,0 --scale 1,2,{length} --color '#5D6D7E'
)",
        "", false, "run --frames 60 (0 diagnostics)" },
};

constexpr std::array kGames{
    Recipe{ "platformer-2d", Kind::Game, "A 2D platformer", "A side-view game: a character that runs and jumps over platforms and collects coins.",
        "platformer 2d side view platforms jumping run sidescroller platformowka platformowa platformowe platformowy skakanka widok z boku platformy skakanie coins monety moneta zbierac",
        "", "",
        R"(
use platformer-level-2d
use player-platformer-2d position=0,2,0
use coin-collecting position=4.8,2.6,0 count=5 side=yes
)",
        "", false, "run --frames 120 --press D@5+60 (the moved: line shows the character ran)" },
    Recipe{ "platformer-3d", Kind::Game, "A 3D platformer", "A third person character that jumps up platforms, with health, a danger zone, a checkpoint and coins to collect.",
        "platformer 3d platforms jumping third person coins checkpoint hazard climb adventure skakanka 3d platformy monety checkpoint przygodowa",
        "", "",
        R"(
use player-third-person position=0,1,0
use platforms-3d
use coin-collecting position=2,1,3 count=5
use health-and-damage target=Player
use hurt-on-touch name=Lava at=0,0.1,16 damage=20
use checkpoint-respawn
)",
        "", false, "run --frames 150 (0 diagnostics)" },
    Recipe{ "top-down-adventure", Kind::Game, "A game seen from above", "A fenced arena seen from above: walk with the keys, enemies run after you, coins to collect, health on the screen.",
        "top down overhead arena adventure roguelike dungeon z gory widok arena przygoda",
        "", "",
        R"(
use arena-walls
use player-controls name=Player at=0,1,0
use camera-top-down target='Main Camera'
use enemy-chaser name=Enemy1 at=3,1,3
use enemy-chaser name=Enemy2 at=-3,1,3
use health-and-damage target=Player
use coin-collecting position=-3,1,-4 count=5
)",
        "", false, "run --frames 150 (0 diagnostics)" },
    Recipe{ "racing", Kind::Game, "A racing game", "A car on a road with a camera behind it, a finish line and a clock that counts the time.",
        "racing race racer finish line lap track wyscigi wyscig wyscigowa samochodowe samochodowy meta okrazenie tor",
        "", "",
        R"(
use car-driving name=Car
use level-exit name=FinishLine at=0,1,80 who=Car
use timer-hud countDown=false
)",
        "", false, "run --frames 150 --press W@5+120 (the moved: line shows the car drove)" },
    Recipe{ "fps-shooter", Kind::Game, "A first person shooter", "A walled arena, a first person character with a gun, enemies that hunt it, health on the screen and a game over.",
        "first person shooter fps gun arena enemies zombies health game over weapon strzelanka fps bron arena wrogowie zombie zycie koniec gry",
        "", "",
        R"(
use arena-walls
use player-first-person position=0,1,0
use gun-shooting target=Camera
use enemy-chaser name=Enemy1 at=3,1,3
use enemy-chaser name=Enemy2 at=-3,1,3
use enemy-chaser name=Enemy3 at=0,1,4
use health-and-damage target=Player
use game-over-screen who=Player
)",
        "", false, "run --frames 150 --press F@20 (0 diagnostics)" },
    Recipe{ "clicker-idle", Kind::Game, "A clicker game", "A big button to click, a count that also grows by itself, saved between plays.",
        "clicker idle incremental game tapper klikacz kliker przyrostowa klikanie",
        "", "",
        R"(
use clicker-core
use save-game name=ClickSaver
)",
        "", false, "run --frames 60 --click Canvas/ClickButton@10 (0 diagnostics)" },
    Recipe{ "puzzle-rooms", Kind::Game, "A puzzle game", "A character seen from above who finds a key and pushes a box onto a plate to open doors on the way to the next level.",
        "puzzle puzzles rooms logic logical think brain teaser lamiglowka zagadka zagadki zagadkami zagadkowa logiczna logiczne logika pokoje",
        "", "",
        R"(
use player-controls name=Player at=0,1,0
use camera-top-down target='Main Camera'
use key-and-door
use pressure-puzzle
use level-flow
)",
        "", false, "run --frames 150 (0 diagnostics)" },
    Recipe{ "endless-runner", Kind::Game, "An endless runner", "A character that runs forward for ever, dodging obstacles that keep coming, with a clock and a game over.",
        "endless runner running obstacles dodge forever infinite score distance nieskonczony bieg przeszkody unikac wynik dystans",
        "", "",
        R"(
use lane-runner name=Runner
use health target=Runner maxHealth=1
entity add --create Cube --name Obstacle --position 0,1,40 --color '#C0392B' --scale 1.5,2,1
use hurt-on-touch target=Obstacle damage=10
prefab new --from-entity Obstacle --name Obstacle
entity remove --name Obstacle
use spawner name=ObstacleSpawner at=0,1,0 prefab=/Game/Prefabs/Obstacle.kbprefab follow=Runner ahead=40 every=1.5 max=60 spread=4
use timer-hud countDown=false
use game-over-screen who=Runner
)",
        "", false, "run --frames 400 (0 diagnostics)" },
    Recipe{ "tower-defense", Kind::Game, "A tower defence game", "A base to protect, towers that shoot enemies automatically, waves of enemies and a game over when the base falls.",
        "tower defense defence base waves enemies turrets protect strategy lose obrona wieza baza fale wrogowie wieze strategia",
        "", "",
        R"(
entity add --create Cube --name Base --position 0,1,8 --color '#2980B9' --scale 3,2,3
use health target=Base maxHealth=200
use health-hud of=Base label=Base
entity set --name 'Main Camera' --position 0,22,-4 --rotation 75,0,0
use turret name=TowerLeft at=-5,1,0
use turret name=TowerRight at=5,1,0
use enemy-waves base=Base
use game-over-screen who=Base
)",
        "", false, "run --frames 600 (0 diagnostics)" },
    Recipe{ "action-rpg", Kind::Game, "An action adventure", "A third person hero with a sword and health fights enemies, talks to a villager, picks up items, saves its progress and starts from a menu.",
        "action rpg adventure hero sword fight enemies villager talk items save menu quest fantasy przygodowa rpg bohater miecz walka wrogowie wiesniak rozmowa zapis menu",
        "", "",
        R"(
use main-menu next=Level1
use player-third-person position=0,1,0
use health-and-damage target=Player
use melee-attack target=Player
use enemy-chaser name=Goblin1 at=10,1,10
use enemy-chaser name=Goblin2 at=-10,1,10
use dialog name=Villager at=5,1,3
use inventory-items
use save-game target=Player
)",
        "", false, "run --frames 150 (0 diagnostics)" },
};

} // namespace

std::span<const Recipe> SystemRecipes() {
    return kSystems;
}

std::span<const Recipe> GameRecipes() {
    return kGames;
}

} // namespace kb::cli::recipes
