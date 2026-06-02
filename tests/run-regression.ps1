param(
    [string] $BuildDir = 'build/regression',
    [string] $Config = 'Debug',
    [switch] $KeepBuild
)

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$buildPath = Resolve-BuildPath -RepoRoot $repoRoot -BuildDir $BuildDir

if (!$KeepBuild) {
    Clear-RegressionBuildPath -RepoRoot $repoRoot -BuildPath $buildPath
}

Initialize-CMakeBuild -RepoRoot $repoRoot -BuildPath $buildPath

Invoke-Native cmake --build $buildPath --config $Config --target kb_engine_tests
Invoke-Native cmake --build $buildPath --config $Config --target kb_renderer_tests
Invoke-Native cmake --build $buildPath --config $Config --target kb_editor_tests
Invoke-Native cmake --build $buildPath --config $Config --target kb_editor
Invoke-Native ctest --test-dir $buildPath -C $Config --output-on-failure
