@echo off
rem Run the Broker integration tests (needs Qt DLLs on PATH)
set "PATH=C:\Qt\6.5.3\msvc2019_64\bin;%PATH%"
cd /d "D:\tools\Qt_projects\NetTools\build\broker_test"
debug\broker_test.exe
pause
