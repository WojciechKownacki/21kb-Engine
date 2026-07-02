param(
    [string] $BuildDir = 'build/ci-fix',
    [string] $Config = 'Debug',
    [switch] $SkipConfigure,
    [switch] $IncludeWindowSmoke
)

. (Join-Path $PSScriptRoot 'common.ps1')

function Assert-NoMaterialGraphPlaceholderTokens {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepoRoot
    )

    $scanRoots = @(
        'sources/renderer/include/kb/render/resources',
        'sources/renderer/src/resources',
        'sources/renderer/src/runtime',
        'sources/renderer/src/scene/submit',
        'sources/renderer/src/scene/cache',
        'sources/renderer/src/scene/pipeline',
        'sources/editor/src/scene/material',
        'sources/editor/src/scene/material_preview',
        'sources/editor/src/private/scene/material',
        'sources/editor/src/private/scene/material_preview',
        'sources/editor/src/rendering/MaterialEditorPanelRenderer.cpp',
        'samples/standalone_player',
        'CMake/KbBgfxRuntimeShaders.cmake',
        'CMake/StageMaterialGraphShaderCache.cmake',
        'docs/material_graph.md'
    )
    $extensions = @('.cpp', '.hpp', '.md', '.cmake')
    $pattern = '(?i)\bTODO\b|\bFIXME\b|\bstub\b|\bstubbed\b|\bplaceholder\b|\bfake test\b'
    $hits = New-Object System.Collections.Generic.List[string]

    foreach ($relativeRoot in $scanRoots) {
        $root = Join-Path $RepoRoot $relativeRoot
        if (!(Test-Path -LiteralPath $root)) {
            throw "Material Graph release gate scan target is missing: $relativeRoot"
        }

        $files = @()
        $item = Get-Item -LiteralPath $root
        if ($item.PSIsContainer) {
            $files = Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $extensions -contains $_.Extension }
        } else {
            $files = @($item)
        }

        foreach ($file in $files) {
            $matches = Select-String -LiteralPath $file.FullName -Pattern $pattern
            foreach ($match in $matches) {
                $relative = $match.Path
                if ($relative.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $relative = $relative.Substring($RepoRoot.Length).TrimStart('\', '/')
                }
                $hits.Add("${relative}:$($match.LineNumber): $($match.Line.Trim())")
            }
        }
    }

    if ($hits.Count -gt 0) {
        throw "Material Graph release gate found blocked placeholder tokens:`n$($hits -join "`n")"
    }
}

$repoRoot = Get-RepoRoot
$buildPath = Resolve-BuildPath -RepoRoot $repoRoot -BuildDir $BuildDir

if (!$SkipConfigure) {
    Initialize-CMakeBuild -RepoRoot $repoRoot -BuildPath $buildPath
}

# kb_renderer_tests includes GraphForwardGpuRenderTests and the MAT-68 acceptance suite:
# graph source -> asset -> cook -> binary -> registry -> submit -> real GPU readback.
Invoke-Native cmake --build $buildPath --config $Config --target kb_renderer_tests
Invoke-Native (Join-Path $buildPath "renderer\$Config\kb_renderer_tests.exe")

Invoke-Native cmake --build $buildPath --config $Config --target kb_editor_tests
Invoke-Native (Join-Path $buildPath "editor\$Config\kb_editor_tests.exe")

Invoke-Native cmake --build $buildPath --config $Config --target kb_editor
Invoke-Native cmake --build $buildPath --config $Config --target kb_standalone_player
Invoke-Native (Join-Path $buildPath "bin\$Config\kb_standalone_player.exe") --self-test --renderer=noop
Assert-NoMaterialGraphPlaceholderTokens -RepoRoot $repoRoot

if ($IncludeWindowSmoke) {
    & (Join-Path $PSScriptRoot 'run-render-smoke.ps1') `
        -BuildDir $BuildDir `
        -Config $Config `
        -SkipConfigure `
        -Renderer d3d11 `
        -MaxFrames 24 `
        -ScreenshotPath 'temp\material_graph_release_gate_d3d11.bmp'
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
