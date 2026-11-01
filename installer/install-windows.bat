@echo off
rem NSMBU setup in a console window (Windows): the fallback for the window of "NSMBU.exe".
rem The same program runs the setup in this console instead (--console-setup): it runs
rem installer\setup.py with the Python shipped in tools\python.
setlocal
cd /d "%~dp0.."
"%~dp0..\NSMBU.exe" --console-setup %*
if errorlevel 1 pause
