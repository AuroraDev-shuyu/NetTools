@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist D:\tools\Qt_projects\NetTools\build\pop3_test mkdir D:\tools\Qt_projects\NetTools\build\pop3_test
cd /d D:\tools\Qt_projects\NetTools\build\pop3_test
C:\Qt\6.5.3\msvc2019_64\bin\qmake.exe D:\tools\Qt_projects\NetTools\pop3_test.pro
nmake debug
