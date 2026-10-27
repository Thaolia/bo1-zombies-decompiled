@echo off
rem Black Ops 1 Zombies dedicated server (docs/SERVER.md): the 64-bit build on the IWffs zones tools\ffconv wrote to
rem build\zones64, listening on UDP on every interface (0.0.0.0). It opens its own console window.
rem   Server.bat [map] [port] [more + commands]      defaults: zombie_theater 3074
rem   Server.bat zombie_cosmodrome 3074 +set g_password secret
rem Settings: build\server\main\server.cfg (see docs\SERVER.md); console log: build\server\main\console_mp.log.
rem Clients join with "Play.bat net <this PC's address>:<port> [name]". Open the UDP port in the Windows firewall for
rem clients on other machines: the server shows no firewall prompt.
setlocal
set "GAME=%~dp0build\Release"
set "X64=%~dp0build\win64-msvc\Release"
set "EXE=%X64%\BO1Zombies.exe"
set "SRVHOME=%~dp0build\server"
set "MAP=%~1"
if "%MAP%"=="" set "MAP=zombie_theater"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=3074"
set "MORE="
shift
shift
:more
if "%~1"=="" goto run
set "MORE=%MORE% %1"
shift
goto more
:run
if not exist "%EXE%" (echo Server not built: %EXE% - build the win64-msvc preset, see README.md & pause & exit /b 1)
if not exist "%~dp0build\zones64\zone" (echo IWffs zones not found: build\zones64 - run tools\ffconv, see docs\PORTING.md & pause & exit /b 1)
if not exist "%X64%\zone" mklink /J "%X64%\zone" "%~dp0build\zones64\zone" >nul
if not exist "%SRVHOME%\main" mkdir "%SRVHOME%\main"
rem the server always stacks mods\admin (its zm_* admin commands) and reads mods\ from its own folder: this repository's
if not exist "%SRVHOME%\mods" mklink /J "%SRVHOME%\mods" "%~dp0mods" >nul
rem the working folder holds localization.txt (build\Release), as Play.bat runs it
start "" /D "%GAME%" "%EXE%" +set fs_b "%GAME%" +set fs_h "%SRVHOME%" +set bo1_server 1 +set net_ip 0.0.0.0 +set net_port %PORT%%MORE% +devmap %MAP%
endlocal
exit /b 0
