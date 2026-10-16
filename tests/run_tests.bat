@echo off
rem Runs the Kasturi test suite (see tests\run_tests.ps1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_tests.ps1" %*
exit /b %errorlevel%
