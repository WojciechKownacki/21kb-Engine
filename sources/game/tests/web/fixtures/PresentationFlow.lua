local frames = 0
local totalX, totalY = 0, 0
local previous = ""

function Tick(self, dt)
    frames = frames + 1
    local delta = Pointer.Delta()
    totalX, totalY = totalX + delta.x, totalY + delta.y
    local position = Pointer.Position()
    local pressed = Pointer.Button(0)
    local state = string.format('{"ready":%s,"focus":%s,"x":%.4f,"y":%.4f,"dx":%.4f,"dy":%.4f,"totalX":%.4f,"totalY":%.4f,"pressed":%s}',
        tostring(frames >= 8), tostring(Input.HasFocus()), position.x, position.y,
        delta.x, delta.y, totalX, totalY, tostring(pressed))
    if state ~= previous or frames % 8 == 0 then
        previous = state
        Log("presentation-flow " .. state)
    end
end
