local counter = 0
local loaded = false
local settingsLoaded = false
local note = ""
local label = ""
local frames = 0
local written = false
local settingsWritten = false
local started = false
local requested = false
local previous = ""

local function call(name, arguments)
    local result, diagnostic = CallFunction(name, arguments or {})
    assert(result ~= nil, diagnostic)
    return result
end

local function encode(value)
    return '"' .. value:gsub('[%z\1-\31\\"]', function(character)
        return string.format('\\u%04x', string.byte(character))
    end) .. '"'
end

local function report()
    local result = call("Save.StorageStatus")
    local state = string.format('{"counter":%d,"loaded":%s,"settingsLoaded":%s,"note":%s,"label":%s,"written":%s,"settingsWritten":%s,"started":%s,"status":%s,"diagnostic":%s,"ready":%s}',
        counter, tostring(loaded), tostring(settingsLoaded), encode(note), encode(label),
        tostring(written), tostring(settingsWritten), tostring(started), encode(result.status),
        encode(result.diagnostic), tostring(frames >= 8))
    if state ~= previous or frames % 8 == 0 then
        previous = state
        Log("storage-flow " .. state)
    end
end

function Ready(self, dt)
    local save = call("Save.Read", {slot="slot_1"})
    local settings = call("Settings.Read", {slot="slot_1"})
    assert(save.loaded or save.status == "FileNotFound", save.diagnostic)
    assert(settings.loaded or settings.status == "FileNotFound", settings.diagnostic)
    loaded = save.loaded
    settingsLoaded = settings.loaded
    counter = call("Save.GetInt", {key="counter"}).value
    note = call("Save.GetString", {key="note"}).value
    label = call("Settings.GetString", {key="label"}).value
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity)
    if call("Save.StorageStatus").status == "Failed" then
        started = call("Save.Flush")
        report()
        return
    end
    counter = counter + 1
    call("Save.SetInt", {key="counter", value=counter})
    call("Save.SetString", {key="note", value="first"})
    label = "Zażółć " .. counter
    call("Settings.SetString", {key="label", value=label})
    written = call("Save.Write", {slot="slot_1"})
    settingsWritten = call("Settings.Write", {slot="slot_1"})
    report()
    started = call("Save.Flush") and call("Settings.Flush")
    requested = true
    report()
    -- A second write while the first transaction is pending must stay dirty.
    note = "latest"
    call("Save.SetString", {key="note", value=note})
    written = call("Save.Write", {slot="slot_1"}) and written
    report()
end

function Tick(self, dt)
    frames = frames + 1
    if requested and call("Save.StorageStatus").status == "Dirty" then
        started = call("Save.Flush") and started
    end
    report()
end
