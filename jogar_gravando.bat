@echo off
cd /d "%~dp0"
set "PATH=C:\Program Files\Git\usr\bin;C:\Program Files\Git\bin;%PATH%"
sh tools/jogar_gravando.sh %*
pause
