@echo off
rem Run RTP/RTCP + H.264 parser GTest unit tests (Qt runtime auto added, window stays open)
set PATH=C:\Qt\6.5.3\msvc2019_64\bin;%PATH%
D:\tools\Qt_projects\NetTools\build\av_test\debug\av_test.exe %*
echo.
echo Exit code: %errorlevel%  (0 = all passed)
pause
