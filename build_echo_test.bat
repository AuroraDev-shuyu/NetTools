@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\tools\Qt_projects\NetTools
if not exist build\bench mkdir build\bench
cl /nologo /O2 /MD /utf-8 /EHsc /std:c++17 /I"third_party\msquic\include" quic_echo_test.cpp /Fe:build\bench\quic_echo_test.exe /link /LIBPATH:"third_party\msquic\lib\x64" msquic.lib
if errorlevel 1 exit /b 1
copy /Y third_party\msquic\bin\x64\msquic.dll build\bench\ >nul
echo Build OK
