local frames = 0
local clicks = 0
local voice = 0
local pausedVoice = 0
local meters = false

local function call(name, args)
    local result, diagnostic = CallFunction("Audio." .. name, args or {})
    assert(result ~= nil, diagnostic)
    return result
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity)
    clicks = clicks + 1
    if clicks == 1 then
        assert(call("PlaySource").succeeded)
        voice = call("Play", {clip="/Game/Audio/Tone.21kb", volume=0, loop=true, spatial=false}).voice
        pausedVoice = call("Play", {clip="/Game/Audio/Tone.21kb", volume=0, loop=true, spatial=false}).voice
        assert(call("Pause", {voice=pausedVoice}))
    elseif clicks == 2 then
        assert(call("PauseSource").succeeded)
    elseif clicks == 3 then
        assert(call("ResumeSource").succeeded)
    else
        assert(call("StopSource").succeeded)
    end
end

function Tick(self, dt)
    frames = frames + 1
    if not meters then
        assert(call("ConfigureMeters", {enabled=true}))
        meters = true
    end
    if frames % 8 ~= 0 then return end
    local device = call("DeviceStatus")
    local source = call("IsSourcePlaying")
    local stats = call("Diagnostics")
    local meter = CallFunction("Audio.Meter", {bus="", channel=0})
    if meter == nil then return end
    local position = call("GetPosition", {voice=voice})
    local pausedPosition = call("GetPosition", {voice=pausedVoice})
    Log("audio-lifecycle " .. string.format(
        '{"frame":%d,"clicks":%d,"available":%s,"status":%d,"playing":%s,"backend":"%s",' ..
        '"generation":%s,"deviceFrames":%s,"shortBlocks":%s,"allocatedBytes":%s,' ..
        '"rms":%.8f,"peak":%.8f,"cursor":%.8f,"voiceValid":%s,"pausedCursor":%.8f,"pausedValid":%s}',
        frames, clicks, tostring(device.available), device.status, tostring(source.playing), stats.backend,
        stats.generation, stats.deviceFrames, stats.shortBlocks, stats.allocatedBytes,
        meter.rms, meter.peak, position.seconds, tostring(position.valid), pausedPosition.seconds, tostring(pausedPosition.valid)))
end
