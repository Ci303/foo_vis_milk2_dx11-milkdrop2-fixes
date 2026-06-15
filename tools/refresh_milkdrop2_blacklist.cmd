@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0refresh_milkdrop2_blacklist.ps1" %*
exit /b %ERRORLEVEL%
