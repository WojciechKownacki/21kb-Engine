"""Verify authored mixer effects against real offline PCM and a cooked native player."""

import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import wave


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli", "cooker", "player", "engine-root", "test-root", "shaderc"]:
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--plugin", action="append", type=Path, required=True)
    parser.add_argument("--runtime-dll", action="append", type=Path, default=[])
    parser.add_argument("--editor-tests", type=Path, help="Also author the packaged mixer through headless Inspector controls")
    args = parser.parse_args()
    for name in ["cli", "cooker", "player", "engine_root", "test_root", "shaderc"]:
        setattr(args, name, getattr(args, name).resolve())
    args.plugin = [path.resolve() for path in args.plugin]
    args.runtime_dll = [path.resolve() for path in args.runtime_dll]
    if args.editor_tests:
        args.editor_tests = args.editor_tests.resolve()
    require(os.name == "nt", "The packaged DSP test requires the native Windows player")
    ctypes.windll.kernel32.SetErrorMode(32771)
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="AudioDspPackage", dir=args.test_root))
    temp = root / "Temp"
    temp.mkdir()
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp))
    project = root / "Sample"
    history = []

    def run(exe, *tail, expected=0, cwd=root, timeout=90):
        start = time.monotonic()
        process = subprocess.Popen([str(exe), *map(str, tail)], cwd=cwd, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, check=False)
            process.communicate()
            raise
        row = dict(executable=str(exe), arguments=list(map(str, tail)), code=process.returncode,
                   seconds=time.monotonic()-start, stdout=stdout.decode("utf8", "replace"),
                   stderr=stderr.decode("utf8", "replace"))
        history.append(row)
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf8")
        require(process.returncode == expected, f"Unexpected command result: {row}")
        return row["stdout"]

    def cli(*tail, **kwargs):
        return run(args.cli, *tail, **kwargs)

    cli("new", "--parent", root, "--name", "Sample", "--template", "Empty")
    tone = root / "Tone.wav"
    with wave.open(str(tone), "wb") as output:
        output.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(3276 * math.sin(2 * math.pi * 440 * n / 48000)))
                                   for n in range(48000)))
    cli("import", "--project", project, "--destination", "/Game/Audio", tone)
    mixer = "/Game/Audio/Main.kbmixer"
    edits = ["audio-mixer", "set", "--project", project, "--mixer", mixer,
             "--effect-bus", "Effects", "--json"]
    files_before = set(project.rglob("*"))
    cli("audio-mixer", "new", "--project", project, "--name", "Draft", "--folder", "Assets/New/Audio",
        "--bus", "Effects", "--effect-bus", "Effects", "--add-effect", "echo=delay", "--dry-run", "--json")
    require(set(project.rglob("*")) == files_before, "Dry run created files or directories")
    authored = json.loads(cli("audio-mixer", "new", "--project", project, "--name", "Main",
        "--bus", "Effects", "--set", "Effects.volume=0.25", "--effect-bus", "Effects",
        "--add-effect", "tone=equalizer", "--add-effect", "gain=compressor",
        "--set-effect", "tone.cutoffHz=440", "--set-effect", "tone.gainDb=6",
        "--set-effect", "gain.ratio=1", "--set-effect", "gain.makeupDb=6", "--json"))
    file = project / "Assets/Audio/Main.kbmixer"
    original = file.read_bytes()
    require(original.startswith(b"kbmixer 2"), "Effects were not versioned")
    candidate = json.loads(cli(*edits, "--set-effect", "tone.gainDb=3", "--dry-run"))
    require(candidate["dryRun"] and file.read_bytes() == original, "Dry run altered the mixer")
    for setting in ["tone.gainDb=nan", "tone.wet=0", "gain.ratio=0"]:
        cli(*edits, "--set-effect", setting, expected=1)
        require(file.read_bytes() == original, "Rejected DSP edit changed saved data")
    cli(*edits, "--add-effect", "tone=delay", expected=1)
    require(file.read_bytes() == original, "Duplicate effect ID changed saved data")
    cli(*edits, "--move-effect", "gain=0")
    moved = json.loads(cli("audio-mixer", "show", "--project", project, "--mixer", mixer, "--json"))
    require([effect["id"] for effect in moved["buses"][0]["effects"]] == ["gain", "tone"], "Reorder was lost")
    cli(*edits, "--move-effect", "tone=0")
    cli(*edits, "--add-effect", "again=equalizer", "--set-effect", "again.bypass=true")
    cli(*edits, "--remove-effect", "again")
    require(file.read_bytes() == original, "Effect edits did not restore exact parameters and order")
    cli("audio-mixer", "assign", "--project", project, "--mixer", mixer)
    cli("entity", "add", "--project", project, "--name", "Emitter", "--create", "Audio Source")
    cli("component", "set", "--project", project, "--entity", "Emitter", "--type", "Audio Source",
        "--set", "clipAssetId=/Game/Audio/Tone.21kb", "--set", "loop=true", "--set", "autoplay=true",
        "--set", "spatial=false", "--set", "outputBus=Effects")
    report = json.loads(cli("run", "--project", project, "--frames", "4", "--report", "audio",
                            "--json", "--pcm-frames", "4096"))
    require(report["succeeded"] and report["backend_available"] and report["offline"] and
            report["pcm_frames"] == 16384 and int(report["rendered_frames"]) >= 16384,
            "CLI did not produce actual PCM")
    require(report["buses"][0]["applied_effects"] == authored["buses"][0]["effects"], "Runtime parameters differ from authored data")
    expected_peak = (3276 / 32768) * 0.25 * 10**(12/20)
    require(abs(report["peak"] - expected_peak) < 0.002 and 0.068 < report["rms"] < 0.072,
            "CLI PCM does not contain the independent expected DSP gain")
    meters = json.loads(cli("run", "--project", project, "--frames", "4", "--report", "audio-meters",
                            "--json", "--pcm-frames", "4096"))
    captured = meters["meters"]
    require(meters["schema"] == "21kb.audio-meters/v1" and meters["succeeded"] and captured["supported"]
            and captured["initialized"] and captured["capture_enabled"] and captured["master"]["available"]
            and int(captured["master"]["processed_frames"]) == 16384 and captured["sample_rate"] == 48000,
            "Actual provider DLL did not expose or execute the optional meter capability")
    require(len(captured["buses"]) == 1 and captured["buses"][0]["bus"] == "Effects"
            and captured["buses"][0]["available"] and int(captured["buses"][0]["processed_frames"]) > 0,
            "Actual provider DLL did not capture its named bus")
    for channel in captured["master"]["channel"]:
        require(abs(channel["held_peak"] - meters["peak"]) < 1e-8
                and abs(channel["held_peak"] - expected_peak) < 0.002
                and abs(channel["rms"] - expected_peak / math.sqrt(2)) < 0.002
                and int(channel["clipped_samples"]) == 0 and int(channel["non_finite_samples"]) == 0,
                "Provider DLL meters differ from independent PCM analysis")
    active_effects = 2
    expected_rms = expected_peak / math.sqrt(2)
    peak_tolerance = 0.002
    if args.editor_tests:
        editor_output = run(args.editor_tests, "audio-mixer-inspector")
        require("parameterEdits=27 uiCommands=50 languages=32 stableRepeatId=1 undoRedoExact=1 externalConflict=1" in editor_output,
                "Inspector did not execute its complete authoring and Undo workflow")
        ui_mixer = temp / "kb_editor_audio_mixer_inspector_tests/Project/Assets/Mixers/NewAudioMixer.kbmixer"
        require(ui_mixer.is_file(), "Inspector did not save its actual mixer asset")
        shutil.copyfile(ui_mixer, file)
        authored = json.loads(cli("audio-mixer", "show", "--project", project, "--mixer", mixer, "--json"))
        effects = authored["buses"][0]["effects"]
        require(len(effects) == 7 and effects[0]["kind"] == "lowpass" and
                effects[0]["parameters"]["cutoffHz"] == 200 and effects[0]["parameters"]["order"] == 4,
                "Inspector-authored package fixture has the wrong effects")
        active_effects = sum(not effect["parameters"]["bypass"] for effect in effects)
        require(active_effects == 1, "Inspector did not enable exactly its authored filter")
        ratio = math.tan(math.pi * 440 / 48000) / math.tan(math.pi * 200 / 48000)
        expected_peak = (3276 / 32768) / math.sqrt(1 + ratio**8)
        expected_rms = expected_peak / math.sqrt(2)
        peak_tolerance = expected_peak * 0.02
        ui_report = json.loads(cli("run", "--project", project, "--frames", "4", "--report", "audio", "--json", "--pcm-frames", "4096"))
        require(ui_report["succeeded"] and ui_report["buses"][0]["applied_effects"] == effects and
                ui_report["peak"] < 0.03 and ui_report["rms"] < 0.01,
                "CLI did not execute the actual Inspector-authored effect chain")
    cli("entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    cli("script-new", "--project", project, "--name", "DspProbe", "--language", "lua")
    script = project / "Assets/Scripts/DspProbe.lua"
    script.write_text(('''function Ready(self, dt)
    assert(Audio.ConfigureOffline(), "offline audio provider")
    Audio.RenderOffline(8192)
    local pcm = Audio.RenderOffline(8192)
    local state = Audio.DspState("Effects")
    assert(state.current and state.activeEffects == @ACTIVE@ and state.error == "", "installed DSP chain")
    assert(state.sampleRate == 48000 and state.channels == 2, "actual PCM format")
    assert(pcm.frames == 8192 and math.abs(pcm.peak - @PEAK@) < @TOL@ and
           math.abs(pcm.rms - @RMS@) < @TOL@, "packaged DSP PCM gain")
    assert(Audio.SetBusVolume("Effects", 0), "post DSP mute")
    Audio.RenderOffline(512)
    assert(Audio.RenderOffline(4096).peak == 0, "post DSP mute PCM")
    assert(Audio.SetBusVolume("Effects", @VOLUME@), "post DSP unmute")
    Audio.RenderOffline(512)
    assert(math.abs(Audio.RenderOffline(4096).peak - @PEAK@) < @TOL@, "post DSP unmute PCM")
    Log("packaged DSP PCM verified")
end
function Destroyed(self, dt)
    Log("packaged DSP stopped")
end
''').replace("@ACTIVE@", str(active_effects)).replace("@PEAK@", repr(expected_peak))
       .replace("@RMS@", repr(expected_rms)).replace("@TOL@", repr(peak_tolerance))
       .replace("@VOLUME@", "1" if args.editor_tests else "0.25"), encoding="utf8")
    cli("scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "Emitter", "--script", "/Game/Scripts/DspProbe.lua")
    output = root / "Packaged"
    output.mkdir()

    def cook():
        return run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", output / "Game.kbpack",
                   "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache")

    cook()
    player = output / "Sample.exe"
    for source in [args.player, *args.plugin, *args.runtime_dll]:
        shutil.copy2(source, player if source == args.player else output / source.name)
    require(not (output / "Assets").exists(), "Runtime used loose assets")
    text = run(player, "--headless", "--frames=3", "--width=64", "--height=64", "--uncapped", cwd=output)
    require(text.count("kb_game: script: packaged DSP PCM verified") == 1 and
            text.count("kb_game: script: packaged DSP stopped") == 1 and
            "rendered=3" in text and "shutdown=clean" in text, "Packaged PCM or script lifecycle was not verified")
    script.write_text('''function Ready(self, dt)
    assert(Audio.ConfigureOffline())
    Audio.RenderOffline(4096)
    assert(Audio.DspState("Effects").activeEffects == 3, "DSP negative control")
end
''', encoding="utf8")
    cook()
    run(player, "--headless", "--frames=3", "--width=64", "--height=64", "--uncapped", cwd=output, expected=1)
    require("DSP negative control" in history[-1]["stderr"] + history[-1]["stdout"], "Negative assertion was not executed")
    (root / "result.json").write_text(json.dumps(dict(result="PASS", pcm=report, meters=meters, inspectorAuthored=bool(args.editor_tests),
        inspectorPcm=ui_report if args.editor_tests else None, negativeScriptExit=1,
        limitation="Offline native PCM and a hidden Windows player; no physical audio device or other platform qualification."), indent=2), encoding="utf8")
    print(f"PASS: authored effects, actual CLI PCM and cooked runtime PCM; {root}")


if __name__ == "__main__":
    main()
