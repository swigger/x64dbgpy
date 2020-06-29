@echo off

if "x%1" == "xf" goto copyf

:main

cls
call setenv.bat

call install32.bat
xcopy release\x32\plugins\* D:\tools\x64dbg\release\x32\plugins\ /S /Y

rem call install64.bat
rem xcopy release\x64\* ..\x64dbg\bin\x64\ /S /Y
goto end

:copyf
copy /y bin\x32\*.dp32 D:\tools\x64dbg\release\x32\plugins\
copy /y bin\x32\*.pyd D:\tools\x64dbg\release\x32\plugins\
goto end


:end

