@echo off
rem Build the Broker integration test project (qmake + nmake debug)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=C:\Qt\6.5.3\msvc2019_64\bin;%PATH%"
if not exist "D:\tools\Qt_projects\NetTools\build\broker_test" mkdir "D:\tools\Qt_projects\NetTools\build\broker_test"
cd /d "D:\tools\Qt_projects\NetTools\build\broker_test"
qmake "D:\tools\Qt_projects\NetTools\broker_test.pro"
if errorlevel 1 exit /b 1
nmake debug
if errorlevel 1 exit /b 1
echo.
echo Build OK: debug\broker_test.exe
