@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /MT /W3 vxltest.c /Fe:vxltest.exe /link /SUBSYSTEM:CONSOLE
