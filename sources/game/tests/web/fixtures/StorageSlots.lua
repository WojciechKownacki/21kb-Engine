local first = false
local second = false
local firstValue = ""
local secondValue = ""
local slot = ""
local written = false
local started = false
local frames = 0
local previous = ""
local requested = false

local function call(name, arguments)
    local value, diagnostic = CallFunction(name, arguments or {})
    assert(value ~= nil, diagnostic)
    return value
end

local function encode(value)
    return '"' .. value:gsub('[%z\1-\31\\"]', function(character)
        return string.format('\\u%04x', string.byte(character))
    end) .. '"'
end

function Ready(self, dt)
    local one = call("Save.Read", {slot="slot_1"})
    assert(one.loaded or one.status == "FileNotFound", one.diagnostic)
    first = one.loaded
    if first then firstValue = call("Save.GetString", {key="value"}).value end
    local two = call("Save.Read", {slot="slot_2"})
    assert(two.loaded or two.status == "FileNotFound", two.diagnostic)
    second = two.loaded
    if second then secondValue = call("Save.GetString", {key="value"}).value end
end

function OnUISubmitted(self, event)
    assert(event.args.entity == self.entity)
    slot = call("World.GetPropertyString", {entity=self.entity, component="Text", property="content"})
    assert(slot == "slot_1" or slot == "slot_2")
    call("Save.Clear")
    call("Save.SetString", {key="value", value="value-" .. slot})
    written = call("Save.Write", {slot=slot})
    started = call("Save.Flush")
    requested = true
end

function Tick(self, dt)
    frames = frames + 1
    if requested and call("Save.StorageStatus").status == "Dirty" then
        started = call("Save.Flush") and started
    end
    local status = call("Save.StorageStatus")
    local state = string.format('{"ready":%s,"first":%s,"second":%s,"firstValue":%s,"secondValue":%s,"slot":%s,"written":%s,"started":%s,"status":%s,"diagnostic":%s}',
        tostring(frames >= 8), tostring(first), tostring(second), encode(firstValue), encode(secondValue),
        encode(slot), tostring(written), tostring(started), encode(status.status), encode(status.diagnostic))
    if state ~= previous or frames % 8 == 0 then
        previous = state
        Log("storage-flow " .. state)
    end
end
