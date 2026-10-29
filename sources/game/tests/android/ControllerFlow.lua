local frames = 0
local previous = ""
local fields = {"LX", "LY", "RX", "RY", "LT", "RT", "A", "B", "Up", "Down", "Left", "Right"}

local function call(name, args)
    local result, diagnostic = CallFunction(name, args)
    assert(result ~= nil, diagnostic)
    return result
end

function Tick(self, dt)
    frames = frames + 1
    local pads = {}
    for slot = 0, 3 do
        local values = {}
        for _, field in ipairs(fields) do
            values[#values + 1] = string.format('"%s":%.4f', field,
                call("Input.Value", {action="Pad" .. slot .. field}))
        end
        pads[#pads + 1] = string.format('{"connected":%s,%s}',
            tostring(call("Input.IsGamepadConnected", {gamepadIndex=slot})), table.concat(values, ","))
    end
    local state = string.format('{"ready":%s,"focus":%s,"pads":[%s]}',
        tostring(frames >= 8), tostring(Input.HasFocus()), table.concat(pads, ","))
    if state ~= previous or frames % 30 == 0 then
        previous = state
        Log("controller-flow " .. state)
    end
end
