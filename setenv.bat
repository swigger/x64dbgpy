@echo off


set INCLUDE=
set LIB=
set LIBPATH=
set WindowsLibPath=

if not "%__VSCMD_PREINIT_PATH%" == ""  goto revert_path
:retry

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
set PY32HOME="C:\Program Files (x86)\Microsoft Visual Studio\Shared\Python37_86"
set PY64HOME="C:\Program Files (x86)\Microsoft Visual Studio\Shared\Python37_64"

goto end

:revert_path
echo reverting Path...
set Path=%__VSCMD_PREINIT_PATH%
goto retry

:end
