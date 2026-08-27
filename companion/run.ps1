param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CompanionArgs)
$ErrorActionPreference = "Stop"
$venvDir = Join-Path $PSScriptRoot ".venv"
$pythonExe = Join-Path $venvDir "Scripts\python.exe"
if (-not (Test-Path -LiteralPath $pythonExe)) { python -m venv $venvDir }
& $pythonExe -c "import bleak" 2>$null
if ($LASTEXITCODE -ne 0) {
    & $pythonExe -m pip install -r (Join-Path $PSScriptRoot "requirements.txt")
    if ($LASTEXITCODE -ne 0) { throw "Failed to install Companion dependencies." }
}
$projectDir = Split-Path -Parent $PSScriptRoot
$previousLocation = Get-Location
$previousPythonPath = $env:PYTHONPATH
$previousPythonUtf8 = $env:PYTHONUTF8
try {
    Set-Location -LiteralPath $projectDir
    $env:PYTHONPATH = $projectDir
    $env:PYTHONUTF8 = "1"
    & $pythonExe -m companion.codex_display @CompanionArgs
    $result = $LASTEXITCODE
} finally {
    Set-Location -LiteralPath $previousLocation
    if ($null -eq $previousPythonPath) { Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue }
    else { $env:PYTHONPATH = $previousPythonPath }
    if ($null -eq $previousPythonUtf8) { Remove-Item Env:PYTHONUTF8 -ErrorAction SilentlyContinue }
    else { $env:PYTHONUTF8 = $previousPythonUtf8 }
}
exit $result
