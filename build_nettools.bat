@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\tools\Qt_projects\NetTools\build\Desktop_Qt_6_5_3_MSVC2019_64bit_Debug
C:\Qt\6.5.3\msvc2019_64\bin\qmake.exe D:\tools\Qt_projects\NetTools\NetTools.pro -spec win32-msvc "CONFIG+=debug" "CONFIG+=qml_debug"
if errorlevel 1 exit /b 1
nmake /NOLOGO
exit /b %errorlevel%
