local frames = 0
local clicks = 0

local function call(name)
    local result, diagnostic = CallFunction("Audio." .. name, {})
    assert(result ~= nil, diagnostic)
    return result
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity)
    clicks = clicks + 1
    -- The first click only supplies browser activation. Playback comes from
    -- the authored autoplay flag; this controller never requests source play.
    local field, value
    if clicks == 2 then field, value = "volume", 0.5
    elseif clicks == 3 then field, value = "mute", true
    elseif clicks == 4 then field, value = "mute", false
    elseif clicks == 5 then field, value = "pitch", 2.0
    elseif clicks == 6 then field, value = "enabled", false
    elseif clicks == 7 then field, value = "enabled", true
    end
    if field then assert(self:SetProperty("AudioSource", field, value)) end
end

function Tick(self, dt)
    frames = frames + 1
    if frames % 8 == 0 then
        local device = call("DeviceStatus")
        local source = call("IsSourcePlaying")
        Log("audio-autoplay " .. string.format(
            '{"ready":true,"clicks":%d,"playing":%s,"available":%s,"deviceStatus":%d,"autoplay":%s,"volume":%.4f,"pitch":%.4f,"mute":%s,"enabled":%s}',
            clicks, tostring(source.playing), tostring(device.available), device.status,
            tostring(self:GetProperty("AudioSource", "autoplay")), self:GetProperty("AudioSource", "volume"),
            self:GetProperty("AudioSource", "pitch"), tostring(self:GetProperty("AudioSource", "mute")),
            tostring(self:GetProperty("AudioSource", "enabled"))))
    end
end
