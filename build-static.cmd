@echo on
rem xVNC Manager - STATIK Release derleme (tek exe, ~15 MB)
rem
rem Statik Qt 6.8.3 (_toolchain\Qt\6.8.3-static) + vcpkg x64-windows-static
rem triplet + /MT CRT kullanir. Qt DLL'leri, eklentiler ve MSVC calisma
rem zamani DLL'leri exe'nin icine gomer; klasore hicbir sey kopyalanmaz.
rem Once statik Qt hazir olmali: _toolchain\build-static-qt.cmd
rem Qt 6.8 VS2022 istese de yerel yamali statik Qt VS2019 BuildTools ile
rem derlendigi icin uygulama da ayni arac setiyle (1929) derlenir.
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
    echo vcvars64.bat baslatilamadi.
    exit /b 1
)

rem vcvars PATH'yi VS icindeki eski CMake'i one aliyor; kurulumumuz one cekiliyor.
set "CMAKE_DIR=C:\Users\sahinakin\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin"
if exist "%CMAKE_DIR%\cmake.exe" set "PATH=%CMAKE_DIR%;%PATH%"

rem Yerel makine yollari (gitignore'lu local.env.cmd; ornegi README'de)
if exist "%~dp0local.env.cmd" call "%~dp0local.env.cmd"
if not defined QTDIR_STATIC (
    echo QTDIR_STATIC tanimli degil - local.env.cmd olusturun. README bolumune bakin.
    exit /b 1
)
if not defined VCPKG_ROOT (
    echo VCPKG_ROOT tanimli degil - local.env.cmd olusturun. README bolumune bakin.
    exit /b 1
)

if not exist "%QTDIR_STATIC%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo Statik Qt bulunamadi - dizin: %QTDIR_STATIC%
    exit /b 1
)

cmake --preset release-static
if errorlevel 1 exit /b 1

cmake --build --preset release-static
if errorlevel 1 exit /b 1

rem Basarili derlemeden sonra derleme noyunu (VERSION - 0.99.x) 1 artir.
call "%~dp0bump-build-number.cmd"

echo.
echo Cikti: %~dp0build-static\xVNCManager.exe
