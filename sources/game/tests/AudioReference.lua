-- Reference audio project: a looping stream through a bus hierarchy, a one-shot, a spatial voice, a snapshot, runtime bus overrides and
-- a DSP chain, all rendered to real PCM offline and checked against the analytic levels of the authored sine waves.
local RATE = 48000

local function near(actual, expected, relative, what)
    local tolerance = math.max(expected * relative, 1e-5)
    assert(math.abs(actual - expected) <= tolerance,
        string.format("%s: %.6f, expected %.6f (+-%.6f)", what, actual, expected, tolerance))
end

-- Peak held by a meter since the last reset; `bus` "" is the master.
local function held(bus, channel)
    local meter = Audio.Meter(bus, channel)
    assert(meter and meter.capturing, "meter for " .. bus .. " is not capturing")
    return meter.heldPeak
end

local function measure(frames)
    assert(Audio.ResetMeters(), "reset meters")
    local pcm = Audio.RenderOffline(frames)
    assert(pcm.frames == frames, "the offline block came back short: no dropout is possible in offline rendering, so this is a failure")
    return pcm
end

function Ready(self, dt)
    assert(Audio.ConfigureOffline(), "offline audio")
    assert(Audio.ConfigureMeters(true), "signal meters")
    Audio.RenderOffline(8192) -- the autoplay sources start

    -- 1. The stream. Music: a 220 Hz sine of 0.4, bus Music 0.5, parent Output 0.8.
    local pcm = measure(16384)
    near(pcm.rms, 0.4 * 0.5 * 0.8 / math.sqrt(2), 0.03, "master rms with the stream")
    near(held("Music", 0), 0.4 * 0.5, 0.01, "Music bus peak")
    near(held("Output", 0), 0.4 * 0.5 * 0.8, 0.02, "Output bus peak")
    near(held("", 1), 0.4 * 0.5 * 0.8, 0.02, "master right channel peak")
    local music = World.FindByName("Music")
    assert(Audio.IsSourcePlaying(music).playing, "the stream is playing")
    assert(held("Voice", 0) == 0 and held("Sfx", 0) == 0, "buses nobody feeds are silent")

    -- 2. The DSP chain: a 4th order low-pass at 55 Hz on a 220 Hz sine on the Filtered bus.
    local ratio = math.tan(math.pi * 220 / RATE) / math.tan(math.pi * 55 / RATE)
    local expected = 0.4 / math.sqrt(1 + ratio ^ 8)
    measure(16384)
    local filtered = held("Filtered", 0)
    near(filtered, expected, 0.25, "filtered drone peak")
    assert(filtered < 0.4 / 100, "the low-pass takes the 220 Hz drone down by at least 40 dB")
    local state = Audio.DspState("Filtered")
    assert(state.current and state.activeEffects == 1 and state.error == "", "the Filtered chain is installed")

    -- 3. A one-shot on the Sfx bus: a 880 Hz sine of 0.5 for 0.3 s (14400 frames).
    local played = Audio.Play({ clip = "/Game/Audio/Hit.21kb", outputBus = "Sfx", volume = 1.0 })
    assert(played, "the one-shot was refused")
    assert(Audio.IsPlaying(played), "the one-shot voice is playing")
    measure(4096)
    near(held("Sfx", 0), 0.5, 0.01, "one-shot peak on the Sfx bus")
    near(held("Sfx", 1), 0.5, 0.01, "one-shot peak on the Sfx bus, right")
    Audio.RenderOffline(16384) -- past the end of the clip
    measure(4096)
    assert(held("Sfx", 0) < 1e-6, "the one-shot ended")
    assert(not Audio.IsPlaying(played), "the one-shot voice is gone")

    -- 4. A spatial voice to the left of the listener, then to the right.
    local voice = World.FindByName("Voice")
    assert(Audio.PlaySource(voice), "start the spatial source")
    Audio.RenderOffline(4096)
    measure(8192)
    local left, right = held("Voice", 0), held("Voice", 1)
    assert(left > 0.01 and left > right * 3, string.format("a source on the left is heard on the left: L %.5f R %.5f", left, right))
    Transform.SetPosition(voice, 6, 0, 5)
    Audio.RenderOffline(4096)
    measure(8192)
    left, right = held("Voice", 0), held("Voice", 1)
    assert(right > 0.01 and right > left * 3, string.format("a source on the right is heard on the right: L %.5f R %.5f", left, right))
    Transform.SetPosition(voice, 60, 0, 5) -- beyond the maximum distance
    Audio.RenderOffline(4096)
    measure(8192)
    assert(held("Voice", 1) < right * 0.5, "a source far away is quieter")
    assert(Audio.StopSource(voice), "stop the spatial source")

    -- 5. A snapshot, a runtime override and clearing both: Music 0.1 in Quiet.
    assert(Audio.SetSnapshot("Quiet"), "select the Quiet snapshot")
    assert(Audio.ActiveSnapshot() == "Quiet", "the snapshot is selected")
    Audio.RenderOffline(2048)
    measure(8192)
    near(held("Music", 0), 0.4 * 0.1, 0.03, "Music bus peak in Quiet")
    near(held("", 0), 0.4 * 0.1 * 0.8, 0.05, "master peak in Quiet")
    assert(Audio.SetBusVolume("Music", 1.0), "a runtime override")
    Audio.RenderOffline(2048)
    measure(8192)
    near(held("Music", 0), 0.4, 0.02, "an override is stronger than the snapshot")
    assert(Audio.ClearBusVolume("Music"), "clear the override")
    Audio.RenderOffline(2048)
    measure(8192)
    near(held("Music", 0), 0.4 * 0.1, 0.03, "clearing the override returns to the snapshot")
    assert(Audio.SetSnapshot(""), "clear the snapshot")
    Audio.RenderOffline(2048)
    measure(8192)
    near(held("Music", 0), 0.4 * 0.5, 0.02, "clearing the snapshot returns to the authored mix")

    Log("audio reference project verified")
end

function Destroyed(self, dt)
    Log("audio reference project stopped")
end
