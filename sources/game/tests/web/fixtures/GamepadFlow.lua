local frames = 0
local previous = ""

local function call(name, arguments)
    local result, diagnostic = CallFunction(name, arguments or {})
    assert(result ~= nil, diagnostic)
    return result
end

function Tick(self, dt)
    frames = frames + 1
    local move = call("Input.Vector2", {action="Move"})
    local jump = call("Input.Value", {action="Jump"})
    local first = call("Input.IsGamepadConnected", {gamepadIndex=0})
    local fourth = call("Input.IsGamepadConnected", {gamepadIndex=3})
    local set, message = CallFunction("World.SetPropertyFloat", {
        entity=self.entity, component="Border", property="backgroundColor.r",
        value=jump > 0 and 0.8 or 0.1
    })
    assert(set == true and message == nil, message)
    local state = string.format('{"ready":%s,"focus":%s,"first":%s,"fourth":%s,"x":%.4f,"y":%.4f,"jump":%.4f}',
        tostring(frames >= 8), tostring(Input.HasFocus()), tostring(first), tostring(fourth),
        move.x, move.y, jump)
    if state ~= previous or frames % 8 == 0 then
        previous = state
        Log("gamepad-flow " .. state)
    end
end
