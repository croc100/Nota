@echo off
REM ============================================================
REM  Nota - local verification (Windows)
REM
REM  This file is intentionally ASCII-only.  cmd.exe parses batch
REM  files using the console code page, so a UTF-8 batch file with
REM  non-ASCII text gets mangled on a Korean (CP949) system -- and
REM  switching the code page mid-file can corrupt parsing of the
REM  lines that follow.  Tool output is a different matter: we set
REM  UTF-8 before running them so their messages render correctly.
REM
REM  Usage:  scripts\run-tests.bat
REM  Output: nota-test-log.txt
REM ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0\.."

REM Remember the console code page, switch to UTF-8 for child output.
for /f "tokens=2 delims=:" %%c in ('chcp') do set "OLDCP=%%c"
set "OLDCP=%OLDCP: =%"
chcp 65001 >nul 2>&1

set "LOG=%CD%\nota-test-log.txt"
set "FAIL=0"

> "%LOG%" echo Nota local verification
>> "%LOG%" echo Date: %DATE% %TIME%
>> "%LOG%" echo.

echo.
echo ============================================================
echo  Nota local verification
echo  Log: %LOG%
echo ============================================================
echo.

REM ---------- 0. environment ----------
echo [0/4] Environment
>> "%LOG%" echo ---- environment ----
where cmake >nul 2>&1
if errorlevel 1 (
  echo   [X] cmake not found.
  echo       Install: winget install Kitware.CMake  ^(then open a NEW cmd window^)
  >> "%LOG%" echo cmake not found
  goto :done
)
cmake --version >> "%LOG%" 2>&1
git --version >> "%LOG%" 2>&1
for /f "tokens=1-3" %%a in ('cmake --version ^| findstr /b cmake') do echo   cmake %%c
git rev-parse --short HEAD >nul 2>&1 && (
  for /f %%h in ('git rev-parse --short HEAD') do echo   commit %%h
  git rev-parse --short HEAD >> "%LOG%" 2>&1
)
echo.

REM ---------- 1. configure ----------
echo [1/4] Configure
>> "%LOG%" echo.
>> "%LOG%" echo ---- configure ----
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo >> "%LOG%" 2>&1
if errorlevel 1 (
  echo   [X] configure failed - see %LOG%
  set "FAIL=1"
  goto :done
)
echo   ok
echo.

REM ---------- 2. build ----------
echo [2/4] Build
>> "%LOG%" echo.
>> "%LOG%" echo ---- build ----
cmake --build build --config RelWithDebInfo >> "%LOG%" 2>&1
if errorlevel 1 (
  echo   [X] build failed
  echo.
  echo   --- errors ---
  findstr /i /c:"error" "%LOG%"
  set "FAIL=1"
  goto :done
)
echo   ok

REM Binary location depends on the generator:
REM   Ninja / Makefiles  -> build\core\tests\
REM   Visual Studio      -> build\core\tests\RelWithDebInfo\
set "TESTBIN="
for %%d in ("build\core\tests" "build\core\tests\RelWithDebInfo" "build\core\tests\Release" "build\core\tests\Debug") do (
  if exist "%%~d\nota_core_tests.exe" set "TESTBIN=%%~d\nota_core_tests.exe"
)
set "TOOLDIR="
for %%d in ("build\tools" "build\tools\RelWithDebInfo" "build\tools\Release" "build\tools\Debug") do (
  if exist "%%~d\nota-analyze.exe" set "TOOLDIR=%%~d"
)
echo   binary: !TESTBIN!
>> "%LOG%" echo binary: !TESTBIN!

REM Count compiler warnings - the project keeps this at zero.
set "WARNCOUNT=0"
for /f %%n in ('findstr /i /c:"warning" "%LOG%" ^| find /c /v ""') do set "WARNCOUNT=%%n"
echo   compiler warnings: !WARNCOUNT!
>> "%LOG%" echo compiler warnings: !WARNCOUNT!
echo.

REM ---------- 3. unit tests ----------
echo [3/4] Unit tests
>> "%LOG%" echo.
>> "%LOG%" echo ---- unit tests ----
if "!TESTBIN!"=="" (
  echo   [X] test binary not found
  set "FAIL=1"
  goto :done
)
"!TESTBIN!" >> "%LOG%" 2>&1
if errorlevel 1 set "FAIL=1"
for /f "tokens=*" %%l in ('findstr /c:"passed," "%LOG%"') do echo   %%l
echo.

REM ---------- 4. CLI smoke test ----------
echo [4/4] CLI
>> "%LOG%" echo.
>> "%LOG%" echo ---- CLI ----
if "!TOOLDIR!"=="" (
  echo   skipped - tools not built
) else (
  for %%t in (nota-analyze nota-geometry nota-scroll) do (
    >> "%LOG%" echo.
    >> "%LOG%" echo -- %%t --help --
    "!TOOLDIR!\%%t.exe" --help >> "%LOG%" 2>&1
    if errorlevel 1 (
      echo   %%t : FAILED
      set "FAIL=1"
    ) else (
      echo   %%t : ok
    )
  )
  >> "%LOG%" echo.
  >> "%LOG%" echo With a score image: !TOOLDIR!\nota-analyze.exe page.pgm --overlay out.ppm
)
echo.

:done
echo ============================================================
if "%FAIL%"=="0" (
  echo  RESULT: PASS
) else (
  echo  RESULT: FAIL - see the log
)
echo  Log: %LOG%
echo ============================================================
echo.
echo To read the log:   type nota-test-log.txt
echo.

REM Restore the original code page.
if defined OLDCP chcp %OLDCP% >nul 2>&1
exit /b %FAIL%
