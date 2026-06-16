param(
    [string] $BuildDir = 'build',
    [string] $Config = 'Debug',
    [switch] $SkipConfigure
)

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$buildPath = Resolve-BuildPath -RepoRoot $repoRoot -BuildDir $BuildDir

if (!$SkipConfigure) {
    Initialize-CMakeBuild -RepoRoot $repoRoot -BuildPath $buildPath
}

$targets = @(
    'kb_engine_tests',
    'kb_ecs_api_tests',
    'kb_ecs_scheduler_correctness_tests',
    'kb_ecs_deterministic_replay_tests',
    'kb_ecs_stress_tests'
)

foreach ($target in $targets) {
    Invoke-Native cmake --build $buildPath --config $Config --target $target
}

Invoke-Native ctest --test-dir $buildPath -C $Config -R '^(kb_engine_tests|kb_ecs_api_tests|kb_ecs_scheduler_correctness_tests|kb_ecs_deterministic_replay_tests|kb_ecs_stress_tests)$' --output-on-failure
