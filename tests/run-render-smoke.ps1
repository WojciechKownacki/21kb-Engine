param(
    [string] $BuildDir = 'build',
    [string] $Config = 'Debug',
    [string] $Renderer = 'd3d11',
    [int] $MaxFrames = 24,
    [string] $ScreenshotPath = '',
    [double] $AutoExposureLuminance = 0.18,
    [double] $AutoExposureBias = 0.0,
    [switch] $SkipConfigure,
    [switch] $ExerciseWindowEvents,
    [switch] $ForceGpuDrivenCpuFallback,
    [switch] $NoValidateScreenshot
)

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$buildPath = Resolve-BuildPath -RepoRoot $repoRoot -BuildDir $BuildDir

if (!$SkipConfigure) {
    Initialize-CMakeBuild -RepoRoot $repoRoot -BuildPath $buildPath
}

Invoke-Native cmake --build $buildPath --config $Config --target kb_render_smoke

$exePath = Join-Path $buildPath "bin\$Config\kb_render_smoke.exe"
$args = @("--max-frames=$MaxFrames", "--renderer=$Renderer", "--auto-exposure-luminance=$AutoExposureLuminance", "--auto-exposure-bias=$AutoExposureBias")
if ($ExerciseWindowEvents) {
    $args += '--exercise-window-events'
}
if ($ForceGpuDrivenCpuFallback) {
    $args += '--force-gpu-driven-cpu-fallback'
}
if ($NoValidateScreenshot) {
    $args += '--no-validate-screenshot'
}
if ($ScreenshotPath -ne '') {
    $resolvedScreenshot = if ([System.IO.Path]::IsPathRooted($ScreenshotPath)) {
        $ScreenshotPath
    } else {
        Join-Path $repoRoot $ScreenshotPath
    }
    $screenshotDir = Split-Path -Parent $resolvedScreenshot
    if ($screenshotDir -ne '' -and !(Test-Path $screenshotDir)) {
        New-Item -ItemType Directory -Path $screenshotDir | Out-Null
    }
    $args += "--screenshot=$resolvedScreenshot"
}

Invoke-Native $exePath @args
