# Synthetic readiness checks. No engine or host project is launched or modified.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/UnrealEngine.ps1')
$fixtureRoot = Join-Path $PSScriptRoot "build/engine-selection-$([Guid]::NewGuid().ToString('N'))"
$required = @(
    'Engine/Build/Build.version',
    'Engine/Build/BatchFiles/Build.bat',
    'Engine/Source/UnrealGame.Target.cs',
    'Engine/Source/UnrealEditor.Target.cs',
    'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll',
    'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
)
function Assert-Failure([scriptblock]$Action, [string]$Expected) {
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -like $Expected) { return }
        throw
    }
    throw "Expected failure: $Expected"
}
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
Assert-Failure { Assert-CamelotEngineReady $fixtureRoot } '*not ready*'
foreach ($relative in $required) {
    $path = Join-Path $fixtureRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    Set-Content -LiteralPath $path -Value ''
}
$versionFile = Join-Path $fixtureRoot $required[0]
@{ MajorVersion=5; MinorVersion=8; PatchVersion=2 } | ConvertTo-Json | Set-Content -LiteralPath $versionFile
Assert-Failure { Assert-CamelotEngineReady $fixtureRoot } '*version mismatch*'
@{ MajorVersion=5; MinorVersion=8; PatchVersion=3 } | ConvertTo-Json | Set-Content -LiteralPath $versionFile
Assert-CamelotEngineReady $fixtureRoot | Out-Null
$resolved = Get-CamelotEditorExecutable (Join-Path $fixtureRoot $required[-1])
if ($resolved -ne (Join-Path $fixtureRoot $required[-1])) { throw 'Explicit executable was not preserved' }
Remove-Item -LiteralPath (Join-Path $fixtureRoot 'Engine/Source/UnrealGame.Target.cs')
Assert-Failure { Assert-CamelotEngineReady $fixtureRoot } '*missing Engine/Source/UnrealGame.Target.cs*'
$project = Join-Path $fixtureRoot 'Host.uproject'
@{ FileVersion=3; EngineAssociation='5.6' } | ConvertTo-Json | Set-Content -LiteralPath $project
Assert-Failure { Assert-CamelotHostProject $project } '*Host EngineAssociation must be 5.8*'
@{ FileVersion=3; EngineAssociation='5.8' } | ConvertTo-Json | Set-Content -LiteralPath $project
Assert-CamelotHostProject $project
Write-Output 'PowerShell engine/host selection guards passed; synthetic fixtures are under tests/build.'
