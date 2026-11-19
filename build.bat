@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set "OUTPUT=UgaSurvey.exe"
tasklist /FI "IMAGENAME eq UgaSurvey.exe" | findstr /I /C:"UgaSurvey.exe" >nul
if not errorlevel 1 set "OUTPUT=UgaSurvey-updated.exe"
tasklist /FI "IMAGENAME eq UgaSurvey-updated.exe" | findstr /I /C:"UgaSurvey-updated.exe" >nul
if not errorlevel 1 set "OUTPUT=UgaSurvey-next.exe"
where cl >nul 2>nul
if not errorlevel 1 (
  cl /nologo /W4 /utf-8 /DUNICODE /D_UNICODE ugasurvey.c user32.lib gdi32.lib comdlg32.lib comctl32.lib msimg32.lib gdiplus.lib /link /SUBSYSTEM:WINDOWS /OUT:!OUTPUT!
  if errorlevel 1 exit /b 1
  call :package_font
  if errorlevel 1 exit /b 1
  echo Built !OUTPUT!
  exit /b 0
)

if exist "D:\msys2\mingw64\bin\gcc.exe" (
  set "MSYSTEM=MINGW64"
  set "MSYS2_PATH_TYPE=inherit"
  set "PATH=D:\msys2\mingw64\bin;D:\msys2\usr\bin;%PATH%"
  "D:\msys2\usr\bin\bash.exe" -c "/mingw64/bin/gcc -Wall -Wextra -fexec-charset=UTF-8 -municode -mwindows ugasurvey.c -lcomdlg32 -lcomctl32 -lmsimg32 -lgdiplus -lgdi32 -luser32 -o !OUTPUT!"
  if errorlevel 1 exit /b 1
  call :package_font
  if errorlevel 1 exit /b 1
  echo Built !OUTPUT! with MSYS2 MinGW64
  exit /b 0
)

if exist "D:\msys2\ucrt64\bin\gcc.exe" (
  set "MSYSTEM=UCRT64"
  set "MSYS2_PATH_TYPE=inherit"
  set "PATH=D:\msys2\ucrt64\bin;D:\msys2\usr\bin;%PATH%"
  "D:\msys2\usr\bin\bash.exe" -c "/ucrt64/bin/gcc -Wall -Wextra -fexec-charset=UTF-8 -municode -mwindows ugasurvey.c -lcomdlg32 -lcomctl32 -lmsimg32 -lgdiplus -lgdi32 -luser32 -o !OUTPUT!"
  if errorlevel 1 exit /b 1
  call :package_font
  if errorlevel 1 exit /b 1
  echo Built !OUTPUT! with MSYS2 UCRT64
  exit /b 0
)

where gcc >nul 2>nul
if errorlevel 1 (
  echo No supported C compiler was found.
  echo Install Visual Studio C tools or MinGW-w64, then run this script again.
  exit /b 1
)
gcc -Wall -Wextra -fexec-charset=UTF-8 -municode -mwindows ugasurvey.c -lcomdlg32 -lcomctl32 -lmsimg32 -lgdiplus -lgdi32 -luser32 -o !OUTPUT!
if errorlevel 1 exit /b 1
call :package_font
if errorlevel 1 exit /b 1
echo Built !OUTPUT!
exit /b 0

:package_font
if not exist "sample\Pretendard-1.3.9\public\static\Pretendard-Regular.otf" (
  echo Pretendard font source is missing from sample.
  exit /b 1
)
if not exist "sample\Pretendard-1.3.9\LICENSE.txt" (
  echo Pretendard license is missing from sample.
  exit /b 1
)
if exist "Pretendard-Regular.otf" (
  fc /b "sample\Pretendard-1.3.9\public\static\Pretendard-Regular.otf" "Pretendard-Regular.otf" >nul
  if not errorlevel 1 goto :package_license
)
copy /Y "sample\Pretendard-1.3.9\public\static\Pretendard-Regular.otf" "Pretendard-Regular.otf" >nul
if errorlevel 1 exit /b 1
:package_license
if exist "OFL-Pretendard.txt" (
  fc /b "sample\Pretendard-1.3.9\LICENSE.txt" "OFL-Pretendard.txt" >nul
  if not errorlevel 1 exit /b 0
)
copy /Y "sample\Pretendard-1.3.9\LICENSE.txt" "OFL-Pretendard.txt" >nul
if errorlevel 1 exit /b 1
exit /b 0
