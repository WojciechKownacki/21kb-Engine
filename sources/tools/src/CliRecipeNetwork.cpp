#include "CliRecipes.hpp"

#include <array>

// The recipes for a game of several players, on the three floors like the rest: a brick that makes one existing object networked, systems
// (a character for each player, a score they share, a lobby with a Start button, a chat) and the games made of them. The server runs the
// game and decides; every recipe's scripts also run in a single-player scene (Net.Mode is "offline"), where they do nothing or play locally.
// `kb_cli net check` tries one with a server and players in one process.
namespace kb::cli::recipes {
namespace {

constexpr std::array kBricks{
    Recipe{ "net-object", Kind::Brick, "Make an object networked", "Makes this object one every player sees at the place the server has it, whatever moves it on the server (physics, a script, a player's character).",
        "+network +networked +multiplayer +online +sync +synchronize +synchronise +replicate +replicated +siec +sieciowy +sieciowa +sieciowym +sieciowe +synchronizacja +synchronizowac +replikowac shared players server see same position move object coop wspolny gracze gracz widza serwer obiekt ruch",
        "", "",
        R"(
attach NetObject
)",
        R"lua(
--- NetObject
-- The server shows this object to the players, and each player's copy of it goes where the server's goes. A player binds the server's object
-- to its own by name, so give every networked object a name of its own. Single-player, it does nothing.
local replicas = {}
local function online()
    local net = Net.Mode()
    return net ~= nil and net.mode ~= "offline"
end
function Ready(self, dt)
    if not online() then return end
    if not GetShared("net.object.declared") then
        SetShared("net.object.declared", true)
        CallFunction("Net.RegisterRpc", { name = "NetWho", direction = "toServer", args = "entity", maxPerSecond = 120 })
        CallFunction("Net.RegisterRpc", { name = "NetIs", direction = "toClients", args = "entity,string(64)" })
    end
    if Net.IsServer() then
        CallFunction("Net.Replicate", { entity = self.entity })
    end
end
function OnNetSpawned(self, event)
    local replica = event.args.entity
    if GetShared("net.object.asked." .. replica) then return end
    SetShared("net.object.asked." .. replica, true)
    CallFunction("Net.Call", { rpc = "NetWho", e1 = replica })
end
function OnRpc_NetWho(self, event)
    if self.entity ~= event.args.e1 then return end
    CallFunction("Net.Call", { rpc = "NetIs", target = event.args.sender, e1 = self.entity, s1 = World.Name(self.entity) })
end
function OnRpc_NetIs(self, event)
    if World.Name(self.entity) ~= event.args.s1 then return end
    replicas[self.entity] = event.args.e1
    Log("net-object " .. event.args.s1 .. " follows the server")
end
function OnNetDespawned(self, event)
    if replicas[self.entity] == event.args.entity then
        replicas[self.entity] = nil
        World.Destroy(self.entity, true)
    end
end
function Tick(self, dt)
    local replica = replicas[self.entity]
    if not replica then return end
    local at = Transform.GetPosition(replica)
    if not at then return end
    Transform.SetPosition(self.entity, at.x, at.y, at.z)
    local turn = Transform.Rotation(replica)
    if turn then Transform.SetRotation(self.entity, turn.x, turn.y, turn.z) end
    if Physics.GetVelocity(self.entity) then Physics.SetVelocity(self.entity, 0.0, 0.0, 0.0) end
end
)lua",
        true, "net check --press 1:W@40+40 (the replicated: line shows the object at the same place on the server and on every client)" },
};

constexpr std::array kSystems{
    Recipe{ "net-players", Kind::System, "A character for each player", "Every player who joins gets a character of their own; WASD moves it, the server decides where it goes and every player sees it move.",
        "+multiplayer +network +online +sync +synchronize +synchronise +synchronizacja +synchronizowac +siec +sieciowy +dolacza players player join joins character avatar walk wasd move movement replicate gracze gracz postac ruch ruchu postaci chodzic klawisze widza",
        "NetPlayers@0,0,0", "speed=5|spacing=2|waitForStart=false|prefab=/Game/Prefabs/NetAvatar.kbprefab",
        R"(
entity add --create Capsule --name NetAvatar --position 0,1,0
component add --entity NetAvatar --type Rigidbody --if-missing
component set --entity NetAvatar --type Rigidbody --set bodyType=Dynamic --set lockRotation=true
prefab new --from-entity NetAvatar
entity remove --name NetAvatar
input-action new --name Move --type Axis2D --key W=+y --key S=-y --key D=+x --key A=-x --key ArrowUp=+y --key ArrowDown=-y --key ArrowRight=+x --key ArrowLeft=-x
attach NetPlayers
)",
        R"lua(
--- NetPlayers
-- Every player who joins gets a character, a copy of the prefab, in a row from x = -3 at the near edge of the floor (z = -3). The player holds Move (WASD or the arrows): it tells the server
-- which way, the server moves the character and every player sees it move. A replicated object with an owner is a player's character. Single-player,
-- it does nothing. With waitForStart the characters come when the game starts (the event NetGameStarted, sent by the lobby).
-- @expose prefab String = "/Game/Prefabs/NetAvatar.kbprefab"
-- @expose speed Float = 5.0
-- @expose spacing Float = 2.0
-- @expose waitForStart Bool = false
local avatars = {}
local order = {}
local started = true
local moves = {}
local proxies = {}
local mine = 0
local sentX, sentY = 0.0, 0.0
local function online()
    local net = Net.Mode()
    return net ~= nil and net.mode ~= "offline"
end
local function spawn(self, player)
    if avatars[player] then return end
    local count = 0
    for _ in pairs(avatars) do count = count + 1 end
    local v = self.variables
    local entity = World.InstantiatePrefab(v.prefab, nil, nil, -3.0 + count * v.spacing, 1.0, -3.0)
    if not entity or entity == 0 then
        Log("NetPlayers: the prefab " .. v.prefab .. " could not be placed (is it in the project?)")
        return
    end
    CallFunction("Net.Replicate", { entity = entity })
    CallFunction("Net.SetOwner", { entity = entity, player = player })
    avatars[player] = entity
    Log("player " .. player .. " joined")
end
function Ready(self, dt)
    if not online() then return end
    started = not self.variables.waitForStart
    CallFunction("Net.RegisterRpc", { name = "NetMove", direction = "toServer", args = "entity,float(-1..1),float(-1..1)", owner = 0, maxPerSecond = 60 })
end
function OnNetConnected(self, event)
    if not Net.IsServer() then return end
    order[#order + 1] = event.args.player
    if started then spawn(self, event.args.player) end
end
function OnNetDisconnected(self, event)
    if not Net.IsServer() then return end
    local entity = avatars[event.args.player]
    if entity then
        moves[entity] = nil
        World.Destroy(entity, true)
        avatars[event.args.player] = nil
    end
    for index, player in ipairs(order) do
        if player == event.args.player then table.remove(order, index) break end
    end
end
function NetGameStarted(self, event)
    if not online() or not Net.IsServer() then return end
    started = true
    for _, player in ipairs(order) do spawn(self, player) end
end
function OnRpc_NetMove(self, event)
    moves[event.args.e1] = { x = event.args.n1, y = event.args.n2 }
end
function OnNetSpawned(self, event)
    if event.args.owner == 0 then return end
    local replica = event.args.entity
    local at = Transform.GetPosition(replica)
    if not at then return end
    local copy = World.InstantiatePrefab(self.variables.prefab, nil, nil, at.x, at.y, at.z)
    if copy and copy ~= 0 then proxies[replica] = copy end
    if event.args.owner == Net.LocalPlayerId() then mine = replica end
end
function OnNetDespawned(self, event)
    local copy = proxies[event.args.entity]
    if copy then World.Destroy(copy, true) end
    proxies[event.args.entity] = nil
    if mine == event.args.entity then mine = 0 end
end
function Tick(self, dt)
    if not online() then return end
    if Net.IsServer() then
        local speed = self.variables.speed
        for entity, move in pairs(moves) do
            local velocity = World.Exists(entity) and Physics.GetVelocity(entity) or nil
            if velocity then
                Physics.SetVelocity(entity, move.x * speed, velocity.y, move.y * speed)
                if move.x ~= 0.0 or move.y ~= 0.0 then Transform.SetRotation(entity, 0.0, math.deg(math.atan2(move.x, move.y)), 0.0) end
            else
                moves[entity] = nil
            end
        end
        return
    end
    for replica, copy in pairs(proxies) do
        local at = Transform.GetPosition(replica)
        if at then
            Transform.SetPosition(copy, at.x, at.y, at.z)
            local turn = Transform.Rotation(replica)
            if turn then Transform.SetRotation(copy, turn.x, turn.y, turn.z) end
            if Physics.GetVelocity(copy) then Physics.SetVelocity(copy, 0.0, 0.0, 0.0) end
        end
    end
    if mine == 0 then return end
    local move = Input.Vector2("Move")
    local x, y = move.x or 0.0, move.y or 0.0
    if x ~= sentX or y ~= sentY then
        sentX, sentY = x, y
        CallFunction("Net.Call", { rpc = "NetMove", e1 = mine, n1 = x, n2 = y })
    end
end
)lua",
        false, "net check --press 1:W@40+40 (the log says 'player 1 joined'; the replicated: line shows the character moved forward on the server and on both clients)" },
    Recipe{ "net-score", Kind::System, "A score every player shares", "A counter on every player's screen that adds up what any player collects or defeats; the server counts, so everyone sees the same number.",
        "+shared +together +team +coop +cooperative +multiplayer +online +network +wspolny +wspolna +wspolne +razem +druzyna +kooperacja score points counter total hud screen coins collect players licznik punktow punkty wynik ekran gracze zbieranie",
        "NetScoreText:Text", "label=Score",
        R"(
component set --entity {target} --type Text --set 'content={label}: 0' --set fontSize=28
@place {target} top-right 300 40
attach NetScore
)",
        R"lua(
--- NetScore
-- The server adds the points of every ItemCollected and tells every player the total; a player who joins asks for it. Single-player,
-- the total is counted here. The total is also the shared value "score". A player who joins asks for the total once its connection works.
-- @expose label String = "Score"
local total = 0
local told = false
local function online()
    local net = Net.Mode()
    return net ~= nil and net.mode ~= "offline"
end
local function show(self)
    SetShared("score", total)
    self:SetProperty("Text", "content", self.variables.label .. ": " .. total)
end
function Ready(self, dt)
    if online() then
        CallFunction("Net.RegisterRpc", { name = "NetScore", direction = "toClients", args = "uint(0..1000000)" })
        CallFunction("Net.RegisterRpc", { name = "NetScoreNow", direction = "toServer", maxPerSecond = 5 })
    end
end
function ItemCollected(self, event)
    if online() and not Net.IsServer() then return end
    total = total + (event.args.points or 1)
    show(self)
    if online() then
        CallFunction("Net.Call", { rpc = "NetScore", target = 0, n1 = total })
    end
end
function Tick(self, dt)
    if told or not online() or Net.IsServer() then return end
    if Net.Mode().connected then
        told = CallFunction("Net.Call", { rpc = "NetScoreNow" }) ~= nil
    end
end
function OnNetDisconnected(self, event)
    told = false
end
function OnRpc_NetScoreNow(self, event)
    CallFunction("Net.Call", { rpc = "NetScore", target = event.args.sender, n1 = total })
end
function OnRpc_NetScore(self, event)
    total = event.args.n1
    show(self)
    Log("score is " .. total)
end
)lua",
        false, "net check --press 1:W@40+40 (the log says 'collected Coin' on the server and 'score is 1' on every client)" },
    Recipe{ "net-health", Kind::System, "Health every player sees", "Health of each player's character that the server keeps and every player sees: damage is an event, the server lowers the health and sends it to everyone as a call; spikes that hurt whoever touches them show it.",
        "+multiplayer +online +network +siec +sieciowy +sieciowa +coop health life hp damage hurt spikes lives zdrowie zycie obrazenia kolce gracze players characters sync",
        "HealthText:Text", "label=Health|maxHealth=100|damage=10|who=NetAvatar",
        R"(
component set --entity {target} --type Text --set 'content={label}: {maxHealth}' --set fontSize=28
@place {target} top-left 300 40
entity add --create Cube --name Spikes --position 0,0.5,0 --scale 8,1,0.5
component set --entity Spikes --type Collider --set trigger=true
attach NetHealth
attach NetHazard --node Spikes
)",
        R"lua(
--- NetHealth
-- Only a position travels by itself; health goes as a call. The server keeps the health of each character: a script of the server sends the event
-- NetDamage { entity, amount }, the server lowers that character's health and tells every player (NetHealth), and a player who sees a character appear
-- asks for its health (NetHealthOf), because a call about a character a player does not have yet is dropped. A player's own health shows in this text.
-- Single-player, it does nothing.
-- @expose label String = "Health"
-- @expose maxHealth Float = 100.0
local health = {}
local owners = {}
local mine = 0
local function online()
    local net = Net.Mode()
    return net ~= nil and net.mode ~= "offline"
end
local function tell(entity, player)
    CallFunction("Net.Call", { rpc = "NetHealth", target = player, e1 = entity, n1 = health[entity] })
end
function Ready(self, dt)
    if not online() then return end
    CallFunction("Net.RegisterRpc", { name = "NetHealth", direction = "toClients", args = "entity,float(0..100000)" })
    CallFunction("Net.RegisterRpc", { name = "NetHealthOf", direction = "toServer", args = "entity", maxPerSecond = 120 })
end
function NetDamage(self, event)
    if not online() or not Net.IsServer() then return end
    local entity = event.args.entity
    if not Net.IsReplicated(entity) then return end
    health[entity] = math.max(0.0, (health[entity] or self.variables.maxHealth) - event.args.amount)
    Log("health of " .. World.Name(entity) .. " is " .. math.floor(health[entity]))
    tell(entity, 0)
end
function OnRpc_NetHealthOf(self, event)
    local entity = event.args.e1
    health[entity] = health[entity] or self.variables.maxHealth
    tell(entity, event.args.sender)
end
function OnNetSpawned(self, event)
    if event.args.owner == 0 then return end
    owners[event.args.entity] = event.args.owner
    if event.args.owner == Net.LocalPlayerId() then mine = event.args.entity end
    CallFunction("Net.Call", { rpc = "NetHealthOf", e1 = event.args.entity })
end
function OnNetDespawned(self, event)
    owners[event.args.entity] = nil
    if mine == event.args.entity then mine = 0 end
end
function OnRpc_NetHealth(self, event)
    local value = math.floor(event.args.n1)
    Log("player " .. (owners[event.args.e1] or 0) .. " health " .. value)
    if event.args.e1 == mine then
        self:SetProperty("Text", "content", self.variables.label .. ": " .. value)
    end
end
--- NetHazard
-- Whoever of `who` touches this object on the server is hurt: it sends the event NetDamage { entity, amount }, which NetHealth answers.
-- @expose who String = "NetAvatar"
-- @expose damage Float = 10.0
function OnTriggerEnter(self, event)
    local net = Net.Mode()
    if net == nil or net.mode == "client" then return end
    if World.Name(event.args.other) ~= self.variables.who then return end
    Emit("NetDamage", { entity = event.args.other, amount = self.variables.damage })
end
)lua",
        false, "net check --press 1:W@40+40 (the server log says 'health of NetAvatar is 90' and every client's log 'player 1 health 90'); to hurt from a script of your own: Emit(NetDamage, { entity = <character>, amount = 10 })" },
    Recipe{ "net-lobby", Kind::System, "A lobby with a Start button", "A waiting screen that counts the players who joined and a Start button only the first player can press; the game begins for everyone when it is pressed.",
        "+lobby +poczekalnia +host +gospodarz +multiplayer +online +network waiting room start button begin ready players join wait przycisk zaczac gotowi gracze czekac sala oczekiwanie",
        "LobbyStart:Button", "minPlayers=2",
        R"(
component set --entity {target} --type Text --set content=Start --set fontSize=24
component set --entity {target} --type Selectable --set eventName=StartClicked
@place {target} bottom-center 200 50
entity add --create Text --name LobbyText
component set --entity LobbyText --type Text --set 'content=Waiting for players' --set fontSize=28
@place LobbyText top-center 500 50
attach NetLobby
)",
        R"lua(
--- NetLobby
-- Every player sees how many have joined (a player joins when its connection works); the first to join is the host and is the only one who can press Start, once minPlayers have joined.
-- The server then sends the event NetGameStarted (the characters of net-players come with it) and tells every player the game began.
-- Single-player, Start begins the game at once.
-- @expose minPlayers Int = 2
local players = {}
local began = false
local asked = false
local function online()
    local net = Net.Mode()
    return net ~= nil and net.mode ~= "offline"
end
local function tell(player, host)
    CallFunction("Net.Call", { rpc = "NetLobby", target = player, n1 = #players, b1 = began, b2 = host })
end
local function tellAll()
    for index, player in ipairs(players) do tell(player, index == 1) end
end
local function say(text)
    local label = World.FindByName("LobbyText")
    if label and label ~= 0 then World.SetPropertyString(label, "Text", "content", text) end
end
function Ready(self, dt)
    if not online() then
        say("Single-player: press Start")
        return
    end
    CallFunction("Net.RegisterRpc", { name = "NetJoin", direction = "toServer", maxPerSecond = 5 })
    CallFunction("Net.RegisterRpc", { name = "NetStart", direction = "toServer", maxPerSecond = 5 })
    CallFunction("Net.RegisterRpc", { name = "NetLobby", direction = "toClients", args = "uint(0..64),bool,bool" })
end
function Tick(self, dt)
    if asked or not online() or Net.IsServer() then return end
    if Net.Mode().connected then
        asked = CallFunction("Net.Call", { rpc = "NetJoin" }) ~= nil
    end
end
function OnRpc_NetJoin(self, event)
    for _, player in ipairs(players) do
        if player == event.args.sender then return end
    end
    players[#players + 1] = event.args.sender
    tellAll()
end
function OnNetDisconnected(self, event)
    asked = false
    if not Net.IsServer() then return end
    for index, player in ipairs(players) do
        if player == event.args.player then table.remove(players, index) break end
    end
    tellAll()
end
function StartClicked(self, event)
    if not online() then
        began = true
        Emit("NetGameStarted", {})
        World.SetActive(self.entity, false)
        say("")
        return
    end
    CallFunction("Net.Call", { rpc = "NetStart" })
end
function OnRpc_NetStart(self, event)
    if began then return end
    if event.args.sender ~= players[1] then
        Log("start refused: only the host (player " .. tostring(players[1]) .. ") can start")
    elseif #players < self.variables.minPlayers then
        Log("start refused: " .. #players .. " of " .. self.variables.minPlayers .. " players have joined")
    else
        began = true
        Log("game started with " .. #players .. " players")
        Emit("NetGameStarted", {})
        tellAll()
    end
end
function OnRpc_NetLobby(self, event)
    local joined, started, host = event.args.n1, event.args.b1, event.args.b2
    World.SetPropertyBool(self.entity, "Selectable", "interactable", host and not started)
    if started then
        say("")
        World.SetActive(self.entity, false)
        Log("the game began")
    else
        say(joined .. " player(s) joined" .. (host and ": press Start" or ": waiting for the host"))
        Log("lobby: " .. joined .. " joined")
    end
end
)lua",
        false, "net check --clients 2 --click 1:Canvas/LobbyStart@40 --press 1:W@80+60 (the log says 'game started with 2 players'; the characters come then)" },
    Recipe{ "net-chat", Kind::System, "A chat between the players", "A line to type and a Send button; what a player sends appears in the chat of every player, with the number of the player who wrote it.",
        "+chat +czat +multiplayer +online +network message messages text talk say send type write players room wiadomosc wiadomosci pisac rozmowa wysylac gracze pokoj",
        "ChatSend:Button", "lines=6",
        R"(
component set --entity {target} --type Text --set content=Send --set fontSize=20
component set --entity {target} --type Selectable --set eventName=SendClicked
@place {target} bottom-right 100 36
entity add --create 'Input Field' --name ChatInput
@place ChatInput bottom-left 400 36
entity add --create Text --name ChatLog
component set --entity ChatLog --type Text --set content= --set fontSize=18
@place ChatLog top-left 500 160
attach NetChat
)",
        R"lua(
--- NetChat
-- The Send button sends the text of the line ChatInput; the server sends it on to every player and the last `lines` lines show in ChatLog.
-- Single-player, the line only shows here.
-- @expose lines Int = 6
local shown = {}
local function online()
    local net = Net.Mode()
    return net ~= nil and net.mode ~= "offline"
end
local function add(self, who, text)
    shown[#shown + 1] = who .. ": " .. text
    while #shown > self.variables.lines do table.remove(shown, 1) end
    local log = World.FindByName("ChatLog")
    if log and log ~= 0 then World.SetPropertyString(log, "Text", "content", table.concat(shown, "\n")) end
    Log("chat " .. who .. ": " .. text)
end
function Ready(self, dt)
    if not online() then return end
    CallFunction("Net.RegisterRpc", { name = "ChatSay", direction = "toServer", args = "string(120)", maxPerSecond = 5 })
    CallFunction("Net.RegisterRpc", { name = "ChatSaid", direction = "toClients", args = "uint(0..64),string(120)", maxPerSecond = 60 })
end
function SendClicked(self, event)
    local input = World.FindByName("ChatInput")
    if not input or input == 0 then return end
    local text = World.GetPropertyString(input, "Text", "content") or ""
    if text == "" then return end
    World.SetPropertyString(input, "Text", "content", "")
    if online() then
        CallFunction("Net.Call", { rpc = "ChatSay", s1 = string.sub(text, 1, 120) })
    else
        add(self, "you", text)
    end
end
function OnRpc_ChatSay(self, event)
    CallFunction("Net.Call", { rpc = "ChatSaid", target = 0, n1 = event.args.sender, s1 = event.args.s1 })
end
function OnRpc_ChatSaid(self, event)
    add(self, "player " .. event.args.n1, event.args.s1)
end
)lua",
        false, "net check --click 1:Canvas/ChatSend@30 after `component set --entity ChatInput --type Text --set content=hello` (every client's log says 'chat player 1: hello')" },
};

constexpr std::array kGames{
    Recipe{ "multiplayer-lan", Kind::Game, "Game for 2-4 players on a local network", "A coin-collecting game for several players in one room or house: each has a character, all share the coins and the score; the server runs on one computer of the network.",
        "+multiplayer +network +lan +coop +cooperative +kooperacja +kooperacyjna +wieloosobowa +siec +sieci +sieciowa +graczy +gracze wifi local room house home friends family split couch two four players game lokalna lokalnej domowa dwoch czterech znajomi rodzina",
        "", "",
        R"(
entity set --name 'Main Camera' --position 0,10,-9 --look-at 0,0,0
use net-players
use net-score label=Coins
use collectible name=Coin1 at=-3,1,2 who=NetAvatar item=Coin
use collectible name=Coin2 at=-1,1,2 who=NetAvatar item=Coin
use collectible name=Coin3 at=1,1,2 who=NetAvatar item=Coin
use collectible name=Coin4 at=3,1,2 who=NetAvatar item=Coin
use net-object target=Coin1
use net-object target=Coin2
use net-object target=Coin3
use net-object target=Coin4
)",
        "", false, "net check --clients 2 --press 1:W@40+40 (player 1 collects a coin: 'collected Coin' on the server, 'score is 1' on both clients); to play: kb_cli net setup --project <dir> --players 4 --address <the server's address>" },
    Recipe{ "multiplayer-online", Kind::Game, "Game for friends over the internet with a lobby", "The same coin game for players in different places: a lobby where the first to join starts the game, a chat, a server that runs on a machine the others can reach.",
        "+multiplayer +online +internet +network +coop +cooperative +kooperacja +wieloosobowa +graczy +gracze +siec +sieci +lobby remote friends distant world host start chat server players game znajomi zdalna gospodarz czat serwer",
        "", "",
        R"(
entity set --name 'Main Camera' --position 0,10,-9 --look-at 0,0,0
use net-lobby
use net-players waitForStart=true
use net-score label=Coins
use net-chat
use collectible name=Coin1 at=-3,1,2 who=NetAvatar item=Coin
use collectible name=Coin2 at=-1,1,2 who=NetAvatar item=Coin
use collectible name=Coin3 at=1,1,2 who=NetAvatar item=Coin
use collectible name=Coin4 at=3,1,2 who=NetAvatar item=Coin
use net-object target=Coin1
use net-object target=Coin2
use net-object target=Coin3
use net-object target=Coin4
)",
        "", false, "net check --clients 2 --click 1:Canvas/LobbyStart@40 --press 1:W@90+40 (the log says 'game started with 2 players', then the coin and the score); to play: kb_cli net setup --project <dir> --players 4 --address <the server's public address>, forward the UDP port on its router" },
};

} // namespace

std::span<const Recipe> NetworkBrickRecipes() {
    return kBricks;
}

std::span<const Recipe> NetworkSystemRecipes() {
    return kSystems;
}

std::span<const Recipe> NetworkGameRecipes() {
    return kGames;
}

} // namespace kb::cli::recipes
