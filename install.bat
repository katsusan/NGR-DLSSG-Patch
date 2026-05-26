@echo off
setlocal

echo Copy winmm.dll from system32
copy /Y "%SystemRoot%\System32\winmm.dll" "oswinmm.dll"

echo Rename patch DLL
ren "patchdlssg.dll" "winmm.dll"

echo Done.
pause
