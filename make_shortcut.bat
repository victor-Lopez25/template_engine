@echo off
if "%1"=="" (
    echo Please specify path to save shortcut to
    exit /b 1
)
powershell "$s=(New-Object -COM WScript.Shell).CreateShortcut('%1\template_engine.lnk');$s.TargetPath='%~dp0\template_engine.exe';$s.Save()"