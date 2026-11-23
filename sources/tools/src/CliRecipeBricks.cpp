#include "CliRecipes.hpp"

#include <array>

// The bricks: one small job each, with its Lua script. What they tell each other: the events ItemCollected {item, points},
// ObjectDied {name}, TimeUp, LevelComplete and SaveGame; and the shared values "has.<item>" (what the player holds), "score",
// "clicks" and "checkpoint.x|y|z".
namespace kb::cli::recipes {
namespace {

constexpr std::array kBricks{
    // ---- moving ----
    Recipe{ "walk", Kind::Brick, "Walk with the keys", "An object that walks where WASD or the arrows say, turns to face the way it goes and stops at walls.",
        "walk walking move moving controls keys wasd arrows keyboard character player chodzic chodzenie ruch poruszac sterowanie klawisze strzalki postac gracz",
        "Player:Capsule@0,1,0", "speed=5",
        R"(
input-action new --name Move --type Axis2D --key W=+y --key S=-y --key D=+x --key A=-x --key ArrowUp=+y --key ArrowDown=-y --key ArrowRight=+x --key ArrowLeft=-x
component add --entity {target} --type Rigidbody --if-missing
component set --entity {target} --type Rigidbody --set bodyType=Dynamic --set lockRotation=true
attach Walk
)",
        R"lua(
--- Walk
-- Walk with Move (WASD or the arrows) along the world axes; the object is a dynamic body that does not tip over.
-- @expose speed Float = 5.0
function Tick(self, dt)
    local move = Input.Vector2("Move")
    local x = move.x or 0.0
    local y = move.y or 0.0
    local speed = self.variables.speed
    local velocity = Physics.GetVelocity(self.entity)
    if not velocity then return end
    Physics.SetVelocity(self.entity, x * speed, velocity.y, y * speed)
    if x ~= 0.0 or y ~= 0.0 then
        Transform.SetRotation(self.entity, 0.0, math.deg(math.atan2(x, y)), 0.0)
    end
end
)lua",
        true, "run --frames 120 --press W@5+90 (the moved: line shows it walked)" },
    Recipe{ "jump", Kind::Brick, "Jump with Space", "An object that jumps when Space is pressed and it stands on something.",
        "jump jumping hop space spacebar press skok skakac skakanie skoczyc spacja spacji spacje nacisniecie nacisnieciu",
        "", "jumpSpeed=7",
        R"(
input-action new --name Jump --key Space
component add --entity {target} --type Rigidbody --if-missing
component set --entity {target} --type Rigidbody --set bodyType=Dynamic --set lockRotation=true
attach Jump
)",
        R"lua(
--- Jump
-- Jump when the Jump action is pressed and the object is not rising or falling (it stands on something).
-- @expose jumpSpeed Float = 7.0
function Tick(self, dt)
    if not Input.Pressed("Jump") then return end
    local velocity = Physics.GetVelocity(self.entity)
    if velocity and math.abs(velocity.y) < 0.1 then
        Physics.SetVelocity(self.entity, velocity.x, self.variables.jumpSpeed, velocity.z)
    end
end
)lua",
        true, "run --frames 120 --press Space@10 (the moved: line shows it went up)" },
    Recipe{ "camera-follow", Kind::Brick, "A camera that follows an object", "A camera that glides after an object at a distance and looks at it: behind it, above it or from the side.",
        "camera follow following chase view behind above track kamera sledzic sledzaca podazac widok za nad",
        "", "follow=Player|offsetX=0|offsetY=5|offsetZ=-8|smooth=6|lookAt=true",
        R"(
attach CameraFollow
)",
        R"lua(
--- CameraFollow
-- Glide after the object named in follow, keeping the offset, and look at it.
-- @expose follow String = "Player"
-- @expose offsetX Float = 0.0
-- @expose offsetY Float = 5.0
-- @expose offsetZ Float = -8.0
-- @expose smooth Float = 6.0
-- @expose lookAt Bool = true
function LateTick(self, dt)
    local v = self.variables
    local who = World.FindByName(v.follow)
    if not who or who == 0 then return end
    local p = Transform.GetPosition(who)
    local c = Transform.GetPosition(self.entity)
    if not p or not c then return end
    local k = math.min(1.0, v.smooth * dt)
    Transform.SetPosition(self.entity, c.x + (p.x + v.offsetX - c.x) * k, c.y + (p.y + v.offsetY - c.y) * k, c.z + (p.z + v.offsetZ - c.z) * k)
    if v.lookAt then
        Transform.LookAt(self.entity, p.x, p.y, p.z, 0.0, 1.0, 0.0)
    end
end
)lua",
        true, "run --frames 120 (the camera's moved: line shows it followed)" },
    Recipe{ "run-forward", Kind::Brick, "Run forward on its own", "An object that runs forward by itself and steers left and right with A and D within the width of the road.",
        "run running runner endless forward automatic lanes steer road bieg biegnie bieg w przod automatycznie nieskonczony pas",
        "Runner:Capsule@0,1,0", "speed=8|sideSpeed=6|halfWidth=4",
        R"(
input-action new --name Move --type Axis2D --key W=+y --key S=-y --key D=+x --key A=-x --key ArrowUp=+y --key ArrowDown=-y --key ArrowRight=+x --key ArrowLeft=-x
component add --entity {target} --type Rigidbody --if-missing
component set --entity {target} --type Rigidbody --set bodyType=Dynamic --set lockRotation=true
attach RunForward
)",
        R"lua(
--- RunForward
-- Run along +Z by itself; Move steers sideways, not past halfWidth either way.
-- @expose speed Float = 8.0
-- @expose sideSpeed Float = 6.0
-- @expose halfWidth Float = 4.0
function Tick(self, dt)
    local v = self.variables
    local move = Input.Vector2("Move")
    local p = Transform.GetPosition(self.entity)
    if not p then return end
    local side = (move.x or 0.0) * v.sideSpeed
    if (p.x >= v.halfWidth and side > 0.0) or (p.x <= -v.halfWidth and side < 0.0) then side = 0.0 end
    local velocity = Physics.GetVelocity(self.entity)
    if velocity then Physics.SetVelocity(self.entity, side, velocity.y, v.speed) end
end
)lua",
        true, "run --frames 120 (the moved: line shows it ran forward)" },
    Recipe{ "drive-car", Kind::Brick, "Drive a car", "An object that drives: W and S speed up and brake, A and D steer, and it coasts to a stop.",
        "car drive driving vehicle steer accelerate brake racing samochod jezdzic prowadzic pojazd kierownica gaz hamulec wyscig",
        "Car:Cube@0,0.5,0#C0392B*2,1,4", "maxSpeed=15|acceleration=8|turnSpeed=90",
        R"(
input-action new --name Move --type Axis2D --key W=+y --key S=-y --key D=+x --key A=-x --key ArrowUp=+y --key ArrowDown=-y --key ArrowRight=+x --key ArrowLeft=-x
attach DriveCar
)",
        R"lua(
--- DriveCar
-- W and S speed up and brake along the way the object faces, A and D turn it; it rolls to a stop without a key.
-- @expose maxSpeed Float = 15.0
-- @expose acceleration Float = 8.0
-- @expose turnSpeed Float = 90.0
local speeds = {}
function Tick(self, dt)
    local v = self.variables
    local move = Input.Vector2("Move")
    local s = speeds[self.entity] or 0.0
    local throttle = move.y or 0.0
    if throttle ~= 0.0 then
        s = s + throttle * v.acceleration * dt
    else
        s = s - s * math.min(1.0, 2.0 * dt)
    end
    s = math.max(-v.maxSpeed * 0.4, math.min(v.maxSpeed, s))
    speeds[self.entity] = s
    local steer = (move.x or 0.0) * v.turnSpeed * dt * math.min(1.0, math.abs(s) / 4.0)
    if s < 0.0 then steer = -steer end
    Transform.Rotate(self.entity, 0.0, steer, 0.0)
    local origin = Transform.TransformPoint(self.entity, 0.0, 0.0, 0.0)
    local ahead = Transform.TransformPoint(self.entity, 0.0, 0.0, 1.0)
    local dx, dz = ahead.x - origin.x, ahead.z - origin.z
    local length = math.sqrt(dx * dx + dz * dz)
    if length > 0.0 then
        Transform.Translate(self.entity, dx / length * s * dt, 0.0, dz / length * s * dt)
    end
end
)lua",
        true, "run --frames 120 --press W@5+90 (the moved: line shows it drove)" },
    Recipe{ "patrol", Kind::Brick, "Walk back and forth", "An object that moves back and forth along one axis by itself: a guard on its round, a moving platform, a sliding wall.",
        "patrol back forth moving platform guard slide shuttle oscillate wander patrolowac tam i z powrotem ruchoma platforma straznik krazyc",
        "Guard:Cube@6,1,0#8E44AD", "distance=4|speed=2|axis=x",
        R"(
attach Patrol
)",
        R"lua(
--- Patrol
-- Move along one axis (x, y or z) up to distance either side of where it started, and turn round there.
-- @expose distance Float = 4.0
-- @expose speed Float = 2.0
-- @expose axis String = "x"
local states = {}
function Tick(self, dt)
    local v = self.variables
    local p = Transform.GetPosition(self.entity)
    if not p then return end
    local s = states[self.entity]
    if not s then
        s = { x = p.x, y = p.y, z = p.z, dir = 1.0 }
        states[self.entity] = s
    end
    local step = s.dir * v.speed * dt
    local offset
    if v.axis == "y" then offset = p.y - s.y elseif v.axis == "z" then offset = p.z - s.z else offset = p.x - s.x end
    if (s.dir > 0.0 and offset + step >= v.distance) or (s.dir < 0.0 and offset + step <= -v.distance) then
        s.dir = -s.dir
        step = -step
    end
    if v.axis == "y" then Transform.Translate(self.entity, 0.0, step, 0.0)
    elseif v.axis == "z" then Transform.Translate(self.entity, 0.0, 0.0, step)
    else Transform.Translate(self.entity, step, 0.0, 0.0) end
end
)lua",
        true, "run --frames 120 (the moved: line shows it went)" },

    Recipe{ "spin", Kind::Brick, "Spin around", "An object that turns round and round by itself around one axis (a crate, a pickup, a fan, a sign), keeping everything else it has.",
        "spin spinning rotate rotating turn turning revolve carousel fan wheel around axis obracac obraca obrot krecic kreci wirowac wiruje kolo wentylator",
        "Crate:Cube@0,1,0#C0392B", "speed=45|axis=y",
        R"(
attach Spin
)",
        R"lua(
--- Spin
-- Turn around one axis (x, y or z) by speed degrees every second.
-- @expose speed Float = 45.0
-- @expose axis String = "y"
local turned = {}
function Tick(self, dt)
    local v = self.variables
    local turn = v.speed * dt
    local before = turned[self.entity] or 0.0
    turned[self.entity] = before + math.abs(turn)
    if before < 90.0 and turned[self.entity] >= 90.0 then Log(World.Name(self.entity) .. " turned a quarter") end
    if v.axis == "x" then Transform.Rotate(self.entity, turn, 0.0, 0.0)
    elseif v.axis == "z" then Transform.Rotate(self.entity, 0.0, 0.0, turn)
    else Transform.Rotate(self.entity, 0.0, turn, 0.0) end
end
)lua",
        true, "run --frames 120 (0 diagnostics; the object turns)" },

    // ---- life and fighting ----
    Recipe{ "health", Kind::Brick, "Health and dying", "Gives an object health that damage takes away; when it reaches zero the object dies and everything is told (ObjectDied).",
        "health life lives hit points hp damage die dies dying death dead alive combatant zycie zdrowie punkty zycia obrazenia smierc umrzec umiera ginac zginac",
        "", "maxHealth=100|team=0|destroyOnDeath=false|points=0",
        R"(
component add --entity {target} --type Combatant --if-missing --set maximumHealth={maxHealth} --set health={maxHealth} --set team={team}
attach Health
)",
        R"lua(
--- Health
-- The Combatant holds the health; this tells the game when the object dies.
-- @expose destroyOnDeath Bool = false
-- @expose points Int = 0
function OnDeath(self, event)
    local name = World.Name(self.entity)
    Log(name .. " died")
    Emit("ObjectDied", { name = name })
    if self.variables.points > 0 then
        Emit("ItemCollected", { item = name, points = self.variables.points })
    end
    if self.variables.destroyOnDeath then
        World.Destroy(self.entity, true)
    end
end
)lua",
        true, "run --frames 60 (0 diagnostics); the log says '<name> died' when its health runs out" },
    Recipe{ "health-hud", Kind::Brick, "Health on the screen", "A line of text in the top left corner that shows the health of an object: HP 80 / 100.",
        "health bar hud screen text hp display show life lives pasek zycia ekran wyswietlac zdrowie napis",
        "HealthText:Text", "of=Player|label=HP",
        R"(
@new component set --entity {target} --type Text --set 'content={label}: ...' --set fontSize=28
@new @place {target} top-left 300 40
attach HealthHud
)",
        R"lua(
--- HealthHud
-- Show "label: health / maximum" of the object named in of, on this Text.
-- @expose of String = "Player"
-- @expose label String = "HP"
local shown = {}
function Tick(self, dt)
    local v = self.variables
    local who = World.FindByName(v.of)
    if not who or who == 0 then return end
    local h = Gameplay.Health(who)
    if not h or not h.found then return end
    local text = v.label .. ": " .. math.floor(h.health + 0.5) .. " / " .. math.floor(h.maximumHealth + 0.5)
    if shown[self.entity] ~= text then
        shown[self.entity] = text
        self:SetProperty("Text", "content", text)
    end
end
)lua",
        true, "run --frames 30 (0 diagnostics); entity show --name <the text> shows the content" },
    Recipe{ "hurt-on-touch", Kind::Brick, "Hurts what touches it", "Makes an object a danger zone: whoever has health and steps in it loses some at once and again every second.",
        "hurt hazard danger damage touch spikes lava trap fire kill zone dangerous ranic niebezpieczny pulapka kolce lawa ogien obrazenia dotyk",
        "Hazard:Cube@4,0.2,0#C0392B*2,0.4,2", "damage=10|everySeconds=1",
        R"(
component add --entity {target} --type Collider --if-missing
component set --entity {target} --type Collider --set trigger=true
attach Hurt
)",
        R"lua(
--- Hurt
-- Whatever with a Combatant is inside this zone loses damage at once and every everySeconds after.
-- @expose damage Float = 10.0
-- @expose everySeconds Float = 1.0
local nextHit = {}
local function hurt(self, event)
    local other = event.args.other
    local now = Time.Elapsed()
    local key = self.entity .. ":" .. other
    if (nextHit[key] or 0.0) > now then return end
    nextHit[key] = now + self.variables.everySeconds
    local result = Gameplay.ApplyDamage(other, self.variables.damage, "Physical", self.entity)
    if result and result.applied then Log(World.Name(other) .. " hurt by " .. World.Name(self.entity)) end
end
function OnTriggerEnter(self, event) hurt(self, event) end
function OnTriggerStay(self, event) hurt(self, event) end
)lua",
        true, "run --frames 120 (0 diagnostics); the log says '<name> hurt by <zone>' when something with health is in it" },
    Recipe{ "chase", Kind::Brick, "Chase an object", "An object that runs after another one (the player) when it is near, and hits it when it gets there.",
        "chase chasing follow pursue hunt run after goni gonic scigac poscig podazac biegnie",
        "Enemy:Capsule@8,1,8#27AE60", "follow=Player|speed=3|range=25|stopAt=1.5|damage=0|cooldown=1",
        R"(
component add --entity {target} --type Rigidbody --if-missing
component set --entity {target} --type Rigidbody --set bodyType=Dynamic --set lockRotation=true
attach Chase
)",
        R"lua(
--- Chase
-- Walk after the object named in follow while it is within range; within stopAt stand and hit it for damage every cooldown seconds.
-- @expose follow String = "Player"
-- @expose speed Float = 3.0
-- @expose range Float = 25.0
-- @expose stopAt Float = 1.5
-- @expose damage Float = 0.0
-- @expose cooldown Float = 1.0
local waits = {}
function Tick(self, dt)
    local v = self.variables
    local who = World.FindByName(v.follow)
    if not who or who == 0 then return end
    local me = Transform.GetPosition(self.entity)
    local p = Transform.GetPosition(who)
    if not me or not p then return end
    local dx, dz = p.x - me.x, p.z - me.z
    local distance = math.sqrt(dx * dx + dz * dz)
    local wait = (waits[self.entity] or 0.0) - dt
    local velocity = Physics.GetVelocity(self.entity)
    if not velocity then return end
    if distance > v.range then
        Physics.SetVelocity(self.entity, 0.0, velocity.y, 0.0)
    elseif distance > v.stopAt then
        Physics.SetVelocity(self.entity, dx / distance * v.speed, velocity.y, dz / distance * v.speed)
        Transform.SetRotation(self.entity, 0.0, math.deg(math.atan2(dx, dz)), 0.0)
    else
        Physics.SetVelocity(self.entity, 0.0, velocity.y, 0.0)
        if v.damage > 0.0 and wait <= 0.0 then
            Gameplay.ApplyDamage(who, v.damage, "Physical", self.entity)
            wait = v.cooldown
        end
    end
    waits[self.entity] = wait
end
)lua",
        true, "run --frames 120 (the moved: line shows it went after the player)" },
    Recipe{ "tag-object", Kind::Brick, "Mark an object with a tag", "Gives an object a tag (Enemy) that towers and weapons look for.",
        "tag mark label group enemy class find znacznik oznaczyc etykieta grupa wrog klasa",
        "", "tag=Enemy",
        R"(
attach TagObject
)",
        R"lua(
--- TagObject
-- Give this object the tag when the scene starts.
-- @expose tag String = "Enemy"
function Ready(self)
    World.DefineTag(self.variables.tag)
    World.SetTag(self.entity, self.variables.tag)
end
)lua",
        true, "run --frames 10 (0 diagnostics)" },
    Recipe{ "melee-attack", Kind::Brick, "Hit with a sword", "On a key press (F or the left mouse button) hits the nearest tagged object within reach with a sword, fist or claw.",
        "melee attack hit sword fist swing slash punch fight close combat uderzyc uderzenie miecz piesc walka wrecz atak",
        "", "tag=Enemy|reach=2.5|damage=25|cooldown=0.5",
        R"(
input-action new --name Fire --key MouseLeft --key F
attach Melee
)",
        R"lua(
--- Melee
-- Fire hits the nearest object with the tag that is within reach, for damage; one hit per cooldown seconds.
-- @expose tag String = "Enemy"
-- @expose reach Float = 2.5
-- @expose damage Float = 25.0
-- @expose cooldown Float = 0.5
local waits = {}
function Tick(self, dt)
    local v = self.variables
    local wait = (waits[self.entity] or 0.0) - dt
    waits[self.entity] = wait
    if wait > 0.0 or not Input.Pressed("Fire") then return end
    local me = Transform.GetPosition(self.entity)
    if not me then return end
    local best, bestDistance
    for skip = 0, 63 do
        local e = World.FindAllByTag(v.tag, skip)
        if not e or e == 0 or not World.Exists(e) then break end
        local p = Transform.GetPosition(e)
        if p then
            local d = math.sqrt((p.x - me.x) ^ 2 + (p.y - me.y) ^ 2 + (p.z - me.z) ^ 2)
            if d <= v.reach and (not bestDistance or d < bestDistance) then best, bestDistance = e, d end
        end
    end
    if best then
        Gameplay.ApplyDamage(best, v.damage, "Physical", self.entity)
        waits[self.entity] = v.cooldown
        Log("hit " .. World.Name(best))
    end
end
)lua",
        true, "run --frames 60 --press F@10 (the log says 'hit <name>' for a tagged object within reach)" },
    Recipe{ "shoot", Kind::Brick, "Shoot a gun", "On a key press (the left mouse button or F) fires a shot straight ahead and hurts what it hits.",
        "shoot shooting gun fire weapon rifle pistol bullet hitscan aim strzelac strzal bron karabin pistolet pocisk celowac",
        "", "damage=25|range=60",
        R"(
input-action new --name Fire --key MouseLeft --key F
attach Shoot
)",
        R"lua(
--- Shoot
-- Fire sends a ray from this object straight ahead and hurts the first thing it hits that has health.
-- @expose damage Float = 25.0
-- @expose range Float = 60.0
function Tick(self, dt)
    if not Input.Pressed("Fire") then return end
    local v = self.variables
    local origin = Transform.TransformPoint(self.entity, 0.0, 0.0, 0.0)
    local ahead = Transform.TransformPoint(self.entity, 0.0, 0.0, 1.0)
    local dx, dy, dz = ahead.x - origin.x, ahead.y - origin.y, ahead.z - origin.z
    local hit = Physics.Raycast(ahead.x, ahead.y, ahead.z, dx, dy, dz, v.range)
    if hit and hit.hit then
        local result = Gameplay.ApplyDamage(hit.entity, v.damage, "Physical", self.entity)
        if result and result.applied then Log("shot " .. World.Name(hit.entity)) end
    end
end
)lua",
        true, "run --frames 60 --press F@10 (the log says 'shot <name>' when it hits something with health)" },
    Recipe{ "turret", Kind::Brick, "A tower that shoots", "An object that turns to the nearest tagged enemy in range and hurts it every second: a tower, a gun emplacement.",
        "turret tower defense defence auto aim gun emplacement cannon shoots enemies automatically wieza obrona dzialko automatycznie strzela wrogow",
        "Tower:Cylinder@6,1,6#7F8C8D*1,2,1", "tag=Enemy|range=10|damage=10|cooldown=1",
        R"(
attach Turret
)",
        R"lua(
--- Turret
-- Every cooldown seconds hurt the nearest object with the tag within range, and face it.
-- @expose tag String = "Enemy"
-- @expose range Float = 10.0
-- @expose damage Float = 10.0
-- @expose cooldown Float = 1.0
local waits = {}
function Tick(self, dt)
    local v = self.variables
    local wait = (waits[self.entity] or 0.0) - dt
    local me = Transform.GetPosition(self.entity)
    if not me then return end
    local best, bestDistance, bestPosition
    for skip = 0, 63 do
        local e = World.FindAllByTag(v.tag, skip)
        if not e or e == 0 or not World.Exists(e) then break end
        local p = Transform.GetPosition(e)
        if p then
            local d = math.sqrt((p.x - me.x) ^ 2 + (p.y - me.y) ^ 2 + (p.z - me.z) ^ 2)
            if d <= v.range and (not bestDistance or d < bestDistance) then best, bestDistance, bestPosition = e, d, p end
        end
    end
    if best then
        Transform.LookAt(self.entity, bestPosition.x, me.y, bestPosition.z, 0.0, 1.0, 0.0)
        if wait <= 0.0 then
            Gameplay.ApplyDamage(best, v.damage, "Physical", self.entity)
            wait = v.cooldown
            Log(World.Name(self.entity) .. " shot " .. World.Name(best))
        end
    end
    waits[self.entity] = wait
end
)lua",
        true, "run --frames 120 (0 diagnostics); the log says '<tower> shot <enemy>' for an enemy in range" },

    // ---- things to collect and use ----
    Recipe{ "collectible", Kind::Brick, "Collect it by touching", "Makes an object something the player picks up by touching: it vanishes, tells the game (ItemCollected) and is remembered as held.",
        "collect collectible pick up pickup item gem coin key potion touch grab take zbierac zebrac podniesc moneta klucz przedmiot dotknac",
        "Gem:Sphere@0,1,4#F5B921*0.5,0.5,0.5", "who=Player|item=Gem|points=1",
        R"(
component add --entity {target} --type Collider --if-missing
component set --entity {target} --type Collider --set trigger=true
@new component set --entity {target} --type Collider --set radius=1.5
attach Collect
)",
        R"lua(
--- Collect
-- When the object named in who touches this one: tell the game, count it as held (has.<item>) and take it away.
-- @expose who String = "Player"
-- @expose item String = "Gem"
-- @expose points Int = 1
function OnTriggerEnter(self, event)
    local v = self.variables
    if World.Name(event.args.other) ~= v.who then return end
    Log("collected " .. v.item)
    Emit("ItemCollected", { item = v.item, points = v.points })
    SetShared("has." .. v.item, (GetShared("has." .. v.item) or 0) + 1)
    World.Destroy(self.entity, true)
end
)lua",
        true, "run --frames 150 --press W@5+90 (the log says 'collected <item>')" },
    Recipe{ "heal-pickup", Kind::Brick, "A pickup that heals", "Makes an object a first aid kit: the player who touches it is healed and it is used up.",
        "heal healing medkit medicine first aid potion restore health pickup apteczka leczyc leczenie uleczyc mikstura zdrowie",
        "Medkit:Cube@-3,0.5,4#2ECC71*0.6,0.6,0.6", "who=Player|amount=30",
        R"(
component add --entity {target} --type Collider --if-missing
component set --entity {target} --type Collider --set trigger=true
attach HealPickup
)",
        R"lua(
--- HealPickup
-- When the object named in who touches this one, it gets amount of health back and this one is gone.
-- @expose who String = "Player"
-- @expose amount Float = 30.0
function OnTriggerEnter(self, event)
    local v = self.variables
    if World.Name(event.args.other) ~= v.who then return end
    Gameplay.Heal(event.args.other, v.amount)
    Log(v.who .. " healed")
    World.Destroy(self.entity, true)
end
)lua",
        true, "run --frames 60 (the log says '<who> healed')" },
    Recipe{ "bounce-pad", Kind::Brick, "A spring that launches", "Makes an object a spring: whatever steps on it is thrown up into the air.",
        "bounce pad spring trampoline launch jump boost throw up sprezyna trampolina odbijac wyrzucic podskok wysoko",
        "SpringPad:Cube@3,0.1,4#E67E22*2,0.2,2", "power=14",
        R"(
component add --entity {target} --type Collider --if-missing
component set --entity {target} --type Collider --set trigger=true
attach BouncePad
)",
        R"lua(
--- BouncePad
-- Whatever with a body steps on this is sent up at power.
-- @expose power Float = 14.0
function OnTriggerEnter(self, event)
    local other = event.args.other
    local velocity = Physics.GetVelocity(other)
    if velocity then
        Physics.SetVelocity(other, velocity.x, self.variables.power, velocity.z)
        Log(World.Name(other) .. " bounced")
    end
end
)lua",
        true, "run --frames 60 (the log says '<name> bounced' when something steps on it)" },
    Recipe{ "score-hud", Kind::Brick, "Score on the screen", "A line of text in the top right corner that adds up the points of everything collected or defeated.",
        "score points hud screen text count total counter display punkty wynik licznik ekran suma napis",
        "ScoreText:Text", "label=Score",
        R"(
@new component set --entity {target} --type Text --set 'content={label}: 0' --set fontSize=28
@new @place {target} top-right 300 40
attach ScoreHud
)",
        R"lua(
--- ScoreHud
-- Add the points of every ItemCollected and show "label: total"; the total is also the shared value "score".
-- @expose label String = "Score"
local shown = {}
local function refresh(self)
    local text = self.variables.label .. ": " .. (GetShared("score") or 0)
    if shown[self.entity] ~= text then
        shown[self.entity] = text
        self:SetProperty("Text", "content", text)
    end
end
function ItemCollected(self, event)
    SetShared("score", (GetShared("score") or 0) + (event.args.points or 1))
    refresh(self)
end
function Tick(self, dt) refresh(self) end
)lua",
        true, "run --frames 150 (0 diagnostics); the text shows the total after a pickup" },
    Recipe{ "inventory-hud", Kind::Brick, "What the player carries", "A line of text at the bottom left that lists what the player has picked up: Key x1, Potion x2.",
        "inventory backpack bag items carry carrying held list hud ekwipunek plecak torba przedmioty niesie lista",
        "InventoryText:Text", "label=Items",
        R"(
@new component set --entity {target} --type Text --set 'content={label}: none' --set fontSize=26
@new @place {target} bottom-left 500 40
attach InventoryHud
)",
        R"lua(
--- InventoryHud
-- List each item collected with how many: "label: Key x1, Potion x2".
-- @expose label String = "Items"
local order = {}
local counts = {}
function ItemCollected(self, event)
    local item = event.args.item or "Item"
    if not counts[item] then
        counts[item] = 0
        order[#order + 1] = item
    end
    counts[item] = counts[item] + 1
    local parts = {}
    for _, name in ipairs(order) do parts[#parts + 1] = name .. " x" .. counts[name] end
    self:SetProperty("Text", "content", self.variables.label .. ": " .. table.concat(parts, ", "))
end
)lua",
        true, "run --frames 150 (0 diagnostics); the text lists the items after a pickup" },
    Recipe{ "timer-hud", Kind::Brick, "A clock on the screen", "A clock at the top centre that counts down from a start (and says TimeUp at zero) or counts up.",
        "timer clock time countdown count down stopwatch seconds limit hud zegar czas odliczanie stoper sekundy limit czasu",
        "TimerText:Text", "seconds=60|countDown=true",
        R"(
@new component set --entity {target} --type Text --set 'content=Time' --set fontSize=32
@new @place {target} top-center 300 44
attach TimerHud
)",
        R"lua(
--- TimerHud
-- Count down from seconds (TimeUp at zero) or count up from zero, and show the whole seconds.
-- @expose seconds Float = 60.0
-- @expose countDown Bool = true
local clocks = {}
local done = {}
function Tick(self, dt)
    local v = self.variables
    local t = clocks[self.entity]
    if t == nil then t = v.countDown and v.seconds or 0.0 end
    if v.countDown then
        t = math.max(0.0, t - dt)
        if t == 0.0 and not done[self.entity] then
            done[self.entity] = true
            Log("time is up")
            Emit("TimeUp", {})
        end
    else
        t = t + dt
    end
    clocks[self.entity] = t
    self:SetProperty("Text", "content", "Time: " .. math.ceil(t))
end
)lua",
        true, "run --frames 120 (0 diagnostics)" },
    Recipe{ "door", Kind::Brick, "A door that opens", "An object that slides up out of the way when the player holds what it needs (a key, a pressed plate) and shuts again without it.",
        "door gate open opens key unlock locked wall slide barrier drzwi brama otwierac klucz zamek zamknieta odblokowac",
        "Door:Cube@4,1.5,0#8B5A2B*0.3,3,3", "needs=Key|height=3.5|speed=3",
        R"(
attach Door
)",
        R"lua(
--- Door
-- Slide up by height while the shared value has.<needs> is set (a key picked up, a plate pressed), back down when it is not.
-- @expose needs String = "Key"
-- @expose height Float = 3.5
-- @expose speed Float = 3.0
local closedAt = {}
local shown = {}
function Tick(self, dt)
    local v = self.variables
    local p = Transform.GetPosition(self.entity)
    if not p then return end
    local base = closedAt[self.entity]
    if base == nil then
        base = p.y
        closedAt[self.entity] = base
    end
    local held = GetShared("has." .. v.needs)
    local open = held ~= nil and held ~= false and held ~= 0
    if shown[self.entity] ~= open then
        shown[self.entity] = open
        Log(World.Name(self.entity) .. (open and " opens" or " shuts"))
    end
    local goal = open and (base + v.height) or base
    local step = v.speed * dt
    local y = p.y + math.max(-step, math.min(step, goal - p.y))
    if y ~= p.y then Transform.SetPosition(self.entity, p.x, y, p.z) end
end
)lua",
        true, "run --frames 150 (the log says '<door> opens' once the item is held)" },
    Recipe{ "pressure-plate", Kind::Brick, "A pressure plate", "A plate on the floor that is pressed while something stands on it (the player or a box) and lets go when it leaves.",
        "plate pressure button switch floor trigger step puzzle weight plyta nacisk przycisk przelacznik podloga zagadka waga",
        "Plate:Cylinder@-3,0.05,3#3498DB*1.5,0.1,1.5", "item=Plate|by=",
        R"(
component add --entity {target} --type Collider --if-missing
component set --entity {target} --type Collider --set trigger=true
attach Plate
)",
        R"lua(
--- Plate
-- While something (named in by, or anything when by is empty) is on it, the shared value has.<item> is 1, else 0.
-- @expose item String = "Plate"
-- @expose by String = ""
local inside = {}
local function set(self)
    local pressed = (inside[self.entity] or 0) > 0
    SetShared("has." .. self.variables.item, pressed and 1 or 0)
    Log(World.Name(self.entity) .. (pressed and " pressed" or " released"))
end
local function counts(self, event)
    local by = self.variables.by
    return by == "" or World.Name(event.args.other) == by
end
function OnTriggerEnter(self, event)
    if not counts(self, event) then return end
    inside[self.entity] = (inside[self.entity] or 0) + 1
    set(self)
end
function OnTriggerExit(self, event)
    if not counts(self, event) then return end
    inside[self.entity] = math.max(0, (inside[self.entity] or 0) - 1)
    set(self)
end
)lua",
        true, "run --frames 120 (the log says '<plate> pressed' and 'released')" },
    Recipe{ "checkpoint", Kind::Brick, "A checkpoint", "A spot that remembers where the player was last safe, so falling or dying puts it back there.",
        "checkpoint save spot flag respawn point safe restart punkt kontrolny flaga odrodzenie bezpieczny",
        "Checkpoint:Cylinder@8,0.1,0#2ECC71*2,0.2,2", "who=Player",
        R"(
component add --entity {target} --type Collider --if-missing
component set --entity {target} --type Collider --set trigger=true
attach Checkpoint
)",
        R"lua(
--- Checkpoint
-- When the object named in who touches this one, remember this place as the one to come back to.
-- @expose who String = "Player"
function OnTriggerEnter(self, event)
    if World.Name(event.args.other) ~= self.variables.who then return end
    local p = Transform.GetPosition(self.entity)
    SetShared("checkpoint.x", p.x)
    SetShared("checkpoint.y", p.y + 1.0)
    SetShared("checkpoint.z", p.z)
    Log("checkpoint reached")
end
)lua",
        true, "run --frames 150 --press W@5+90 (the log says 'checkpoint reached')" },
    Recipe{ "respawn", Kind::Brick, "Come back after falling or dying", "Puts an object back where it started, or at the last stop, when it falls off the world or dies, with its health back.",
        "respawn revive restart fall falls off world return reborn odrodzic wrocic spasc upadek ozywic odrodzenie",
        "", "fallLimit=-10|healthOnRespawn=100",
        R"(
attach Respawn
)",
        R"lua(
--- Respawn
-- Below fallLimit, or after ObjectDied for this object, go back to the last checkpoint (else the start) with health restored.
-- @expose fallLimit Float = -10.0
-- @expose healthOnRespawn Float = 100.0
local starts = {}
local function back(self)
    local x = GetShared("checkpoint.x")
    if x ~= nil then
        Transform.SetPosition(self.entity, x, GetShared("checkpoint.y"), GetShared("checkpoint.z"))
    elseif starts[self.entity] then
        local s = starts[self.entity]
        Transform.SetPosition(self.entity, s.x, s.y, s.z)
    end
end
function Ready(self)
    local p = Transform.GetPosition(self.entity)
    if p then starts[self.entity] = { x = p.x, y = p.y, z = p.z } end
end
function Tick(self, dt)
    local p = Transform.GetPosition(self.entity)
    if p and p.y < self.variables.fallLimit then
        back(self)
        Log(World.Name(self.entity) .. " is back")
    end
end
function ObjectDied(self, event)
    if event.args.name ~= World.Name(self.entity) then return end
    Gameplay.Revive(self.entity, self.variables.healthOnRespawn)
    back(self)
    Log(World.Name(self.entity) .. " is back")
end
)lua",
        true, "run --frames 120 (the log says '<name> is back' after a fall or a death)" },
    Recipe{ "level-exit", Kind::Brick, "The end of a level", "A goal the player reaches to finish the level: tells the game (LevelComplete) and can open the next scene.",
        "goal exit finish end level next win complete portal door flag cel koniec poziom nastepny wygrana ukonczyc meta portal",
        "Goal:Cube@0,1,15#F1C40F*3,2,0.5", "who=Player|scene=",
        R"(
component add --entity {target} --type Collider --if-missing
component set --entity {target} --type Collider --set trigger=true
attach LevelExit
)",
        R"lua(
--- LevelExit
-- When the object named in who touches this one, say LevelComplete and open the scene in scene (when it is set).
-- @expose who String = "Player"
-- @expose scene String = ""
function OnTriggerEnter(self, event)
    local v = self.variables
    if World.Name(event.args.other) ~= v.who then return end
    Log("level complete")
    Emit("LevelComplete", {})
    if v.scene ~= "" then Scene.Load(v.scene) end
end
)lua",
        true, "run --frames 150 --press W@5+90 (the log says 'level complete')" },
    Recipe{ "spawner", Kind::Brick, "Make copies over time", "An object that adds a copy of a prefab every few seconds (enemies, obstacles, pickups), near itself or ahead of an object.",
        "spawn spawner waves create enemies obstacles prefab copies repeat timer rain lawina fala wrogow tworzyc kopie przeszkody co kilka sekund",
        "Spawner@0,1,10", "prefab=/Game/Prefabs/Enemy.kbprefab|every=3|max=10|spread=4|follow=|ahead=20",
        R"(
attach Spawner
)",
        R"lua(
--- Spawner
-- Every `every` seconds add a copy of the prefab, up to max copies, within spread of this object; with follow set, ahead of that object.
-- @expose prefab String = "/Game/Prefabs/Enemy.kbprefab"
-- @expose every Float = 3.0
-- @expose max Int = 10
-- @expose spread Float = 4.0
-- @expose follow String = ""
-- @expose ahead Float = 20.0
local clocks = {}
local made = {}
function Tick(self, dt)
    local v = self.variables
    local t = (clocks[self.entity] or v.every) - dt
    if t > 0.0 or (made[self.entity] or 0) >= v.max then
        clocks[self.entity] = t
        return
    end
    clocks[self.entity] = v.every
    local at = Transform.GetPosition(self.entity)
    local z = 0.0
    if v.follow ~= "" then
        local who = World.FindByName(v.follow)
        if who and who ~= 0 then
            at = Transform.GetPosition(who) or at
            z = v.ahead
        end
    end
    if not at then return end
    local x = at.x + (math.random() - 0.5) * 2.0 * v.spread
    World.InstantiatePrefab(v.prefab, nil, nil, x, at.y, at.z + z)
    made[self.entity] = (made[self.entity] or 0) + 1
    Log("spawned copy " .. made[self.entity])
end
)lua",
        true, "run --frames 400 (the log says 'spawned copy 1', 2, ...)" },
    Recipe{ "dialog", Kind::Brick, "Talk to a character", "A character the player can talk to: press E close by and its lines appear one after another at the bottom of the screen.",
        "dialog dialogue talk conversation npc speak villager text lines say character rozmowa dialog postac mowic wioska tekst kwestie",
        "Villager:Capsule@5,1,5#E67E22", "who=Player|range=3|lines=Hello there!;Welcome to our village.;Good luck on your way!",
        R"(
input-action new --name Interact --key E
entity add --create Text --name {target}Dialog
component set --entity {target}Dialog --type Text --set 'content= ' --set fontSize=28
@place {target}Dialog bottom-center 800 60
attach Dialog
)",
        R"lua(
--- Dialog
-- Close to the object named in who, E shows the next line in the Text called <this object>Dialog; after the last it clears.
-- @expose who String = "Player"
-- @expose range Float = 3.0
-- @expose lines String = "Hello there!;Welcome to our village.;Good luck on your way!"
local positions = {}
function Tick(self, dt)
    local v = self.variables
    local box = World.FindByName(World.Name(self.entity) .. "Dialog")
    local who = World.FindByName(v.who)
    if not box or box == 0 or not who or who == 0 then return end
    local me = Transform.GetPosition(self.entity)
    local p = Transform.GetPosition(who)
    if not me or not p then return end
    local near = math.sqrt((p.x - me.x) ^ 2 + (p.z - me.z) ^ 2) <= v.range
    local at = positions[self.entity] or 0
    if not near then
        if at > 0 then
            positions[self.entity] = 0
            World.SetPropertyString(box, "Text", "content", " ")
        end
        return
    end
    if Input.Pressed("Interact") then
        local all = {}
        for line in string.gmatch(v.lines, "[^;]+") do all[#all + 1] = line end
        at = at + 1
        if at > #all then
            at = 0
            World.SetPropertyString(box, "Text", "content", " ")
        else
            World.SetPropertyString(box, "Text", "content", all[at])
            Log(World.Name(self.entity) .. " says: " .. all[at])
        end
        positions[self.entity] = at
    end
end
)lua",
        true, "run --frames 60 --press E@10 (the log says '<name> says: <line>')" },

    // ---- the screen, sound and keeping the game ----
    Recipe{ "click-counter", Kind::Brick, "Count clicks on a button", "A big button that adds to a count each time it is clicked, with the count shown above it: the heart of a clicker game.",
        "click clicker button count counter tap idle incremental klikac klikniecie przycisk licznik klikanie",
        "ClickButton:Button", "perClick=1|label=Clicks",
        R"(
@new component set --entity {target} --type Text --set 'content=Click!' --set fontSize=32
@new @place {target} center 260 70
entity add --create Text --name {target}Count
component set --entity {target}Count --type Text --set 'content={label}: 0' --set fontSize=44
@place {target}Count top-center 500 70
attach ClickAdd
attach ClickShow --node {target}Count
)",
        R"lua(
--- ClickAdd
-- Every click on this button adds perClick to the shared count "clicks".
-- @expose perClick Int = 1
function OnUIClicked(self, event)
    SetShared("clicks", (GetShared("clicks") or 0) + self.variables.perClick)
end
--- ClickShow
-- Show the shared count "clicks" on this Text.
-- @expose label String = "Clicks"
local shown = {}
function Tick(self, dt)
    local text = self.variables.label .. ": " .. math.floor(GetShared("clicks") or 0)
    if shown[self.entity] ~= text then
        shown[self.entity] = text
        self:SetProperty("Text", "content", text)
    end
end
)lua",
        true, "run --frames 60 --click Canvas/<button>@10 (the count text changes)" },
    Recipe{ "idle-income", Kind::Brick, "Earn while waiting", "Adds a number to the clicker's count every second by itself, even when nobody clicks.",
        "idle income passive auto earn per second automatic offline production pasywny dochod co sekunde automatycznie zarabiac",
        "IdleIncome", "perSecond=1",
        R"(
attach IdleIncome
)",
        R"lua(
--- IdleIncome
-- Add perSecond to the shared count "clicks" every second.
-- @expose perSecond Float = 1.0
local carried = {}
function Tick(self, dt)
    local total = (carried[self.entity] or 0.0) + self.variables.perSecond * dt
    local whole = math.floor(total)
    carried[self.entity] = total - whole
    if whole > 0 then SetShared("clicks", (GetShared("clicks") or 0) + whole) end
end
)lua",
        true, "run --frames 120 (0 diagnostics)" },
    Recipe{ "button-load-scene", Kind::Brick, "A button that opens a scene", "Makes a button open another scene when clicked: Play on a menu, Restart after losing, a level choice.",
        "button scene load open level play restart start menu click change przycisk scena wczytac otworzyc poziom graj od nowa menu klik",
        "", "scene=/Game/Scenes/Main.21kbscene",
        R"(
attach LoadScene
)",
        R"lua(
--- LoadScene
-- A click on this button opens the scene in scene (and lets time run again if the game was paused).
-- @expose scene String = "/Game/Scenes/Main.21kbscene"
function OnUIClicked(self, event)
    Time.Resume()
    Scene.Load(self.variables.scene)
end
)lua",
        true, "run --frames 12 --click <button path>@5 (the log shows the scene opening)" },
    Recipe{ "pause-toggle", Kind::Brick, "Pause the game", "Escape stops the game and shows Paused on the screen; Escape again goes on.",
        "pause paused stop freeze resume escape esc halt pauza zatrzymac wstrzymac wznowic",
        "PauseControl", "label=Paused",
        R"(
input-action new --name Pause --key Escape
entity add --create Text --name {target}Text
component set --entity {target}Text --type Text --set 'content={label}' --set fontSize=60
@place {target}Text center 600 100
attach PauseToggle
)",
        R"lua(
--- PauseToggle
-- Pause toggles the world's time and shows or hides the Text called <this object>Text.
-- @expose label String = "Paused"
local boxes = {}
local paused = {}
function Ready(self)
    local box = World.FindByName(World.Name(self.entity) .. "Text")
    boxes[self.entity] = box
    if box and box ~= 0 then World.SetActive(box, false) end
end
function Tick(self, dt)
    if not Input.Pressed("Pause") then return end
    local now = not paused[self.entity]
    paused[self.entity] = now
    if now then Time.Pause() else Time.Resume() end
    local box = boxes[self.entity]
    if box and box ~= 0 then World.SetActive(box, now) end
end
)lua",
        true, "run --frames 60 --press Escape@10 (0 diagnostics)" },
    Recipe{ "game-over-screen", Kind::Brick, "Game over", "When the player dies or the time is up, shows Game Over with a Restart button and stops the game.",
        "game over lose lost die dead end restart screen failed defeat koniec gry przegrana smierc ekran zacznij od nowa porazka",
        "GameOverControl", "who=Player|scene=/Game/Scenes/Main.21kbscene|label=Game Over",
        R"(
entity add --create Text --name {target}Text
component set --entity {target}Text --type Text --set 'content={label}' --set fontSize=72
@place {target}Text center 700 120
entity add --create Button --name {target}Restart
component set --entity {target}Restart --type Text --set 'content=Restart' --set fontSize=32
@place {target}Restart center 260 70
use button-load-scene target={target}Restart scene={scene}
attach GameOver
)",
        R"lua(
--- GameOver
-- When the object named in who dies, or the time is up, show the Text and the Restart button called <this object>Text and <this object>Restart.
-- @expose who String = "Player"
-- @expose label String = "Game Over"
local parts = {}
local function hide(self)
    local name = World.Name(self.entity)
    parts[self.entity] = { World.FindByName(name .. "Text"), World.FindByName(name .. "Restart") }
    for _, e in ipairs(parts[self.entity]) do
        if e and e ~= 0 then World.SetActive(e, false) end
    end
end
local function show(self)
    Log("game over")
    for _, e in ipairs(parts[self.entity] or {}) do
        if e and e ~= 0 then World.SetActive(e, true) end
    end
    Time.Pause()
end
function Ready(self) hide(self) end
function ObjectDied(self, event)
    if event.args.name == self.variables.who then show(self) end
end
function TimeUp(self, event) show(self) end
)lua",
        true, "run --frames 60 (0 diagnostics)" },
    Recipe{ "sound-collect", Kind::Brick, "A sound when collecting", "Plays a coin sound each time something is collected.",
        "sound audio effect collect coin pickup sfx plays dzwiek moneta zbieranie efekt dzwiekowy",
        "SoundCollect", "clip=/Game/Audio/Coin.wav",
        R"(
@once Assets/Audio/Coin.wav sound new --name Coin --preset coin
attach SoundCollect
)",
        R"lua(
--- SoundCollect
-- Play the clip whenever an item is collected.
-- @expose clip String = "/Game/Audio/Coin.wav"
function ItemCollected(self, event)
    Audio.Play(self.variables.clip, 1.0)
end
)lua",
        true, "run --frames 60 (0 diagnostics)" },
    Recipe{ "sound-hurt", Kind::Brick, "A sound when hurt", "Plays a hit sound each time the object takes damage.",
        "sound audio hurt hit damage ouch sfx plays dzwiek trafienie ranny obrazenia efekt dzwiekowy",
        "", "clip=/Game/Audio/Hit.wav",
        R"(
@once Assets/Audio/Hit.wav sound new --name Hit --preset hit
attach SoundHurt
)",
        R"lua(
--- SoundHurt
-- Play the clip whenever this object takes damage.
-- @expose clip String = "/Game/Audio/Hit.wav"
function OnDamaged(self, event)
    Audio.Play(self.variables.clip, 1.0)
end
)lua",
        true, "run --frames 60 (0 diagnostics)" },
    Recipe{ "music-loop", Kind::Brick, "Background music", "Plays a music loop all through the scene, quietly.",
        "music background loop song soundtrack ambient muzyka tlo petla melodia sciezka dzwiekowa",
        "MusicPlayer", "clip=/Game/Audio/Music.wav|volume=0.5",
        R"(
@once Assets/Audio/Music.wav sound new --name Music --preset music
attach MusicLoop
)",
        R"lua(
--- MusicLoop
-- Start the clip, looping, when the scene starts.
-- @expose clip String = "/Game/Audio/Music.wav"
-- @expose volume Float = 0.5
function Ready(self)
    Audio.Play(self.variables.clip, self.variables.volume, 1.0, false, true)
end
)lua",
        true, "run --frames 30 (0 diagnostics)" },
    Recipe{ "save-game", Kind::Brick, "Save and continue", "Keeps the object's place, the score and the click count between plays: saved every few seconds and on SaveGame, restored when the scene starts.",
        "save saving saved load progress continue slot store remember storage zapis zapisac zapisywanie zapisywac zapisz zapisany wczytac wczytanie postep kontynuacja pamietac",
        "SaveGame", "slot=game|every=5",
        R"(
attach SaveGame
)",
        R"lua(
--- SaveGame
-- Restore this object's place and the shared score and click count at the start; save them every `every` seconds and when SaveGame is emitted.
-- @expose slot String = "game"
-- @expose every Float = 5.0
local clocks = {}
local function key(self, part) return World.Name(self.entity) .. "." .. part end
local function write(self)
    local p = Transform.GetPosition(self.entity)
    if p then
        Save.SetFloat(key(self, "x"), p.x)
        Save.SetFloat(key(self, "y"), p.y)
        Save.SetFloat(key(self, "z"), p.z)
    end
    Save.SetInt("score", math.floor(GetShared("score") or 0))
    Save.SetInt("clicks", math.floor(GetShared("clicks") or 0))
    Save.Write(self.variables.slot)
end
function Ready(self)
    Save.Read(self.variables.slot)
    if Save.Has(key(self, "x")) then
        Log("restored " .. World.Name(self.entity))
        Transform.SetPosition(self.entity, Save.GetFloat(key(self, "x")).value, Save.GetFloat(key(self, "y")).value, Save.GetFloat(key(self, "z")).value)
    end
    if Save.Has("score") then SetShared("score", Save.GetInt("score").value) end
    if Save.Has("clicks") then SetShared("clicks", Save.GetInt("clicks").value) end
end
function Tick(self, dt)
    clocks[self.entity] = (clocks[self.entity] or 0.0) + dt
    if clocks[self.entity] >= self.variables.every then
        clocks[self.entity] = 0.0
        write(self)
    end
end
function SaveGame(self, event) write(self) end
)lua",
        true, "run --frames 400 twice: the second run says 'restored <name>' and starts where the first ended" },
};

} // namespace

std::span<const Recipe> BrickRecipes() {
    return kBricks;
}

} // namespace kb::cli::recipes
