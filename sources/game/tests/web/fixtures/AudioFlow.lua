local frames = 0
local clicks = 0
local lastStatus = -1
local operations = {"PlaySource", "PauseSource", "ResumeSource", "StopSource", "PlaySource"}

local function call(name)
    local result, diagnostic = CallFunction("Audio." .. name, {})
    assert(result ~= nil, diagnostic)
    return result
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity)
    clicks = clicks + 1
    if clicks == 6 or clicks == 7 then
        local result = call("Reinitialize")
        lastStatus = result.status
        assert(result.status == 3 or result.status == 4, "Audio restart failed: " .. result.status)
        return
    end
    local result = call(assert(operations[clicks]))
    lastStatus = result.status
    assert(result.succeeded, "Audio transport failed: " .. result.status)
end

function Tick(self, dt)
    frames = frames + 1
    if frames % 8 == 0 then
        local device = call("DeviceStatus")
        local source = call("IsSourcePlaying")
        Log("audio-flow " .. string.format(
            '{"ready":true,"clicks":%d,"playing":%s,"available":%s,"deviceStatus":%d,"lastStatus":%d}',
            clicks, tostring(source.playing), tostring(device.available), device.status, lastStatus))
    end
end
