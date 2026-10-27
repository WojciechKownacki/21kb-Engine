local frames = 0
local phase = 0
local previous = ""
local first, second, settings = false, false, false
local firstValue, secondValue, settingsValue = "", "", ""
local started, deleted, missing, refused = false, false, false, false

local function call(name, arguments)
    local result, diagnostic = CallFunction(name, arguments or {})
    assert(result ~= nil, diagnostic)
    return result
end

local function read(domain, slot)
    local result = call(domain .. ".Read", {slot=slot})
    assert(result.loaded or result.status == "FileNotFound", result.diagnostic)
    return result.loaded, result.loaded and call(domain .. ".GetString", {key="value"}).value or ""
end

local function refresh()
    first, firstValue = read("Save", "slot_1")
    second, secondValue = read("Save", "slot_2")
    settings, settingsValue = read("Settings", "slot_1")
end

local function write(domain, slot, value)
    call(domain .. ".Clear")
    call(domain .. ".SetString", {key="value", value=value})
    assert(call(domain .. ".Write", {slot=slot}))
end

local function report()
    local result = call("Save.StorageStatus")
    local state = string.format('{"ready":%s,"phase":%d,"first":%s,"second":%s,"settings":%s,"firstValue":"%s","secondValue":"%s","settingsValue":"%s","started":%s,"deleted":%s,"missing":%s,"refused":%s,"status":"%s"}',
        tostring(frames >= 8), phase, tostring(first), tostring(second), tostring(settings),
        firstValue, secondValue, settingsValue, tostring(started), tostring(deleted),
        tostring(missing), tostring(refused), result.status)
    if state ~= previous or frames % 8 == 0 then
        previous = state
        Log("storage-flow " .. state)
    end
end

function Ready(self, dt)
    refresh()
    if firstValue == "recreated" then phase = 4
    elseif first then phase = 1
    elseif settings then phase = 2
    elseif second then phase = 3 end
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity)
    if phase == 0 then
        write("Save", "slot_1", "first")
        write("Save", "slot_2", "second")
        write("Settings", "slot_1", "settings")
    elseif phase == 1 then
        assert(call("Save.Read", {slot="slot_1"}).loaded)
        deleted = call("Save.Delete", {slot="slot_1"})
        assert(deleted and call("Save.GetString", {key="value"}).value == "first")
        missing = not call("Save.Delete", {slot="slot_1"})
        local result, diagnostic = CallFunction("Save.Delete", {slot="../outside"})
        refused = result == nil and diagnostic ~= nil
        assert(missing and refused)
    elseif phase == 2 then
        deleted = call("Settings.Delete", {slot="slot_1"})
        assert(deleted and call("Settings.GetString", {key="value"}).value == "settings")
        missing = not call("Settings.Delete", {slot="slot_1"})
        local result, diagnostic = CallFunction("Settings.Delete", {slot="../outside"})
        refused = result == nil and diagnostic ~= nil
        assert(missing and refused)
    elseif phase == 3 then
        write("Save", "slot_1", "recreated")
    else
        error("No action expected after the final restart")
    end
    phase = phase + 1
    refresh()
    report()
    started = call("Save.Flush")
    report()
end

function Tick(self, dt)
    frames = frames + 1
    report()
end
