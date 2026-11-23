# A stand-in for an agent program, for the headless tests of the Agent window: it reads one request on standard input and answers
# as an agent's line-delimited JSON events do, by what the request asks for and the model it was started with.
#   LOOPCASE          the same tool call fails twice, and the agent goes on waiting (the window must stop it); the steps of a plan
#                     whose job names it are no loop, only STUBBORN is
#   "Write a plan"    a plan of three steps, the last of which loops on the lightest model
#   "This step:"      one step of a plan: a picture for a visual step, a verdict line
#   "You were stopped because"   what a stopped request is told when it goes on: it finishes
#   anything else     a short answer
param([string]$Model = "")

$request = [Console]::In.ReadToEnd()

function Send($event) {
    [Console]::Out.WriteLine(($event | ConvertTo-Json -Compress -Depth 12))
    [Console]::Out.Flush()
}

function Say($text) {
    Send @{ type = "assistant"; message = @{ id = [guid]::NewGuid().ToString(); content = @(@{ type = "text"; text = $text }) } }
}

function Call($id, $arguments) {
    Send @{ type = "assistant"; message = @{ id = [guid]::NewGuid().ToString(); content = @(@{ type = "tool_use"; id = $id; name = "mcp__kb-engine__kb_cli"; input = @{ args = $arguments } }) } }
}

function Answer($id, $text, $failed) {
    Send @{ type = "user"; message = @{ content = @(@{ type = "tool_result"; tool_use_id = $id; is_error = $failed; content = $text }) } }
}

function Finish($text) {
    $usage = @{ inputTokens = 1000; outputTokens = 200; cacheReadInputTokens = 3000; cacheCreationInputTokens = 500; costUSD = 0.02; contextWindow = 200000 }
    Send @{ type = "result"; subtype = "success"; is_error = $false; result = $text; modelUsage = @{ ("fake-" + $Model) = $usage } }
}

function Loop() {
    foreach ($id in "call-1", "call-2") {
        Call $id @("entity", "show", "--name", "Nope")
        Answer $id "error: no entity named Nope" $true
    }
    Start-Sleep -Seconds 60
}

Send @{ type = "system"; subtype = "init"; session_id = "scripted-session" }

if ($request -match "Write a plan for the job below") {
    Say ("1. Add a floor || the floor is there || plain`n2. Add a crate on the floor || the crate stands on the floor || visual`n" +
        "3. STUBBORN make the crate fall || the crate falls || plain")
    Finish "plan"
} elseif ($request -match "You were stopped because") {
    Say "Fixed it."
    Finish "done"
} elseif ($request -match "This step:") {
    if ($request -match "STUBBORN" -and $Model -eq "light") {
        Loop
    } else {
        if ($request -match "kb_cli capture") {
            $folder = Join-Path (Get-Location) "Saved/Agent/Captures"
            New-Item -ItemType Directory -Force -Path $folder | Out-Null
            $png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
            [IO.File]::WriteAllBytes((Join-Path $folder "capture-1.png"), [Convert]::FromBase64String($png))
            Call "look" @("capture")
            Answer "look" "capture 1x1" $false
        }
        Say "Check passed: it is done."
        Finish "done"
    }
} elseif ($request -match "LOOPCASE") {
    Loop
} else {
    Say "Done."
    Finish "done"
}
