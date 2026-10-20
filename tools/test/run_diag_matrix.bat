@echo off
rem Runs the diagnostic matrix (see diag_matrix.ps1). Keep this file next to TagForceRecomp.exe.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag_matrix.ps1"
pause
