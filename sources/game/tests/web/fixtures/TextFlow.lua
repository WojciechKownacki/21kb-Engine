local entry = 0
local other = 0
local form = 0
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
    other = World.FindByName("OtherEntry")
    form = World.FindByName("Form")
    assert(entry ~= 0 and form ~= 0)
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
    local otherText = ""
    if other ~= 0 then
        otherText = CallFunction("World.GetPropertyString", {entity=other, component="Text", property="content"})
    end
    local scroll, scrollMessage = CallFunction("World.GetPropertyFloat", {
        entity = form, component = "Scroll View", property = "scrollY"
    })
    assert(type(scroll) == "number" and scrollMessage == nil, scrollMessage or "Scroll property read failed")
    local state = string.format('{"text":%s,"focused":%d,"entry":%d,"submitted":%d,"canceled":%d,"hasFocus":%s,"ready":%s,"scrollY":%.4f}',
        encode(value), UI.Focused(), entry, submitted, canceled, tostring(Input.HasFocus()), tostring(frames >= 8), scroll)
    state = state:sub(1, -2) .. string.format(',"other":%d,"otherText":%s}', other, encode(otherText))
    if state ~= previous or frames % 8 == 0 then
        previous = state
        Log("text-flow " .. state)
    end
end
