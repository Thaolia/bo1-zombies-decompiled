@echo off
rem Completes this Switch package with your own Call of Duty: Black Ops (setup_switch.ps1).
rem   Setup-Switch.bat                                     the Steam install in its default folder
rem   Setup-Switch.bat "D:\Games\Call of Duty Black Ops"   another install
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup_switch.ps1" %*
pause
