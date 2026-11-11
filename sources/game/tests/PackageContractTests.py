"""Packages a project made by ProjectCreator for Windows and Android and holds each package to the project.

  windows       Build Game's package script makes the Windows x64 package; its files, its manifest and the
                game running headless are checked.
  android-cook  The Android cook of assets and shaders for ETC2 and ASTC, checked without any Android SDK.
  android-apk   The same script makes the Android debug APK; the archive is checked. Exits 77 (skipped) when
                the Android toolchain is incomplete.

The pack checks run kb_game_core_tests --package-contract. Set KB_PACKAGE_TEST_OFFLINE=1 to make any attempted
download fail instead of happening.
"""
import argparse
import atexit
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

SKIP = 77
TEXT_SUFFIXES = ('.json', '.txt', '.kbmanifest', '.properties', '.version')
GAME_NAME = 'PackageSample'
SCENES = ('Main', 'Second', 'Unlisted')

# What the project asks for: every value differs from the default, and the build list names Second only.
SETTINGS = {
    'Project.Product': {'Publisher': 'Engine Samples', 'Version': '1.2.3', 'ExecutableName': GAME_NAME,
                        'ApplicationIcon': 'Branding/ApplicationIcon.png'},
    'Project.Android': {'ApplicationId': 'com.kbengine.packagesample', 'VersionCode': '7', 'Label': 'Package Sample'},
    'Project.BuildScenes': {'Count': '1', 'Scene0': '/Game/Scenes/Second.21kbscene'},
    'Project.Display': {'WindowWidth': '1600', 'WindowHeight': '900', 'WindowMode': 'BorderlessFullscreen',
                        'Resizable': '0', 'VSync': '0', 'FrameRateCap': '90'},
    'Project.Physics': {'FixedStepHz': '120', 'GravityY': '-3.7'},
    'Project.Time': {'TimeScale': '0.5'},
    'Project.Audio': {'MasterVolume': '0.25'},
    'Project.Tags': {'Count': '1', 'Tag0': 'Boss'},
}

# CpuParticleBackend::Step accepts only a 1/60 s step, so a project with another fixed step gets this one
# error from the particle provider, which every new project lists, and no particle simulation. Until that is
# lifted the run may carry exactly this error and no other.
KNOWN_PARTICLE_STEP_ERROR = 'particle CPU backend rejected the authoritative fixed step'

# Everything a Windows package holds besides Licenses/: no editor, no tool, no source asset, no project file.
WINDOWS_FILES = {
    f'{GAME_NAME}.exe', 'Game.kbpack', 'release.kbmanifest', 'sbom.cdx.json', 'THIRD_PARTY_NOTICES.txt',
    'CRASH_REPORTS.txt', 'package.manifest.json', 'package.receipt.json', 'phonon.dll',
    'kb_physics_jolt_plugin.dll', 'kb_audio_miniaudio_plugin.dll', 'kb_basic_lighting_plugin.dll',
    'kb_21kb_particle_plugin.dll', 'kb_ai_plugin.dll', 'kb_world2d_plugin.dll',
}


def png(width, height, rgba):
    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)
    rows = b''.join(b'\x00' + bytes(rgba) * width for _ in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def read_ini(path):
    sections, current = {}, None
    for line in path.read_text(encoding='utf-8').splitlines():
        header = re.fullmatch(r'\[(.+)\]', line.strip())
        if header:
            current = sections.setdefault(header.group(1), {})
        elif current is not None and '=' in line:
            key, value = line.split('=', 1)
            current[key] = value
    return sections


def write_ini(path, sections):
    lines = []
    for name, values in sections.items():
        lines += [f'[{name}]', *(f'{key}={value}' for key, value in values.items()), '']
    path.write_text('\n'.join(lines), encoding='utf-8', newline='\n')


def patch_ini(path, changes):
    sections = read_ini(path)
    for name, values in changes.items():
        sections.setdefault(name, {}).update(values)
    write_ini(path, sections)


class Context:
    def __init__(self, args, mode):
        self.args = args
        self.mode = mode
        self.root = args.test_root / mode
        if self.root.exists():
            shutil.rmtree(self.root)
        self.root.mkdir(parents=True)
        # The release key of the package is made outside the repository, which refuses private keys, and removed.
        keys = tempfile.mkdtemp(prefix='kbpackagekeys')
        atexit.register(shutil.rmtree, keys, ignore_errors=True)
        self.env = os.environ.copy()
        self.env['KB_RELEASE_KEY_ROOT'] = keys
        self.env['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'
        self.history = []

    def run(self, command, *, cwd=None, expected=0, timeout=240, env=None, low_priority=False):
        command = [str(part) for part in command]
        # A native build of the whole engine runs for long; it yields to whatever else the machine is doing.
        flags = getattr(subprocess, 'BELOW_NORMAL_PRIORITY_CLASS', 0) if low_priority else 0
        result = subprocess.run(command, cwd=cwd or self.root, env=env or self.env, capture_output=True,
                                encoding='utf-8', errors='replace', timeout=timeout, creationflags=flags)
        self.history.append(dict(command=command, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (self.root / 'commands.json').write_text(json.dumps(self.history, indent=1), encoding='utf-8')
        assert result.returncode == expected, self.history[-1]
        return result

    def make_project(self):
        """A new project from ProjectCreator with three scenes, a textured cube, an icon and the settings above."""
        cli = self.args.cli
        self.run([cli, 'new', '--parent', self.root, '--name', GAME_NAME, '--template', 'Empty'])
        self.project = self.root / GAME_NAME
        for scene in SCENES[1:]:
            self.run([cli, 'scene-new', '--project', self.project, '--name', scene, '--template', 'Empty'])
        source = self.root / 'Swatch.png'
        source.write_bytes(png(16, 16, (200, 60, 30, 255)))
        self.run([cli, 'import', '--project', self.project, '--destination', '/Game/Textures', source])
        # Only what the startup scene reaches is cooked, so the texture hangs on a material on a cube.
        self.run([cli, 'material-new', '--project', self.project, '--name', 'Swatch',
                  '--set', 'Base Color Texture=/Game/Textures/Swatch.21kb'])
        self.run([cli, 'entity', 'add', '--project', self.project, '--name', 'Cube', '--create', 'Cube'])
        self.run([cli, 'component', 'set', '--project', self.project, '--entity', 'Cube', '--type', 'MeshRenderer',
                  '--set', 'material=/Game/Materials/Swatch.kbmat'])
        (self.project / 'Branding').mkdir()
        (self.project / 'Branding' / 'ApplicationIcon.png').write_bytes(png(48, 48, (30, 120, 200, 255)))
        self.ini = self.project / 'Config' / 'ProjectSettings.ini'
        patch_ini(self.ini, SETTINGS)
        self.project_file = self.project / f'{GAME_NAME}.21kbproject'
        self.settings = read_ini(self.ini)

    def host_markers(self, *extra):
        roots = [self.args.engine_root, self.args.build_root, self.args.test_root, Path.home(), tempfile.gettempdir(), *extra]
        markers = set()
        for root in roots:
            text = str(Path(root).resolve())
            forward = text.replace('\\', '/')
            markers |= {text.lower(), forward.lower(), text.replace('\\', '\\\\').lower()}
        return sorted(markers)

    def check_pack(self, profile, pack):
        output = self.run([self.args.checker, '--package-contract', profile, pack, self.project, *self.host_markers()]).stdout
        match = re.search(r'package-contract ok: .*', output)
        assert match, output
        print(match.group(0))
        return match.group(0)

    def cook(self, target):
        """The cook and the pack validation of the package script, for one target."""
        import package_game
        job = self.root / f'cook-{target}'
        job.mkdir()
        namespace = argparse.Namespace(build_root=self.args.build_root, target=target, engine_root=self.args.engine_root,
                                       pack_compression_level=package_game.DEFAULT_PACK_COMPRESSION_LEVEL,
                                       configuration='Development')
        tools = [package_game._build_tool_path(self.args.build_root, 'Debug', name)
                 for name in ('kb_cooker', 'kb_runtime_asset_pack_validator')]
        return package_game._cook(namespace, self.project_file, job, *tools)

    def package_command(self, target, output, *extra):
        product = self.settings['Project.Product']
        icon = self.project / self.settings['Project.Product']['ApplicationIcon']
        return [sys.executable, self.args.engine_root / 'scripts' / 'package_game.py', '--project', self.project_file,
                '--target', target, '--configuration', 'Development', '--output', output,
                '--engine-root', self.args.engine_root, '--build-root', self.args.build_root,
                '--product-name', self.settings['Project.Identity']['GameName'], '--publisher', product['Publisher'],
                '--version', product['Version'], '--executable-name', product['ExecutableName'],
                '--application-icon', icon, *extra]


def scan_for_host_paths(label, data, markers, *, text):
    low = data.lower()
    for marker in markers:
        assert marker.encode() not in low, f'{label} names the build machine: {marker}'
    if text:
        found = re.search(rb'(?<![A-Za-z0-9])[A-Za-z]:[\\/]', data)
        assert not found, f'{label} holds a drive-letter path: {data[found.start():found.start() + 80]!r}'


def check_windows(args):
    context = Context(args, 'windows')
    context.make_project()
    package = context.root / 'Export'
    context.run(context.package_command('Windows.x64', package), timeout=1800)

    # The package holds the game and what it runs on, and the manifest lists each file by its hash.
    files = {path.relative_to(package).as_posix() for path in package.rglob('*') if path.is_file()}
    extra = {name for name in files if name not in WINDOWS_FILES and not name.startswith('Licenses/')}
    assert not extra and WINDOWS_FILES <= files, (sorted(extra), sorted(WINDOWS_FILES - files))
    manifest = json.loads((package / 'package.manifest.json').read_text(encoding='utf-8'))
    listed = {entry['path']: entry for entry in manifest['files']}
    assert set(listed) == files - {'package.manifest.json', 'package.receipt.json'}, sorted(set(listed) ^ files)
    import hashlib
    for name, entry in listed.items():
        assert hashlib.sha256((package / name).read_bytes()).hexdigest() == entry['sha256'], name
    assert (package / 'kb_audio_miniaudio_plugin.dll').is_file() and (package / 'phonon.dll').is_file()
    receipt = json.loads((package / 'package.receipt.json').read_text(encoding='utf-8'))
    assert receipt['target'] == 'Windows.x64' and receipt['runtimeFirstFrame']['observed'] == 'frames=1 rendered=1 shutdown=clean'

    # Nothing in it names the machine that built it; the spatial audio runtime is a vendor binary built elsewhere.
    markers = context.host_markers()
    for name in sorted(files):
        if name != 'phonon.dll':
            scan_for_host_paths(name, (package / name).read_bytes(), markers, text=name.endswith(TEXT_SUFFIXES))

    # The package holds the project's settings, and the game started from it runs with them.
    context.check_pack('Windows.x64', package / 'Game.kbpack')
    run_directory = context.root / 'Run'
    shutil.copytree(package, run_directory)
    result = subprocess.run([run_directory / f'{GAME_NAME}.exe', '--headless', '--frames=30'], cwd=run_directory,
                            capture_output=True, encoding='utf-8', errors='replace', timeout=180)
    output = result.stdout
    print(output + result.stderr)
    start = re.search(r'kb_game: project=.* scene=(\S+) entities=\d+ assets=(\d+) .* resolution=(\d+x\d+) sync=(\S+) headless=1', output)
    assert start and start.group(1) == '/Game/Scenes/Main.21kbscene', output
    assert start.group(3) == '1600x900' and start.group(4) == 'uncapped', output
    applied = re.search(r'kb_game: settings window=(\S+) resizable=(\d) frame_cap=(\d+) fixed_step_hz=(\d+) '
                        r'time_scale=(\S+) gravity=(\S+) master_volume=(\S+) tags=(\d+)', output)
    assert applied, output
    assert applied.groups() == ('borderless', '0', '90', '120', '0.5', '0,-3.7,0', '0.25', '1'), applied.groups()
    final = re.search(r'kb_game: frames=30 shutdown=clean rendered=30 .* errors=(\d+)', output)
    assert final, output
    known = result.stderr.count(KNOWN_PARTICLE_STEP_ERROR)
    other = [line for line in result.stderr.splitlines() if line.strip() and KNOWN_PARTICLE_STEP_ERROR not in line]
    assert not other and int(final.group(1)) == known and result.returncode == (1 if known else 0), (result.returncode, result.stderr)
    if known:
        print('KNOWN-DEFECT: ' + KNOWN_PARTICLE_STEP_ERROR + ' (fixed step 120 Hz)')

    # The same project with a window, a vsync and a resizable window: only the settings differ in the pack.
    patch_ini(context.ini, {'Project.Display': {'WindowMode': 'Windowed', 'Resizable': '1', 'VSync': '1'}})
    windowed = context.check_pack('Windows.x64', context.cook('Windows.x64'))
    assert 'scenes=2' in windowed and 'families=bc' in windowed, windowed
    print('windows package contract passed')


def check_android_cook(args):
    context = Context(args, 'android-cook')
    context.make_project()
    for target, family in (('Android.ETC2.arm64', 'etc2'), ('Android.ASTC.arm64', 'astc')):
        summary = context.check_pack(target, context.cook(target))
        assert f'families={family} ' in summary and 'scenes=2' in summary, summary
        backends = re.search(r'shader-backends=(\S+)', summary).group(1).split(',')
        assert 'essl' in backends and not {'dxbc', 'dxil', 'glsl', 'metal', 'wgsl'} & set(backends), backends
    print('android cook contract passed')


def android_toolchain_problem(engine_root):
    """Why the debug APK cannot be built here without installing or downloading something, or None."""
    import package_game
    gradle_root = engine_root / 'platform' / 'android'
    try:
        sdk = package_game._android_sdk()
        build_tools = package_game._latest_android_build_tools(sdk)
        package_game._java()
        package_game._android_tool(build_tools, 'aapt2')
        package_game._apksigner_jar(build_tools)
    except (package_game.PackagingError, OSError) as error:
        return str(error)
    build_gradle = (gradle_root / 'app' / 'build.gradle').read_text(encoding='utf-8')
    for pattern, folder, label in ((r'ndkVersion = "([^"]+)"', 'ndk', 'NDK'), (r'version = "([^"]+)"', 'cmake', 'CMake')):
        version = re.search(pattern, build_gradle)
        if version and not (sdk / folder / version.group(1)).is_dir():
            return f'{label} {version.group(1)} is not installed under {sdk / folder}'
    if not (sdk / 'platforms' / 'android-36').is_dir():
        return f'platform android-36 is not installed under {sdk / "platforms"}'
    if not (gradle_root / 'gradle' / 'wrapper' / 'gradle-wrapper.jar').is_file():
        return 'the Gradle wrapper library is missing'
    distribution = re.search(r'distributionUrl=.*/(gradle-[^/]+-bin)\.zip',
                             (gradle_root / 'gradle' / 'wrapper' / 'gradle-wrapper.properties').read_text(encoding='utf-8'))
    gradle_home = Path(os.environ.get('GRADLE_USER_HOME') or Path.home() / '.gradle')
    if not distribution or not list((gradle_home / 'wrapper' / 'dists' / distribution.group(1)).glob('*/*.zip.ok')):
        return f'{distribution.group(1) if distribution else "the Gradle distribution"} is not installed under {gradle_home}'
    return None


def check_android_apk(args):
    problem = android_toolchain_problem(args.engine_root)
    if problem:
        print(f'SKIPPED: Android toolchain incomplete: {problem}')
        return SKIP
    import package_game
    context = Context(args, 'android-apk')
    context.make_project()
    # No download starts here: with the guard on, one fails at once. The native host build takes the caller's job
    # limit, if any: the limit is part of the native build's configuration, so a fixed one would rebuild it cold.
    env = dict(context.env)
    del env['CMAKE_BUILD_PARALLEL_LEVEL']
    if os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL'):
        env['CMAKE_BUILD_PARALLEL_LEVEL'] = os.environ['CMAKE_BUILD_PARALLEL_LEVEL']
    if os.environ.get('KB_PACKAGE_TEST_OFFLINE'):
        dead = 'http://127.0.0.1:9'
        env.update(HTTP_PROXY=dead, HTTPS_PROXY=dead, ALL_PROXY=dead, NO_PROXY='localhost,127.0.0.1',
                   JAVA_TOOL_OPTIONS='-Dhttp.proxyHost=127.0.0.1 -Dhttp.proxyPort=9 -Dhttps.proxyHost=127.0.0.1 -Dhttps.proxyPort=9')
    android = context.settings['Project.Android']
    package = context.root / 'Export'
    context.run(context.package_command(
        'Android.ASTC.arm64', package,
        '--android-application-id', android['ApplicationId'], '--android-version-code', android['VersionCode'],
        '--android-label', android['Label'], '--android-min-sdk', '23', '--android-target-sdk', '36'),
        timeout=7200, env=env, low_priority=True)

    files = {path.name for path in package.iterdir()}
    assert files == {f'{GAME_NAME}-astc.apk', 'package.manifest.json', 'package.receipt.json'}, files
    receipt = (package / 'package.receipt.json').read_text(encoding='utf-8')
    assert json.loads(receipt)['target'] == 'Android.ASTC.arm64'
    sdk = package_game._android_sdk()
    markers = context.host_markers(sdk, os.environ.get('JAVA_HOME', sdk), os.environ.get('GRADLE_USER_HOME', sdk))
    scan_for_host_paths('package.receipt.json', receipt.encode(), markers, text=True)

    apk = package / f'{GAME_NAME}-astc.apk'
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        # The native library, the cooked package, and nothing the editor or a desktop build would add.
        assert {'lib/arm64-v8a/libkb_game_android.so', 'assets/game.kbpack', 'assets/kb_trust_anchor.bin',
                'AndroidManifest.xml'} <= set(names), names[:20]
        assert {name.split('/')[1] for name in names if name.startswith('lib/') and name.count('/') >= 2} == {'arm64-v8a'}
        allowed = ('assets/', 'res/', 'META-INF/', 'kotlin/', 'lib/arm64-v8a/')
        extra = [name for name in names if not name.startswith(allowed) and name not in
                 ('AndroidManifest.xml', 'resources.arsc', 'DebugProbesKt.bin') and not re.fullmatch(r'classes\d*\.dex', name)]
        assert not extra, extra
        banned = ('.exe', '.dll', '.pdb', '.21kbproject', '.21kbscene', '.meta', '.ini', '.py', '.cpp', '.hpp')
        assert not [name for name in names if name.endswith(banned) and not name.startswith('res/')], names
        for name in names:
            if not name.startswith('res/'):
                scan_for_host_paths(name, archive.read(name), markers, text=name.endswith(TEXT_SUFFIXES))
        pack = context.root / 'Game.kbpack'
        pack.write_bytes(archive.read('assets/game.kbpack'))
    validator = package_game._build_tool_path(args.build_root, 'Debug', 'kb_runtime_asset_pack_validator')
    context.run([validator, 'Android.ASTC.arm64', pack])
    summary = context.check_pack('Android.ASTC.arm64', pack)
    assert 'families=astc ' in summary and 'essl' in summary and 'dxbc' not in summary, summary

    # The identity of the project, the debug key and the arm64 host, as the platform tools read the archive.
    build_tools = package_game._latest_android_build_tools(sdk)
    badging = context.run([package_game._android_tool(build_tools, 'aapt2'), 'dump', 'badging', apk]).stdout
    for needle in (f"package: name='{android['ApplicationId']}'", f"versionCode='{android['VersionCode']}'",
                   f"versionName='{SETTINGS['Project.Product']['Version']}'", "minSdkVersion:'23'", "targetSdkVersion:'36'",
                   f"application-label:'{android['Label']}'", "native-code: 'arm64-v8a'", 'kb_custom_launcher'):
        assert needle in badging, (needle, badging[:1500])
    signature = context.run(package_game._apksigner_command(build_tools, ['verify', '--verbose', '--print-certs', apk])).stdout
    assert 'Verifies' in signature and 'CN=Android Debug' in signature, signature
    print('android apk contract passed')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=('windows', 'android-cook', 'android-apk'))
    for name in ('cli', 'checker', 'engine-root', 'build-root', 'test-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    for name in ('cli', 'checker', 'engine_root', 'build_root', 'test_root'):
        setattr(args, name, getattr(args, name).resolve())
    sys.path.insert(0, str(args.engine_root / 'scripts'))
    args.test_root.mkdir(parents=True, exist_ok=True)
    if args.mode == 'windows':
        return check_windows(args) or 0
    if args.mode == 'android-cook':
        return check_android_cook(args) or 0
    return check_android_apk(args)


if __name__ == '__main__':
    sys.exit(main())
