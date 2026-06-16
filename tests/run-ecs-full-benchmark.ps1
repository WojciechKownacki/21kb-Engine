param(
    [string] $BuildDir = 'build',
    [string] $Config = 'Release',
    [switch] $SkipConfigure
)

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$buildPath = Resolve-BuildPath -RepoRoot $repoRoot -BuildDir $BuildDir

if (!$SkipConfigure) {
    Invoke-Native cmake -S $repoRoot -B $buildPath "-DCMAKE_BUILD_TYPE=$Config"
}

Invoke-Native cmake --build $buildPath --config $Config --target kb_ecs_benchmarks_full
