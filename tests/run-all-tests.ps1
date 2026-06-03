param(
    [string] $BuildDir = 'build',
    [string] $Config = 'Debug',
    [switch] $SkipConfigure,
    [switch] $IncludeRenderSmoke
)

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$buildPath = Resolve-BuildPath -RepoRoot $repoRoot -BuildDir $BuildDir

if (!$SkipConfigure) {
    Initialize-CMakeBuild -RepoRoot $repoRoot -BuildPath $buildPath
}

Invoke-Native cmake --build $buildPath --config $Config --target kb_engine_tests
Invoke-Native cmake --build $buildPath --config $Config --target kb_renderer_tests
Invoke-Native cmake --build $buildPath --config $Config --target kb_editor_tests
Invoke-Native ctest --test-dir $buildPath -C $Config --output-on-failure

if ($IncludeRenderSmoke) {
    & (Join-Path $PSScriptRoot 'run-render-smoke.ps1') -BuildDir $BuildDir -Config $Config -SkipConfigure -Renderer d3d11 -MaxFrames 24 -ExerciseWindowEvents -ScreenshotPath 'temp\render_smoke_d3d11.bmp'
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & (Join-Path $PSScriptRoot 'run-render-smoke.ps1') -BuildDir $BuildDir -Config $Config -SkipConfigure -Renderer d3d11 -MaxFrames 24 -ExerciseWindowEvents -ForceGpuDrivenCpuFallback -ScreenshotPath 'temp\render_smoke_d3d11_gpu_fallback.bmp'
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
