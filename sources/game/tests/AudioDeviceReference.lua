-- Appended to the offline reference script for the device qualification package.
-- The output bus is muted; child meters still observe real decoded/DSP signals.
local verifyOffline = Ready
local phase, voice, initialGeneration = 0, nil, nil
local totalBlocks, totalMisses, peakBytes = 0, 0, 0
local verified = false
local onsetFrames, maximumOnsetFrames, waitingOnset = 0, 0, false

local function startDevice()
    assert(Audio.Reinitialize().available, "native output device is required for this qualification")
    assert(Audio.ConfigureMeters(true), "device capture")
    assert(Audio.SetBusVolume("Output", 0), "mute physical output during qualification")
    voice = assert(Audio.Play({clip = "/Game/Audio/Hit.21kb", outputBus = "Sfx", volume = 0.1, loop = true}))
    local d = assert(Audio.Diagnostics())
    assert(d.playbackAvailable and not d.offline and d.backend ~= "Null" and d.backend ~= "", "real device backend")
    onsetFrames, waitingOnset = tonumber(d.deviceFrames), true
    return d
end

function Ready(self, dt)
    verifyOffline(self, dt)
    initialGeneration = tonumber(startDevice().generation)
end

function Tick(self, dt)
    local d = assert(Audio.Diagnostics())
    assert(d.playbackAvailable and not d.offline, "device playback lost")
    if waitingOnset then
        local signal = Audio.Meter("Sfx", 0)
        local elapsedFrames = tonumber(d.deviceFrames) - onsetFrames
        if signal and signal.heldPeak > 0.01 then
            maximumOnsetFrames = math.max(maximumOnsetFrames, elapsedFrames)
            waitingOnset = false
        end
        assert(elapsedFrames < d.sampleRate / 2, "device signal onset exceeded 500 ms")
    end
    assert(d.periodFrames <= d.sampleRate / 10, "device callback period exceeds 100 ms")
    local blocks, misses = tonumber(d.deviceBlocks), tonumber(d.callbackDeadlineMisses)
    assert(tonumber(d.shortBlocks) == 0, "device returned a short PCM block")
    assert(tonumber(d.allocatedBytes) <= tonumber(d.peakAllocatedBytes), "allocation counters")
    assert(tonumber(d.peakAllocatedBytes) < 128 * 1024 * 1024, "reference audio engine exceeds 128 MiB")
    peakBytes = math.max(peakBytes, tonumber(d.peakAllocatedBytes))
    if phase < 3 and tonumber(d.deviceFrames) >= d.sampleRate * 2 then
        local music = assert(Audio.Meter("Music", 0))
        local sfx = assert(Audio.Meter("Sfx", 0))
        assert(music.heldPeak > 0.01 and sfx.heldPeak > 0.01, "real callback did not decode stream and looping voice")
        assert(Audio.Meter("", 0).peak == 0, "device test output bus is not muted")
        assert(misses * 100 <= blocks, "more than one percent callback deadline misses")
        totalBlocks, totalMisses = totalBlocks + blocks, totalMisses + misses
        phase = phase + 1
        if phase < 3 then
            local oldVoice, oldGeneration = voice, tonumber(d.generation)
            d = startDevice()
            assert(tonumber(d.generation) > oldGeneration and not Audio.IsPlaying(oldVoice), "reset retained a stale voice")
            assert(Audio.TransitionToSnapshot("Quiet", 0.25), "device snapshot transition")
        else
            verified = true
            Log(string.format("audio device reference verified backend=%s blocks=%d misses=%d periodFrames=%d sampleRate=%d worstCallbackNs=%s peakEngineBytes=%d generations=%d maxObservedOnsetFrames=%d",
                d.backend, totalBlocks, totalMisses, d.periodFrames, d.sampleRate, d.worstCallbackNanoseconds, peakBytes,
                tonumber(d.generation) - initialGeneration + 1, maximumOnsetFrames))
        end
    end
end

function Destroyed(self, dt)
    assert(verified, "device qualification ended before six seconds of actual callback frames")
    local d = assert(Audio.Diagnostics())
    assert(tonumber(d.shortBlocks) == 0 and tonumber(d.callbackDeadlineMisses) * 100 <= tonumber(d.deviceBlocks), "final device callback budget")
    Log("audio device reference stopped")
end
