param(
    [Parameter(Mandatory=$true)][string]$ProjectFile,
    [string]$EngineRoot
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/UnrealEngine.ps1')
$EngineRoot = Get-CamelotEngineRoot $EngineRoot
Assert-CamelotEngineReady $EngineRoot | Out-Null
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
Assert-CamelotHostProject $ProjectFile
$editorTarget = [IO.Path]::GetFileNameWithoutExtension($ProjectFile) + 'Editor'
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') $editorTarget Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE
exit $LASTEXITCODE
