local frames = 0
local clicks = 0
local originalMixer = ""
local refusedStatus = -1

local function call(name, args)
    local result, diagnostic = CallFunction("Audio." .. name, args or {})
    assert(result ~= nil, diagnostic)
    return result
end

function Ready(self)
    originalMixer = call("ActiveMixer")
    assert(type(originalMixer) == "string" and originalMixer ~= "", "Cooked scene did not retain its authored mixer")
    assert(self:GetProperty("AudioSource", "outputBus") == "Effects")
end

function OnUIClicked(self, event)
    assert(event.args.entity == self.entity)
    clicks = clicks + 1
    -- The first click only provides browser activation for authored autoplay.
    if clicks == 2 then
        assert(call("SetBusVolume", {bus="Output", volume=1.0}))
    elseif clicks == 3 then
        assert(call("SetBusVolume", {bus="Effects", volume=1.0}))
    elseif clicks == 4 then
        assert(call("ClearBusVolume", {bus="Output"}))
    elseif clicks == 5 then
        assert(call("ClearBusVolume", {bus="Effects"}))
    elseif clicks == 6 then
        assert(call("SetMixer", {mixer=""}))
        local refused = call("PlaySource")
        assert(not refused.succeeded and not refused.playing)
        refusedStatus = refused.status
    elseif clicks == 7 then
        assert(call("SetMixer", {mixer=originalMixer}))
    end
end

function Tick(self, dt)
    frames = frames + 1
    if frames % 8 == 0 then
        local device = call("DeviceStatus")
        local source = call("IsSourcePlaying")
        Log("audio-routing " .. string.format(
            '{"ready":true,"clicks":%d,"playing":%s,"available":%s,"deviceStatus":%d,"assigned":%s,"refusedStatus":%d}',
            clicks, tostring(source.playing), tostring(device.available), device.status,
            tostring(call("ActiveMixer") ~= ""), refusedStatus))
    end
end
