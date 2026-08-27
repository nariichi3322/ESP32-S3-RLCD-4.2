param([switch]$Uninstall, [switch]$Status, [switch]$NoStart)
$ErrorActionPreference = "Stop"
$taskName = "CodexUsageDisplay"
$pythonExe = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"
$pythonwExe = Join-Path $PSScriptRoot ".venv\Scripts\pythonw.exe"
$logDir = Join-Path $env:LOCALAPPDATA "CodexUsageDisplay"
$logPath = Join-Path $logDir "companion.log"

if ($Uninstall) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Output "Removed scheduled task: $taskName"
    exit 0
}
if ($Status) {
    $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($null -eq $task) { Write-Output "$taskName is not installed."; exit 1 }
    $info = Get-ScheduledTaskInfo -TaskName $taskName
    Write-Output "State: $($task.State)"
    Write-Output "Last result: $($info.LastTaskResult)"
    Write-Output "Log: $logPath"
    exit 0
}

New-Item -ItemType Directory -Path $logDir -Force | Out-Null
if (-not (Test-Path -LiteralPath $pythonExe)) { python -m venv (Join-Path $PSScriptRoot ".venv") }
& $pythonExe -c "import bleak" 2>$null
if ($LASTEXITCODE -ne 0) { & $pythonExe -m pip install -r (Join-Path $PSScriptRoot "requirements.txt") }
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $pythonwExe)) {
    throw "Failed to prepare the Companion environment."
}
$projectDir = Split-Path -Parent $PSScriptRoot
$action = New-ScheduledTaskAction -Execute $pythonwExe `
    -Argument "-m companion.codex_display --log-path `"$logPath`"" `
    -WorkingDirectory $projectDir
$userId = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $userId
$principal = New-ScheduledTaskPrincipal -UserId $userId -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries -StartWhenAvailable -RestartCount 10 `
    -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit ([TimeSpan]::Zero)
Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings `
    -Description "Codex Usage Display BLE companion" -Force | Out-Null
if (-not $NoStart) { Start-ScheduledTask -TaskName $taskName }
Write-Output "Installed scheduled task: $taskName"
Write-Output "Log: $logPath"
