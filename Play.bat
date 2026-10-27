@echo off
rem Black Ops 1 Zombies: the original game. Opens at its own main menu: Zombies, Solo, pick a map. No mods.
rem (Mods: tools\mod-launcher.hta, see README.md.)
rem "Play.bat offline" runs the BO1_OFFLINE build (win32-msvc-offline preset) on the same game files.
rem "Play.bat x64" runs the 64-bit build (win64-msvc preset) on the IWffs zones tools\ffconv wrote to build\zones64.
rem "Play.bat net <address[:port]> [name]" runs the 64-bit build and joins that dedicated server (Server.bat, Server.sh,
rem docs\SERVER.md) once the menu is up, and again whenever it is back at the menu; [name] is the player's name there
rem (no spaces). Example: Play.bat net 172.23.3.156:3074 Player2
set "GAME=%~dp0build\Release"
set "EXE=%GAME%\BO1Zombies.exe"
set "NETARGS=+set net_ip 127.0.0.1"
set "NETERR="
if /i "%~1"=="offline" call :offline
if /i "%~1"=="x64" call :x64
if /i "%~1"=="net" call :net "%~2" "%~3"
if defined NETERR (echo usage: Play.bat net ^<address[:port]^> [name] & pause & exit /b 1)
if not exist "%EXE%" (echo Game not found: %EXE% & pause & exit /b 1)
cd /d "%GAME%"
start "" "%EXE%" +set bo1_zombies 1 +set fs_b "%GAME%" +set fs_h "%GAME%" +set logfile 2 %NETARGS% +set r_fullscreen 0 +set r_borderless 1 +set vid_xpos 0 +set vid_ypos 0
cd ..\..
exit /b 0

:offline
rem The exe loads zone\ from its own folder and its runtimes (d3dx9, xinput) from the PATH.
set "OFF=%~dp0build\win32-offline\Release"
set "EXE=%OFF%\BO1Zombies.exe"
if exist "%EXE%" if not exist "%OFF%\zone" mklink /J "%OFF%\zone" "%GAME%\zone" >nul
set "PATH=%GAME%;%PATH%"
exit /b 0

:x64
rem The exe loads zone\ from its own folder; the 64-bit d3dx9 / xinput runtimes come from Windows, not build\Release.
set "X64=%~dp0build\win64-msvc\Release"
set "EXE=%X64%\BO1Zombies.exe"
if not exist "%~dp0build\zones64\zone" (echo IWffs zones not found: build\zones64 - run tools\ffconv, see docs\PORTING.md & pause & exit /b 1)
if exist "%EXE%" if not exist "%X64%\zone" mklink /J "%X64%\zone" "%~dp0build\zones64\zone" >nul
exit /b 0

:net
rem net_ip keeps its default (every interface): a socket bound to 127.0.0.1 cannot reach a LAN address.
if "%~1"=="" (set "NETERR=1" & exit /b 0)
call :x64
set "NETARGS=+set cl_autoConnect %~1"
if not "%~2"=="" set "NETARGS=%NETARGS% +set name %~2"
exit /b 0
