local joystick = 0
local clicks = 0
local previous = ""
local knob = 0

local function writeFloat(entity, component, property, value)
    local set, message = CallFunction("World.SetPropertyFloat", {
        entity = entity, component = component, property = property, value = value
    })
    assert(set == true and message == nil, message or "UI property write failed")
end

function Ready(self, dt)
    knob = World.FindByName("JoystickKnob")
    assert(knob ~= 0 and UI.HasComponent(self.entity, "Button"))
    Log("touch-flow-ready")
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity, "UI callback reached the wrong entity")
    clicks = clicks + 1
    writeFloat(self.entity, "Border", "backgroundColor.r", clicks % 2 == 0 and 0.1 or 0.8)
    writeFloat(self.entity, "Border", "backgroundColor.b", clicks % 2 == 0 and 0.8 or 0.1)
    Log(string.format('touch-flow-click {"count":%d,"contacts":%d}', clicks, Input.TouchCount()))
end

function Tick(self, dt)
    local points = {}
    local encoded = {}
    local owner = nil
    for index = 0, Input.TouchCount() - 1 do
        local point = Input.TouchPoint(index)
        assert(point.valid, "Touch index inside the snapshot was invalid")
        points[#points + 1] = point
        encoded[#encoded + 1] = string.format(
            '{"id":%d,"x":%.2f,"y":%.2f,"phase":%d,"active":%s}',
            point.id, point.x, point.y, point.phase, tostring(point.active))
        if point.id == joystick and point.active then owner = point end
    end
    if owner == nil then joystick = 0 end
    if joystick == 0 then
        for _, point in ipairs(points) do
            if point.active and point.phase == 0 and point.x >= 40 and point.x <= 240 and
                point.y >= 80 and point.y <= 280 then
                joystick = point.id
                owner = point
                break
            end
        end
    end
    local x = owner and math.max(40, math.min(240, owner.x)) or 140
    local y = owner and math.max(80, math.min(280, owner.y)) or 180
    writeFloat(knob, "Rect Transform", "offsetMin.x", x - 40 - 15)
    writeFloat(knob, "Rect Transform", "offsetMin.y", y - 80 - 15)
    writeFloat(knob, "Rect Transform", "offsetMax.x", x - 40 + 15)
    writeFloat(knob, "Rect Transform", "offsetMax.y", y - 80 + 15)
    local state = string.format(
        '"joy":%d,"x":%.3f,"y":%.3f,"pressed":%d,"button":%d,"clicks":%d,"focus":%s,"points":[%s]',
        joystick, (x - 140) / 100, (y - 180) / 100, UI.Pressed(), self.entity,
        clicks, tostring(Input.HasFocus()), table.concat(encoded, ","))
    if state ~= previous then
        previous = state
        Log(string.format('touch-flow {"frame":%d,%s}', World.FrameIndex(), state))
    end
end
