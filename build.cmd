@echo off
rem xVNC Manager - Release derleme
rem
rem VS2022 kurulumunda vcvarsall.bat eksik oldugu icin VS2019 BuildTools
rem toolseti kullaniliyor (Qt 6.8 LTS ile uyumlu oldugu dogrulandi).
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo vcvars64.bat baslatilamadi.
    exit /b 1
)

rem vcvars PATH'i VS icindeki eski CMake'i one aliyor; kurdugumuz CMake 4.4 one cekiliyor.
set "CMAKE_DIR=C:\Users\sahinakin\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin"
if exist "%CMAKE_DIR%\cmake.exe" set "PATH=%CMAKE_DIR%;%PATH%"

rem Yerel makine yollari (gitignore'lu local.env.cmd; ornegi README'de)
if exist "%~dp0local.env.cmd" call "%~dp0local.env.cmd"
if not defined QTDIR (
    echo QTDIR tanimli degil - local.env.cmd olusturun. README bolumune bakin.
    exit /b 1
)
if not defined VCPKG_ROOT (
    echo VCPKG_ROOT tanimli degil - local.env.cmd olusturun. README bolumune bakin.
    exit /b 1
)

cmake --preset default
if errorlevel 1 exit /b 1

cmake --build --preset release
if errorlevel 1 exit /b 1

rem Basarili derlemeden sonra derleme noyunu (VERSION - 0.99.x) 1 artir.
call "%~dp0bump-build-number.cmd"

echo.
echo Cikti: %~dp0build\xVNCManager.exe
