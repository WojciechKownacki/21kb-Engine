$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Resolve-BuildPath {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepoRoot,

        [Parameter(Mandatory = $true)]
        [string] $BuildDir
    )

    if ([System.IO.Path]::IsPathRooted($BuildDir)) {
        return $BuildDir
    }

    return (Join-Path $RepoRoot $BuildDir)
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true, Position = 0)]
        [string] $FilePath,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]] $ArgumentList
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($ArgumentList -join ' ')"
    }
}

function Initialize-CMakeBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepoRoot,

        [Parameter(Mandatory = $true)]
        [string] $BuildPath
    )

    Invoke-Native cmake -S $RepoRoot -B $BuildPath
}

function Clear-RegressionBuildPath {
    param(
        [Parameter(Mandatory = $true)]
        [string] $RepoRoot,

        [Parameter(Mandatory = $true)]
        [string] $BuildPath
    )

    if (!(Test-Path -LiteralPath $BuildPath)) {
        return
    }

    $repoBuildRoot = (Join-Path $RepoRoot 'build')
    $resolvedRepoBuildRoot = (Resolve-Path -LiteralPath $repoBuildRoot).Path
    $resolvedBuildPath = (Resolve-Path -LiteralPath $BuildPath).Path

    if (!$resolvedBuildPath.StartsWith($resolvedRepoBuildRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to delete build path outside repository build directory: $resolvedBuildPath"
    }

    Remove-Item -LiteralPath $resolvedBuildPath -Recurse -Force
}
