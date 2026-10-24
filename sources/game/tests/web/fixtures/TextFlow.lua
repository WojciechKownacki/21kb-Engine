local entry = 0
local submitted = 0
local canceled = 0
local previous = ""
local frames = 0

local function encode(value)
    return '"' .. value:gsub('[%z\1-\31\\"]', function(character)
        return string.format('\\u%04x', string.byte(character))
    end) .. '"'
end

function Ready(self, dt)
    entry = World.FindByName("TextEntry")
    assert(entry ~= 0)
end

function OnUISubmitted(self, event)
    assert(event.args.entity == self.entity)
    submitted = submitted + 1
end

function OnUICanceled(self, event)
    assert(event.args.entity == self.entity)
    canceled = canceled + 1
end

function Tick(self, dt)
    frames = frames + 1
    local value, message = CallFunction("World.GetPropertyString", {
        entity = entry, component = "Text", property = "content"
    })
    assert(type(value) == "string" and message == nil, message or "Text property read failed")
    local state = string.format('{"text":%s,"focused":%d,"entry":%d,"submitted":%d,"canceled":%d,"hasFocus":%s,"ready":%s}',
        encode(value), UI.Focused(), entry, submitted, canceled, tostring(Input.HasFocus()), tostring(frames >= 8))
    if state ~= previous or frames % 8 == 0 then
        previous = state
        Log("text-flow " .. state)
    end
end
