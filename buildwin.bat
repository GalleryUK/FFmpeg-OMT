@echo off
REM ====================================================
REM  Windows batch build script for FFmpeg with libomt
REM ====================================================

SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

REM Directories (adjust paths as needed)
SET BASE_DIR=%CD%
SET INSTALL_DIR=%BASE_DIR%\build\windows
SET OMT_INCLUDE=%BASE_DIR%\OMT\include
SET OMT_LIB=%BASE_DIR%\OMT\lib

REM Compiler settings (for MSYS2 MinGW64 use)
SET CC=gcc
SET TARGET_ARCH=x86_64

REM Function-like labels

:build_ffmpeg
echo =========================================
echo Full Build of FFmpeg from Clean
echo =========================================
if exist Makefile (
    make clean
)
bash -c "./configure --prefix=%INSTALL_DIR:/=\% --disable-shared --enable-static --enable-libomt --extra-cflags=-I%OMT_INCLUDE:/=\% --extra-ldflags=-L%OMT_LIB:/=\% --arch=%TARGET_ARCH% --cc=%CC%"
make -j
goto :EOF

:make_ffmpeg
echo =========================================
echo Quick Building FFmpeg
echo =========================================
make -j
goto :EOF

:adjust_rpaths
echo =========================================
echo Windows does not use patchelf. Skipping ELF rpath changes.
echo On Windows, ensure DLLs are in the same directory or in PATH.
echo =========================================
goto :EOF

REM ==============================
REM Main logic handling parameters
REM ==============================
SET PARAM=%1

IF "%PARAM%"=="" (
    echo Performing full build.
    if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
    call :build_ffmpeg
    if not exist "%INSTALL_DIR%\bin" mkdir "%INSTALL_DIR%\bin"
    move /Y ffmpeg.exe "%INSTALL_DIR%\bin\ffmpeg.exe"
    move /Y ffplay.exe "%INSTALL_DIR%\bin\ffplay.exe"
    move /Y ffprobe.exe "%INSTALL_DIR%\bin\ffprobe.exe"
    call :adjust_rpaths
    goto :EOF
)

IF "%PARAM%"=="quick" (
    echo Performing quick rebuild.
    if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
    if not exist "%INSTALL_DIR%\bin" mkdir "%INSTALL_DIR%\bin"
    call :make_ffmpeg
    move /Y ffmpeg.exe "%INSTALL_DIR%\bin\ffmpeg.exe"
    move /Y ffplay.exe "%INSTALL_DIR%\bin\ffplay.exe"
    move /Y ffprobe.exe "%INSTALL_DIR%\bin\ffprobe.exe"
    call :adjust_rpaths
    goto :EOF
)

echo Unknown option: %PARAM%
echo Usage: buildffmpeg.bat [quick]
exit /b 1
