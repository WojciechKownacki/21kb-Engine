local frames = 0
local authoredVolume = 0
local authoredPitch = 0

function Ready(self, dt)
    assert(self:HasComponent("Audio Source") and self:HasComponent("Audio Listener"))
    authoredVolume = self:GetProperty("Audio Source", "volume")
    authoredPitch = self:GetProperty("AudioSource", "pitch")
    assert(self:SetProperty("Audio Source", "volume", 0.5))
    assert(self:SetProperty("AudioSource", "pitch", 1.0))
end

function Tick(self, dt)
    frames = frames + 1
    if frames % 8 == 0 then
        Log("audio-properties " .. string.format(
            '{"ready":true,"authoredVolume":%.4f,"authoredPitch":%.4f,"volume":%.4f,"pitch":%.4f,"pan":%.4f,"spatialBlend":%.4f,"minDistance":%.4f,"maxDistance":%.4f,"rolloff":%.4f,"dopplerFactor":%.4f,"attenuation":%d,"loop":%s,"spatial":%s,"autoplay":%s,"mute":%s,"enabled":%s,"listenerPriority":%d,"localUser":%d,"listenerPrimary":%s,"listenerEnabled":%s}',
            authoredVolume, authoredPitch,
            self:GetProperty("AudioSource", "volume"), self:GetProperty("AudioSource", "pitch"),
            self:GetProperty("AudioSource", "pan"), self:GetProperty("AudioSource", "spatialBlend"),
            self:GetProperty("AudioSource", "minDistance"), self:GetProperty("AudioSource", "maxDistance"),
            self:GetProperty("AudioSource", "rolloff"), self:GetProperty("AudioSource", "dopplerFactor"),
            self:GetProperty("AudioSource", "attenuationModel"), tostring(self:GetProperty("AudioSource", "loop")),
            tostring(self:GetProperty("AudioSource", "spatial")), tostring(self:GetProperty("AudioSource", "autoplay")),
            tostring(self:GetProperty("AudioSource", "mute")), tostring(self:GetProperty("AudioSource", "enabled")),
            self:GetProperty("Audio Listener", "priority"), self:GetProperty("AudioListener", "localUser"),
            tostring(self:GetProperty("AudioListener", "primary")), tostring(self:GetProperty("AudioListener", "enabled"))))
    end
end
