local frames = 0
local clicks = 0

local function call(name, args)
    local result, diagnostic = CallFunction("Audio." .. name, args or {})
    assert(result ~= nil, diagnostic)
    return result
end

function Ready(self)
    assert(call("ActiveSnapshot") == "Quiet", "Cooked scene lost its selected snapshot")
    assert(self:GetProperty("AudioSource", "outputBus") == "Effects")
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity)
    clicks = clicks + 1
    -- The first trusted click only unlocks authored autoplay.
    if clicks == 2 then
        assert(call("SetSnapshot", {snapshot="Loud"}))
    elseif clicks == 3 then
        assert(call("SetBusVolume", {bus="Effects", volume=0.5}))
    elseif clicks == 4 then
        assert(call("SetSnapshot", {snapshot="Quiet"}))
    elseif clicks == 5 then
        assert(call("ClearBusVolume", {bus="Effects"}))
    elseif clicks == 6 then
        assert(call("SetSnapshot", {snapshot=""}))
    elseif clicks == 7 then
        assert(call("TransitionToSnapshot", {snapshot="Quiet", durationSeconds=0.5}))
    elseif clicks == 8 then
        assert(call("TransitionToSnapshot", {snapshot="", durationSeconds=0.5}))
    end
end

function Tick(self, dt)
    frames = frames + 1
    if frames % 8 == 0 then
        local device = call("DeviceStatus")
        local source = call("IsSourcePlaying")
        Log("audio-snapshots " .. string.format(
            '{"ready":true,"clicks":%d,"playing":%s,"available":%s,"deviceStatus":%d,"snapshot":"%s"}',
            clicks, tostring(source.playing), tostring(device.available), device.status, call("ActiveSnapshot")))
    end
end
