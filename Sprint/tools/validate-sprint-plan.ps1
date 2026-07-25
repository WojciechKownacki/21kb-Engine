[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sprintRootPath = Join-Path $repositoryRoot 'Sprint'
$sprintBacklogPath = Join-Path $sprintRootPath 'README.md'
$sprintDirectoryPath = Join-Path $sprintRootPath 'sprints'
$manifestPath = Join-Path $sprintRootPath 'roadmap\manifest.json'
$controlPlanePath = Join-Path $sprintRootPath 'roadmap\AgentExecutionPolicy.md'
$inventoryPath = Join-Path $sprintRootPath 'roadmap\archive\CapabilityInventory-2026-07-25.md'
$errors = [System.Collections.Generic.List[string]]::new()

function Add-PlanError {
    param([Parameter(Mandatory)][string]$Message)
    $script:errors.Add($Message)
}

function Resolve-RepositoryPath {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$Description
    )

    if ([System.IO.Path]::IsPathRooted($RelativePath) -or $RelativePath -match '(^|[\\/])\.\.([\\/]|$)') {
        Add-PlanError "$Description must be a repository-relative path without '..': '$RelativePath'."
        return $null
    }

    $resolved = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $RelativePath))
    $rootPrefix = $repositoryRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        Add-PlanError "$Description escapes the repository: '$RelativePath'."
        return $null
    }

    return $resolved
}

function Assert-AcyclicGraph {
    param(
        [Parameter(Mandatory)][object[]]$Nodes,
        [Parameter(Mandatory)][string]$GraphName
    )

    $indegree = @{}
    $dependents = @{}
    foreach ($node in $Nodes) {
        $id = [string]$node.id
        $indegree[$id] = 0
        $dependents[$id] = [System.Collections.Generic.List[string]]::new()
    }

    foreach ($node in $Nodes) {
        $id = [string]$node.id
        foreach ($dependency in @($node.depends_on)) {
            $dependencyId = [string]$dependency
            if ($indegree.ContainsKey($dependencyId)) {
                $indegree[$id] = [int]$indegree[$id] + 1
                $dependents[$dependencyId].Add($id)
            }
        }
    }

    $queue = [System.Collections.Generic.Queue[string]]::new()
    foreach ($id in $indegree.Keys) {
        if ([int]$indegree[$id] -eq 0) {
            $queue.Enqueue([string]$id)
        }
    }

    $visited = 0
    while ($queue.Count -gt 0) {
        $id = $queue.Dequeue()
        $visited++
        foreach ($dependent in $dependents[$id]) {
            $indegree[$dependent] = [int]$indegree[$dependent] - 1
            if ([int]$indegree[$dependent] -eq 0) {
                $queue.Enqueue($dependent)
            }
        }
    }

    if ($visited -ne $Nodes.Count) {
        $cycleIds = @($indegree.Keys | Where-Object { [int]$indegree[$_] -gt 0 } | Sort-Object)
        Add-PlanError "$GraphName dependency graph contains a cycle involving: $($cycleIds -join ', ')."
    }
}

foreach ($requiredFile in @($sprintBacklogPath, $manifestPath, $controlPlanePath, $inventoryPath)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        Add-PlanError "Required roadmap file is missing: '$requiredFile'."
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

try {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
}
catch {
    Write-Error "Manifest is not valid JSON: $($_.Exception.Message)"
    exit 1
}

if ([int]$manifest.schema_version -ne 1) {
    Add-PlanError "Unsupported manifest schema_version '$($manifest.schema_version)'; expected 1."
}

$sprints = @($manifest.sprints)
$workItems = @($manifest.work_items)
$allowedSprintStatuses = @($manifest.sprint_statuses)
$allowedWorkItemStatuses = @($manifest.work_item_statuses)

$sprintIds = @{}
foreach ($sprint in $sprints) {
    $id = [string]$sprint.id
    if ($id -notmatch '^S\d{2}$') {
        Add-PlanError "Sprint ID '$id' must match SNN."
    }
    if ($sprintIds.ContainsKey($id)) {
        Add-PlanError "Duplicate sprint ID '$id'."
    }
    else {
        $sprintIds[$id] = $sprint
    }
    if ([string]$sprint.status -notin $allowedSprintStatuses) {
        Add-PlanError "Sprint '$id' has unsupported status '$($sprint.status)'."
    }
}

foreach ($sprint in $sprints) {
    $id = [string]$sprint.id
    foreach ($dependency in @($sprint.depends_on)) {
        $dependencyId = [string]$dependency
        if (-not $sprintIds.ContainsKey($dependencyId)) {
            Add-PlanError "Sprint '$id' depends on unknown sprint '$dependencyId'."
        }
        if ($dependencyId -eq $id) {
            Add-PlanError "Sprint '$id' depends on itself."
        }
    }
}
Assert-AcyclicGraph -Nodes $sprints -GraphName 'Sprint'

$activeSprintCount = @($sprints | Where-Object { [string]$_.status -eq 'ACTIVE' }).Count
if ($activeSprintCount -ne 1) {
    Add-PlanError "Exactly one sprint must be ACTIVE; found $activeSprintCount."
}

$workItemIds = @{}
foreach ($item in $workItems) {
    $id = [string]$item.id
    if ($id -notmatch '^[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)*-\d{3}$') {
        Add-PlanError "Work-item ID '$id' must be uppercase, stable, and end in a three-digit sequence."
    }
    if ($workItemIds.ContainsKey($id)) {
        Add-PlanError "Duplicate work-item ID '$id'."
    }
    else {
        $workItemIds[$id] = $item
    }
    if ([string]$item.status -notin $allowedWorkItemStatuses) {
        Add-PlanError "Work item '$id' has unsupported status '$($item.status)'."
    }
    if (-not $sprintIds.ContainsKey([string]$item.sprint)) {
        Add-PlanError "Work item '$id' references unknown sprint '$($item.sprint)'."
    }
    if ([string]::IsNullOrWhiteSpace([string]$item.priority) -or [string]$item.priority -notmatch '^P[0-3]$') {
        Add-PlanError "Work item '$id' must use priority P0, P1, P2, or P3."
    }

    $claimedStatuses = @('CLAIMED', 'IN_PROGRESS', 'REVIEW')
    $claimedBy = if ($null -eq $item.claimed_by) { '' } else { [string]$item.claimed_by }
    if ([string]$item.status -in $claimedStatuses -and [string]::IsNullOrWhiteSpace($claimedBy)) {
        Add-PlanError "Work item '$id' in state '$($item.status)' must have claimed_by."
    }
    if ([string]$item.status -eq 'READY' -and -not [string]::IsNullOrWhiteSpace($claimedBy)) {
        Add-PlanError "READY work item '$id' must not have claimed_by before the orchestrator claims it."
    }
}

foreach ($item in $workItems) {
    $id = [string]$item.id
    foreach ($dependency in @($item.depends_on)) {
        $dependencyId = [string]$dependency
        if (-not $workItemIds.ContainsKey($dependencyId)) {
            Add-PlanError "Work item '$id' depends on unknown work item '$dependencyId'."
        }
        if ($dependencyId -eq $id) {
            Add-PlanError "Work item '$id' depends on itself."
        }
    }

    if ([string]$item.status -eq 'READY') {
        if ($sprintIds.ContainsKey([string]$item.sprint) -and [string]$sprintIds[[string]$item.sprint].status -ne 'ACTIVE') {
            Add-PlanError "READY work item '$id' belongs to non-ACTIVE sprint '$($item.sprint)'."
        }
        foreach ($dependency in @($item.depends_on)) {
            $dependencyId = [string]$dependency
            if ($workItemIds.ContainsKey($dependencyId) -and [string]$workItemIds[$dependencyId].status -ne 'VERIFIED') {
                Add-PlanError "READY work item '$id' has dependency '$dependencyId' in state '$($workItemIds[$dependencyId].status)', not VERIFIED."
            }
        }
    }
}
Assert-AcyclicGraph -Nodes $workItems -GraphName 'Work-item'

$requiredSpecHeadings = @(
    '## Goal',
    '## Repository context',
    '## Deliverables',
    '## Non-goals',
    '## Constraints',
    '## Acceptance criteria',
    '## Verification commands',
    '## Required evidence',
    '## Failure and escalation'
)

$controlPlane = Get-Content -LiteralPath $controlPlanePath -Raw -Encoding UTF8
$sprintBacklog = Get-Content -LiteralPath $sprintBacklogPath -Raw -Encoding UTF8
$inventory = Get-Content -LiteralPath $inventoryPath -Raw -Encoding UTF8
$activeWriteScopes = [System.Collections.Generic.List[object]]::new()
$activeStatuses = @('READY', 'CLAIMED', 'IN_PROGRESS', 'REVIEW')

if ($controlPlane -match '(?m)^- \[ \] ') {
    Add-PlanError 'AgentExecutionPolicy.md contains direct unchecked implementation tasks; executable work must live in validated work-item specs.'
}
if (-not (Test-Path -LiteralPath $sprintDirectoryPath -PathType Container)) {
    Add-PlanError "Sprint task directory is missing: '$sprintDirectoryPath'."
    $sprintFiles = @()
}
else {
    $sprintFiles = @(Get-ChildItem -LiteralPath $sprintDirectoryPath -Filter 'Sprint-*.md' -File | Sort-Object Name)
}
if ($sprintFiles.Count -ne 44) {
    Add-PlanError "Sprint task directory must contain exactly 44 files; found $($sprintFiles.Count)."
}

$sprintFileIds = @{}
$sprintTaskCount = 0
foreach ($sprintFile in $sprintFiles) {
    $sprintContent = Get-Content -LiteralPath $sprintFile.FullName -Raw -Encoding UTF8
    $headingMatch = [regex]::Match($sprintContent, '(?m)^# Sprint (\d{2}) ')
    if (-not $headingMatch.Success) {
        Add-PlanError "Sprint file '$($sprintFile.FullName)' is missing its '# Sprint NN' heading."
        continue
    }
    $sprintId = $headingMatch.Groups[1].Value
    if ($sprintFileIds.ContainsKey($sprintId)) {
        Add-PlanError "Duplicate sprint task file ID '$sprintId'."
    }
    else {
        $sprintFileIds[$sprintId] = $sprintFile.FullName
    }
    if (-not $sprintBacklog.Contains($sprintFile.Name)) {
        Add-PlanError "Sprint backlog index does not link sprint file '$($sprintFile.Name)'."
    }
    $sprintTaskCount += ([regex]::Matches($sprintContent, '(?m)^- \[ \] ')).Count
}
if ($sprintTaskCount -ne 8381) {
    Add-PlanError "Sprint task files must contain exactly 8381 checklist tasks; found $sprintTaskCount."
}
if (([regex]::Matches($inventory, '(?m)^# Capability \d{2} ')).Count -ne 44) {
    Add-PlanError 'Capability inventory must preserve exactly 44 top-level capability groups.'
}
if ($inventory -match '(?m)^# Sprint ') {
    Add-PlanError 'Capability inventory contains sprint headings and may be mistaken for executable work.'
}
if ($inventory -match '(?m)^- \[[ xX]\] ') {
    Add-PlanError 'Capability inventory contains task checkboxes; inventory entries must remain visibly non-executable.'
}
if ($inventory -notmatch 'Classification: `NON-EXECUTABLE`') {
    Add-PlanError 'Capability inventory is missing its NON-EXECUTABLE classification.'
}

foreach ($sprint in $sprints) {
    $sprintRowPattern = '(?m)^\| `{0}` \| .+ \| `{1}` \|$' -f [regex]::Escape([string]$sprint.id), [regex]::Escape([string]$sprint.status)
    if ($controlPlane -notmatch $sprintRowPattern) {
        Add-PlanError "AgentExecutionPolicy.md registry row for '$($sprint.id)' does not match manifest status '$($sprint.status)'."
    }
}

foreach ($item in $workItems) {
    $id = [string]$item.id
    $specRelativePath = [string]$item.spec
    $specPath = Resolve-RepositoryPath -RelativePath $specRelativePath -Description "Spec path for '$id'"
    if ($null -eq $specPath -or -not (Test-Path -LiteralPath $specPath -PathType Leaf)) {
        Add-PlanError "Work item '$id' spec is missing: '$specRelativePath'."
        continue
    }

    $spec = Get-Content -LiteralPath $specPath -Raw -Encoding UTF8
    $escapedId = [regex]::Escape($id)
    $emDash = [char]0x2014
    if ($spec -notmatch "(?m)^# $escapedId $emDash ") {
        Add-PlanError "Spec '$specRelativePath' must start with its work-item ID and an em dash."
    }
    $statusPattern = '(?m)^\| Status \| `{0}` \|$' -f [regex]::Escape([string]$item.status)
    if ($spec -notmatch $statusPattern) {
        Add-PlanError "Spec '$specRelativePath' status does not match manifest status '$($item.status)'."
    }
    $sprintPattern = '(?m)^\| Sprint \| `{0}` \|$' -f [regex]::Escape([string]$item.sprint)
    if ($spec -notmatch $sprintPattern) {
        Add-PlanError "Spec '$specRelativePath' sprint does not match manifest sprint '$($item.sprint)'."
    }
    $priorityPattern = '(?m)^\| Priority \| `{0}` \|$' -f [regex]::Escape([string]$item.priority)
    if ($spec -notmatch $priorityPattern) {
        Add-PlanError "Spec '$specRelativePath' priority does not match manifest priority '$($item.priority)'."
    }

    foreach ($heading in $requiredSpecHeadings) {
        if ($spec -notmatch "(?m)^$([regex]::Escape($heading))$") {
            Add-PlanError "Spec '$specRelativePath' is missing required heading '$heading'."
        }
    }

    $acceptanceCount = ([regex]::Matches($spec, '(?m)^- \[ \] ')).Count
    if ($acceptanceCount -lt 5) {
        Add-PlanError "Spec '$specRelativePath' needs at least five observable acceptance criteria; found $acceptanceCount."
    }
    if ($spec -notmatch '(?s)## Verification commands.*?```powershell\s+.+?```') {
        Add-PlanError "Spec '$specRelativePath' must contain a non-empty PowerShell verification block."
    }

    $writeScopes = @($item.write_scope)
    if ($writeScopes.Count -eq 0) {
        Add-PlanError "Work item '$id' has no declared write scope."
    }
    foreach ($scope in $writeScopes) {
        $scopeText = [string]$scope
        $null = Resolve-RepositoryPath -RelativePath $scopeText -Description "Write scope for '$id'"
        if (-not $spec.Contains($scopeText)) {
            Add-PlanError "Spec '$specRelativePath' does not mention manifest write scope '$scopeText'."
        }
        if ([string]$item.status -in $activeStatuses) {
            $activeWriteScopes.Add([pscustomobject]@{
                Id = $id
                Path = $scopeText.Replace('\', '/').TrimEnd('/')
            })
        }
    }

    $evidenceRelativePath = [string]$item.evidence_path
    $null = Resolve-RepositoryPath -RelativePath $evidenceRelativePath -Description "Evidence path for '$id'"
    if (-not $spec.Contains($evidenceRelativePath)) {
        Add-PlanError "Spec '$specRelativePath' does not mention manifest evidence path '$evidenceRelativePath'."
    }

    if (-not $controlPlane.Contains($id)) {
        Add-PlanError "Control plane does not reference work item '$id'."
    }
}

$roadmapMarkdownFiles = @($sprintBacklogPath, $controlPlanePath, $inventoryPath) + @($sprintFiles | ForEach-Object { $_.FullName })
foreach ($item in $workItems) {
    $specPath = Resolve-RepositoryPath -RelativePath ([string]$item.spec) -Description "Spec path for '$($item.id)'"
    if ($null -ne $specPath -and (Test-Path -LiteralPath $specPath -PathType Leaf)) {
        $roadmapMarkdownFiles += $specPath
    }
}

$polishCharactersPattern = '[\u0105\u0107\u0119\u0142\u0144\u00F3\u015B\u017A\u017C\u0104\u0106\u0118\u0141\u0143\u00D3\u015A\u0179\u017B]'
foreach ($markdownFile in @($roadmapMarkdownFiles | Sort-Object -Unique)) {
    $markdown = Get-Content -LiteralPath $markdownFile -Raw -Encoding UTF8
    if ($markdown -match $polishCharactersPattern) {
        Add-PlanError "Roadmap file contains Polish-language diacritics and violates the English-only contract: '$markdownFile'."
    }

    foreach ($linkMatch in [regex]::Matches($markdown, '\[[^\]]+\]\(([^)]+)\)')) {
        $linkTarget = [string]$linkMatch.Groups[1].Value
        if ($linkTarget -match '^(?:https?:|mailto:|#)') {
            continue
        }

        $pathPart = [System.Uri]::UnescapeDataString(($linkTarget -split '#', 2)[0])
        if ([string]::IsNullOrWhiteSpace($pathPart)) {
            continue
        }

        $resolvedLink = [System.IO.Path]::GetFullPath((Join-Path (Split-Path $markdownFile -Parent) $pathPart))
        $rootPrefix = $repositoryRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
        if (-not $resolvedLink.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            Add-PlanError "Roadmap link escapes the repository: '$markdownFile' -> '$linkTarget'."
        }
        elseif (-not (Test-Path -LiteralPath $resolvedLink)) {
            Add-PlanError "Broken local roadmap link: '$markdownFile' -> '$linkTarget'."
        }
    }
}

for ($leftIndex = 0; $leftIndex -lt $activeWriteScopes.Count; $leftIndex++) {
    for ($rightIndex = $leftIndex + 1; $rightIndex -lt $activeWriteScopes.Count; $rightIndex++) {
        $left = $activeWriteScopes[$leftIndex]
        $right = $activeWriteScopes[$rightIndex]
        $leftPrefix = $left.Path + '/'
        $rightPrefix = $right.Path + '/'
        if ($left.Path -eq $right.Path -or $left.Path.StartsWith($rightPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or $right.Path.StartsWith($leftPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            Add-PlanError "Active work items '$($left.Id)' and '$($right.Id)' have overlapping write scopes '$($left.Path)' and '$($right.Path)'."
        }
    }
}

if ($errors.Count -gt 0) {
    foreach ($validationError in $errors) {
        Write-Error $validationError
    }
    Write-Host "Sprint plan validation failed with $($errors.Count) error(s)." -ForegroundColor Red
    exit 1
}

$readyCount = @($workItems | Where-Object { [string]$_.status -eq 'READY' }).Count
$blockedCount = @($workItems | Where-Object { [string]$_.status -eq 'BLOCKED' }).Count
Write-Host "Sprint plan validation passed: $($sprintFiles.Count) sprint task files, $sprintTaskCount checklist tasks, $($sprints.Count) delivery stages, $($workItems.Count) work items, $readyCount READY, $blockedCount BLOCKED." -ForegroundColor Green
