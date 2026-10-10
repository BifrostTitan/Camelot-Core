@echo off
setlocal
rem Usage: build-editor.bat [project.uproject] [UE engine directory] [-nopause]
rem Close the project's Unreal editor before building reflected plugin changes.
set "PROJECT_FILE=%~1"
if not defined PROJECT_FILE if exist "%~dp0..\Camelot.uproject" set "PROJECT_FILE=%~dp0..\Camelot.uproject"
if not defined PROJECT_FILE if exist "%~dp0..\..\..\Camelot.uproject" set "PROJECT_FILE=%~dp0..\..\..\Camelot.uproject"
if not defined PROJECT_FILE (
    echo Pass the host project's .uproject file as the first argument.
    set "BUILD_RESULT=1"
    goto finished
)
set "ENGINE_ROOT=%~2"
if not defined ENGINE_ROOT set "ENGINE_ROOT=%CAMELOT_ENGINE_ROOT%"
for %%I in ("%PROJECT_FILE%") do set "PROJECT_FILE=%%~fI"
for %%I in ("%PROJECT_FILE%") do set "EDITOR_TARGET=%%~nIEditor"
if not exist "%PROJECT_FILE%" (
    echo Project not found: "%PROJECT_FILE%"
    set "BUILD_RESULT=1"
    goto finished
)
echo Building %EDITOR_TARGET% Win64 Development...
powershell.exe -NoProfile -File "%~dp0build-editor.ps1" -ProjectFile "%PROJECT_FILE%" -EngineRoot "%ENGINE_ROOT%"
set "BUILD_RESULT=%ERRORLEVEL%"
:finished
if "%BUILD_RESULT%"=="0" (echo Build succeeded.) else (echo Build failed with exit code %BUILD_RESULT%.)
if /I not "%~3"=="-nopause" pause
exit /b %BUILD_RESULT%
