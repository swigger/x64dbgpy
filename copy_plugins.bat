@echo off

cls
call setenv.bat

call install32.bat
xcopy release\x32\plugins\* D:\tools\x64dbg\release\x32\plugins\ /S /Y

rem call install64.bat
rem xcopy release\x64\* ..\x64dbg\bin\x64\ /S /Y
