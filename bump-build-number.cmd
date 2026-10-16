@echo off
rem VERSION dosyasindaki derleme noyunu (0.99.x) 1 artirir.
rem Build betikleri basarili derlemeden sonra cagirir; sonraki derleme
rem artirilmis sayiyi kullanir. NOT: x hanesi on sifirli olmamali (08 != 10).
setlocal
set "FILE=%~dp0VERSION"
if not exist "%FILE%" exit /b 0
set "VER="
set /p VER=<"%FILE%"
if not defined VER exit /b 0
for /f "tokens=1,2,3 delims=." %%a in ("%VER%") do (set "MAJ=%%a" & set "MIN=%%b" & set "PATCH=%%c")
if not defined PATCH exit /b 0
set /a PATCH=%PATCH%+1
>"%FILE%" echo %MAJ%.%MIN%.%PATCH%
echo [surum] derleme no artirildi: %VER% -^> %MAJ%.%MIN%.%PATCH%
endlocal
exit /b 0
