local frames, player, wall = 0, 0, 0
local loaded, written, hit = false, false, false
local saved = 0

local function call(name, args)
    local value, diagnostic = CallFunction(name, args or {})
    assert(value ~= nil, diagnostic)
    return value
end

function Ready(self, dt)
    player = World.FindByName("TemplatePlayer")
    wall = World.FindByName("QueryWall")
    assert(player ~= 0 and wall ~= 0)
    local result = call("Save.Read", {slot="workflow"})
    assert(result.loaded or result.status == "FileNotFound", result.diagnostic)
    loaded = result.loaded
    saved = call("Save.GetFloat", {key="position"}).value
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity)
    local position = call("Transform.GetPosition", {entity=player})
    assert(position.found and position.x > 0.2 and hit)
    saved = position.x
    assert(call("Save.SetFloat", {key="position", value=saved}))
    written = call("Save.Write", {slot="workflow"})
    assert(written and call("Save.Flush"))
end

function Tick(self, dt)
    frames = frames + 1
    local position = call("Transform.GetPosition", {entity=player})
    assert(position.found)
    local ray = call("Physics.Raycast", {originX=position.x, originY=position.y,
        originZ=position.z, directionX=0, directionY=0, directionZ=1, distance=5})
    hit = ray.hit and ray.entity == wall
    assert(call("World.SetPropertyFloat", {entity=self.entity, component="Border",
        property="backgroundColor.r", value=position.x > 0.2 and hit and 0.9 or 0.1}))
    if frames % 8 == 0 then
        Log("lua-workflow " .. string.format(
            '{"ready":true,"x":%.6f,"hit":%s,"loaded":%s,"saved":%.6f,"written":%s,"status":"%s"}',
            position.x, tostring(hit), tostring(loaded), saved, tostring(written),
            call("Save.StorageStatus").status))
    end
end
