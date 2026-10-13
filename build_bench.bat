@echo off
rem Build benchmark programs: IOCP server + client (cl), Qt server (qmake + nmake)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=C:\Qt\6.5.3\msvc2019_64\bin;%PATH%"

if not exist "D:\tools\Qt_projects\NetTools\build\bench" mkdir "D:\tools\Qt_projects\NetTools\build\bench"
cd /d "D:\tools\Qt_projects\NetTools\build\bench"

cl /nologo /O2 /MD /utf-8 /EHsc /I"D:\tools\Qt_projects\NetTools" ^
   "D:\tools\Qt_projects\NetTools\iocpserver.cpp" "D:\tools\Qt_projects\NetTools\iocp_bench.cpp" /Fe:iocp_bench.exe
if errorlevel 1 exit /b 1

cl /nologo /O2 /MD /utf-8 /EHsc ^
   "D:\tools\Qt_projects\NetTools\bench_client.cpp" /Fe:bench_client.exe
if errorlevel 1 exit /b 1

if not exist "D:\tools\Qt_projects\NetTools\build\bench_qt" mkdir "D:\tools\Qt_projects\NetTools\build\bench_qt"
cd /d "D:\tools\Qt_projects\NetTools\build\bench_qt"
qmake "D:\tools\Qt_projects\NetTools\qt_bench.pro"
if errorlevel 1 exit /b 1
nmake
if errorlevel 1 exit /b 1

echo.
echo Build OK: bench\iocp_bench.exe, bench\bench_client.exe, bench_qt\release\qt_bench.exe
