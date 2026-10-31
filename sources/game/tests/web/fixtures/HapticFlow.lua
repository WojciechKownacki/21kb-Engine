local frames, sequence, previousJump = 0, 0, false
local applied = false

local function call(name, arguments)
    local result, diagnostic = CallFunction(name, arguments or {})
    assert(result ~= nil, diagnostic)
    return result
end

function Tick(self, dt)
    frames = frames + 1
    local jump = call("Input.Value", {action="Jump"}) > 0
    local move = call("Input.Vector2", {action="Move"})
    if jump and not previousJump then
        sequence = sequence + 1
        if move.x < -0.5 then
            applied = call("Input.StopVibration")
        elseif move.x > 0.5 then
            assert(call("Input.BindHapticsUser", {localUser=1, gamepadIndex=3}))
            applied = call("Input.SetUserVibration", {localUser=1, lowFrequency=0.2, highFrequency=0.7})
        else
            applied = call("Input.SetVibration", {gamepadIndex=0, lowFrequency=0.6, highFrequency=0.3})
        end
    end
    previousJump = jump
    local first = call("Input.HasHaptics", {gamepadIndex=0})
    local fourth = call("Input.HasHaptics", {gamepadIndex=3})
    if frames % 8 == 0 then
        Log("haptic-flow " .. string.format(
            '{"ready":true,"sequence":%d,"applied":%s,"connected":%s,"supported":%s,"fourth":%s}',
            sequence, tostring(applied), tostring(first.connected), tostring(first.supported), tostring(fourth.supported)))
    end
end
