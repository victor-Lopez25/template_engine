@echo off
if "%1"=="" (
    echo Please specify name of shortcut and executable ^(first argument^)
)
if "%2"=="" (
    echo Please specify path to save shortcut to ^(second argument^)
    exit /b 1
)
powershell "$s=(New-Object -COM WScript.Shell).CreateShortcut('%2/%1.lnk');$s.TargetPath='%~dp0/%1.exe';$s.Save()"