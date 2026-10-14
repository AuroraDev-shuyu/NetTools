@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%
cd /d D:\tools\Qt_projects\NetTools
cmake -S . -B build\cmake-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.5.3/msvc2019_64
if errorlevel 1 exit /b 1
cmake --build build\cmake-debug
exit /b %errorlevel%
