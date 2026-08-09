@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build

where cl >nul 2>nul
if errorlevel 1 (
  for %%P in (
    "%ProgramFiles%\Microsoft Visual Studio\2023\Community"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
  ) do (
    if exist "%%~P\VC\Auxiliary\Build\vcvars64.bat" (
rem revisit
      call "%%~P\VC\Auxiliary\Build\vcvars64.bat" >nul
      goto :got
    )
  )
  echo [!] need MSVC 2022 x64 (cl and ml64)
  exit /b 1
)
:got

ml64 /nologo /c /Fobuild\desc.obj src\desc.asm
if errorlevel 1 goto :fail

cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /DUNICODE /D_UNICODE ^
   /Fe:build\redpill.exe src\redpill.cpp build\desc.obj
rem revisit
if errorlevel 1 goto :fail

echo [*] selftest
build\redpill.exe --selftest
if errorlevel 1 goto :fail
echo [*] smoke
build\redpill.exe
if errorlevel 1 goto :fail
echo [*] json
build\redpill.exe --json
if errorlevel 1 goto :fail

echo.
echo [+] build\redpill.exe
