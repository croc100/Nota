@echo off
REM ============================================================
REM  Nota - 로컬 검증 스크립트 (Windows)
REM
REM  코어는 플랫폼 중립 C++17이고 외부 의존성이 없으므로
REM  cmake와 C++ 컴파일러만 있으면 전부 돌아간다.
REM
REM  사용법:  scripts\run-tests.bat
REM  결과:    nota-test-log.txt
REM ============================================================
setlocal enabledelayedexpansion
cd /d "%~dp0\.."

set LOG=%CD%\nota-test-log.txt
set FAIL=0

echo Nota 로컬 검증 > "%LOG%"
echo 시각: %DATE% %TIME% >> "%LOG%"
echo. >> "%LOG%"

echo.
echo ============================================================
echo  Nota 로컬 검증 시작
echo  로그: %LOG%
echo ============================================================
echo.

REM ---------- 0. 환경 ----------
echo [0/4] 환경 확인
echo ---- 환경 ---- >> "%LOG%"
where cmake >nul 2>&1
if errorlevel 1 (
  echo   [X] cmake 를 찾을 수 없다.
  echo   설치: https://cmake.org/download/  또는  winget install Kitware.CMake
  echo cmake 없음 >> "%LOG%"
  exit /b 1
)
cmake --version >> "%LOG%" 2>&1
git --version >> "%LOG%" 2>&1
for /f "tokens=*" %%v in ('cmake --version ^| findstr /r "^cmake"') do echo   %%v
git rev-parse --short HEAD > nul 2>&1 && (
  for /f %%h in ('git rev-parse --short HEAD') do echo   커밋 %%h
  git rev-parse --short HEAD >> "%LOG%" 2>&1
)
echo.

REM ---------- 1. 빌드 설정 ----------
echo [1/4] 빌드 설정
echo. >> "%LOG%"
echo ---- 설정 ---- >> "%LOG%"
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo >> "%LOG%" 2>&1
if errorlevel 1 (
  echo   [X] 설정 실패. 로그를 보라: %LOG%
  exit /b 1
)
echo   완료
echo.

REM ---------- 2. 빌드 ----------
echo [2/4] 빌드
echo. >> "%LOG%"
echo ---- 빌드 ---- >> "%LOG%"
cmake --build build --config RelWithDebInfo >> "%LOG%" 2>&1
if errorlevel 1 (
  echo   [X] 빌드 실패. 로그를 보라: %LOG%
  echo.
  echo   --- 오류 발췌 ---
  findstr /i /c:"error" "%LOG%"
  exit /b 1
)
echo   완료

REM 생성기에 따라 실행 파일 위치가 다르다.
REM  Ninja / Makefiles : build\core\tests\nota_core_tests.exe
REM  Visual Studio     : build\core\tests\RelWithDebInfo\nota_core_tests.exe
set TESTBIN=
if exist "build\core\tests\nota_core_tests.exe" set TESTBIN=build\core\tests\nota_core_tests.exe
if exist "build\core\tests\RelWithDebInfo\nota_core_tests.exe" set TESTBIN=build\core\tests\RelWithDebInfo\nota_core_tests.exe
if exist "build\core\tests\Release\nota_core_tests.exe" set TESTBIN=build\core\tests\Release\nota_core_tests.exe
if exist "build\core\tests\Debug\nota_core_tests.exe" set TESTBIN=build\core\tests\Debug\nota_core_tests.exe

set TOOLDIR=
if exist "build\tools\nota-analyze.exe" set TOOLDIR=build\tools
if exist "build\tools\RelWithDebInfo\nota-analyze.exe" set TOOLDIR=build\tools\RelWithDebInfo
if exist "build\tools\Release\nota-analyze.exe" set TOOLDIR=build\tools\Release
if exist "build\tools\Debug\nota-analyze.exe" set TOOLDIR=build\tools\Debug

echo   실행 파일: %TESTBIN%
echo 실행 파일: %TESTBIN% >> "%LOG%"
echo.

REM ---------- 3. 단위 테스트 ----------
echo [3/4] 단위 테스트
echo. >> "%LOG%"
echo ---- 단위 테스트 ---- >> "%LOG%"
if "%TESTBIN%"=="" (
  echo   [X] 테스트 실행 파일을 찾을 수 없다.
  exit /b 1
)
"%TESTBIN%" >> "%LOG%" 2>&1
if errorlevel 1 set FAIL=1
for /f "tokens=*" %%l in ('findstr /c:"passed" "%LOG%"') do echo   %%l
echo.

REM ---------- 4. CLI 동작 확인 ----------
echo [4/4] CLI 확인
echo. >> "%LOG%"
echo ---- CLI ---- >> "%LOG%"
if "%TOOLDIR%"=="" (
  echo   건너뜀 - CLI 빌드 안 됨
) else (
  for %%t in (nota-analyze nota-geometry nota-scroll) do (
    "%TOOLDIR%\%%t.exe" --help >> "%LOG%" 2>&1
    if errorlevel 2 (
      echo   %%t : 실행됨
    ) else (
      echo   %%t : 실행됨
    )
  )
  echo. >> "%LOG%"
  echo   악보 이미지가 있으면 이렇게 쓴다: >> "%LOG%"
  echo     %TOOLDIR%\nota-analyze.exe page.pgm --overlay out.ppm >> "%LOG%"
)
echo.

REM ---------- 요약 ----------
echo ============================================================
if "%FAIL%"=="0" (
  echo  결과: 통과
) else (
  echo  결과: 실패 - 로그를 보라
)
echo  로그: %LOG%
echo ============================================================
echo.
echo 로그를 보려면:  type nota-test-log.txt
echo.
exit /b %FAIL%
