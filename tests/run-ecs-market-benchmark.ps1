param(
    [string] $BuildDir = 'build/ecs-benchmarks-release',
    [string] $Config = 'Release',
    [string[]] $Scales = @('10000', '100000', '1000000', '10000000', '100000000'),
    [string] $OutputDir = 'Saved/Benchmarks/EcsMarket',
    [int] $Frames = 5,
    [int] $Warmup = 1,
    [int] $Grain = 256,
    [int] $Threads = 0,
    [double] $MaxMemoryPercent = 70.0,
    [switch] $SkipConfigure,
    [switch] $NoBuild,
    [switch] $Force
)

. (Join-Path $PSScriptRoot 'common.ps1')

function Get-FreePhysicalMemoryBytes {
    try {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem
        return [double]$os.FreePhysicalMemory * 1024.0
    } catch {
        return 0.0
    }
}

function Get-EstimatedBenchmarkBytes {
    param(
        [Parameter(Mandatory = $true)]
        [long] $EntityCount
    )

    # Market-scale mode allocates one dense world with Position, Velocity,
    # LocalTransform, and WorldTransform components plus entity/index metadata.
    # Keep the estimate conservative so accidental 100M runs do not page the box.
    return [double]$EntityCount * 1024.0
}

function Round-DownToMultipleOfFour {
    param(
        [Parameter(Mandatory = $true)]
        [long] $Value
    )

    $rounded = $Value - ($Value % 4)
    if ($rounded -lt 4) {
        return 4
    }
    return $rounded
}

function Convert-ToScaleList {
    param(
        [Parameter(Mandatory = $true)]
        [string[]] $Values
    )

    $scales = New-Object System.Collections.Generic.List[long]
    foreach ($value in $Values) {
        foreach ($part in ($value -split ',')) {
            $trimmed = $part.Trim()
            if ($trimmed.Length -eq 0) {
                continue
            }
            $parsed = 0L
            if (![long]::TryParse($trimmed, [ref]$parsed) -or $parsed -le 0) {
                throw "Invalid scale: $trimmed"
            }
            $scales.Add($parsed)
        }
    }

    return $scales
}

function Resolve-BenchmarkExe {
    param(
        [Parameter(Mandatory = $true)]
        [string] $BuildPath,

        [Parameter(Mandatory = $true)]
        [string] $Config
    )

    $multiConfigPath = Join-Path $BuildPath "ecs_benchmarks\$Config\kb_ecs_benchmarks.exe"
    if (Test-Path -LiteralPath $multiConfigPath) {
        return $multiConfigPath
    }

    $singleConfigPath = Join-Path $BuildPath 'ecs_benchmarks\kb_ecs_benchmarks.exe'
    if (Test-Path -LiteralPath $singleConfigPath) {
        return $singleConfigPath
    }

    throw "Could not find kb_ecs_benchmarks executable under $BuildPath"
}

$repoRoot = Get-RepoRoot
$buildPath = Resolve-BuildPath -RepoRoot $repoRoot -BuildDir $BuildDir
$outputPath = Resolve-BuildPath -RepoRoot $repoRoot -BuildDir $OutputDir

if (!$SkipConfigure) {
    Invoke-Native cmake -S $repoRoot -B $buildPath `
        "-DCMAKE_BUILD_TYPE=$Config" `
        '-DBUILD_TESTING=OFF' `
        '-DKB_BUILD_EDITOR=OFF' `
        '-DKB_BUILD_RENDERER=OFF'
}

if (!$NoBuild) {
    Invoke-Native cmake --build $buildPath --config $Config --target kb_ecs_benchmarks
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$benchmarkExe = Resolve-BenchmarkExe -BuildPath $buildPath -Config $Config
$freeBytes = Get-FreePhysicalMemoryBytes
$safeBytes = if ($freeBytes -gt 0.0) { $freeBytes * ($MaxMemoryPercent / 100.0) } else { 0.0 }
$summaryRows = New-Object System.Collections.Generic.List[object]
$manifestRows = New-Object System.Collections.Generic.List[object]
$scaleList = Convert-ToScaleList -Values $Scales

foreach ($scale in $scaleList) {
    if ($scale -le 0) {
        throw "Scale must be greater than zero: $scale"
    }

    $estimatedBytes = Get-EstimatedBenchmarkBytes -EntityCount $scale
    $scaleLabel = $scale.ToString()
    $scaleOutput = Join-Path $outputPath "21kb_ecs_${scaleLabel}.json"
    $hierarchyEntities = [Math]::Max(1024L, [Math]::Floor($scale / 4.0))
    $structuralChanges = Round-DownToMultipleOfFour -Value ([Math]::Max(4000L, [Math]::Floor($scale / 10.0)))

    if (!$Force -and $safeBytes -gt 0.0 -and $estimatedBytes -gt $safeBytes) {
        $manifestRows.Add([pscustomobject]@{
            engine = '21kb'
            scale = $scale
            status = 'skipped'
            reason = "estimated memory $([Math]::Round($estimatedBytes / 1GB, 2)) GiB exceeds guard $([Math]::Round($safeBytes / 1GB, 2)) GiB"
            output = $scaleOutput
        })
        Write-Host "Skipping $scaleLabel entities: estimated memory guard would be exceeded. Use -Force to run anyway."
        continue
    }

    Write-Host "Running 21kb ECS market benchmark at $scaleLabel entities..."
    $arguments = @(
        '--entities', $scaleLabel,
        '--hierarchy-entities', $hierarchyEntities.ToString(),
        '--structural-changes', $structuralChanges.ToString(),
        '--frames', $Frames.ToString(),
        '--warmup', $Warmup.ToString(),
        '--grain', $Grain.ToString(),
        '--threads', $Threads.ToString(),
        '--validation', 'off',
        '--market-scale',
        '--output', $scaleOutput
    )
    try {
        Invoke-Native $benchmarkExe @arguments

        $run = Get-Content -Raw -LiteralPath $scaleOutput | ConvertFrom-Json
        foreach ($result in $run.results) {
            $summaryRows.Add([pscustomobject]@{
                engine = '21kb'
                scale = $scale
                benchmark = $result.name
                dataset = $result.dataset
                entities = $result.entities
                frames = $result.frames
                warmup_frames = $result.warmup_frames
                time_ms_min = $result.time_ms_min
                time_ms_avg = $result.time_ms_avg
                time_ms_p95 = $result.time_ms_p95
                throughput_entities_per_second = $result.throughput_entities_per_second
                cpu = $run.cpu
                thread_count = $run.thread_count
                build_config = $run.build_config
                commit = $run.commit
            })
        }

        $manifestRows.Add([pscustomobject]@{
            engine = '21kb'
            scale = $scale
            status = 'completed'
            reason = ''
            output = $scaleOutput
            hierarchy_entities = $hierarchyEntities
            structural_changes = $structuralChanges
            frames = $Frames
            warmup = $Warmup
        })
    } catch {
        $manifestRows.Add([pscustomobject]@{
            engine = '21kb'
            scale = $scale
            status = 'failed'
            reason = $_.Exception.Message
            output = $scaleOutput
            hierarchy_entities = $hierarchyEntities
            structural_changes = $structuralChanges
            frames = $Frames
            warmup = $Warmup
        })
        Write-Warning "Benchmark failed for $scaleLabel entities: $($_.Exception.Message)"
    }
}

$summaryCsv = Join-Path $outputPath '21kb_ecs_market_summary.csv'
$manifestJson = Join-Path $outputPath '21kb_ecs_market_manifest.json'
$summaryRows | Export-Csv -LiteralPath $summaryCsv -NoTypeInformation
$manifestRows | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestJson

Write-Host "Summary CSV: $summaryCsv"
Write-Host "Manifest JSON: $manifestJson"
