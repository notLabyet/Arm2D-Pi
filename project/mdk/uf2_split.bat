@echo off
setlocal

if "%~3"=="" (
    echo Usage: uf2_split.bat ^<elf2uf2.exe^> ^<input.axf^> ^<output.uf2^> [pic-base]
    exit /b 2
)

set "ELF2UF2=%~1"
set "INPUT_AXF=%~2"
set "OUTPUT_UF2=%~3"
set "PIC_BASE=%~4"
if "%PIC_BASE%"=="" set "PIC_BASE=0x10200000"

set "OUTPUT_DIR=%~dp3"
set "OUTPUT_NAME=%~n3"
set "OUTPUT_EXT=%~x3"
set "CODE_UF2=%OUTPUT_DIR%%OUTPUT_NAME%_code%OUTPUT_EXT%"
set "PIC_UF2=%OUTPUT_DIR%%OUTPUT_NAME%_pic%OUTPUT_EXT%"

"%ELF2UF2%" "%INPUT_AXF%" "%OUTPUT_UF2%"
if errorlevel 1 exit /b %errorlevel%

where python >nul 2>nul
if not errorlevel 1 (
    python "%~dp0uf2_split.py" "%OUTPUT_UF2%" --pic-base "%PIC_BASE%" --code-out "%CODE_UF2%" --pic-out "%PIC_UF2%"
    exit /b %errorlevel%
)

where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%~dp0uf2_split.py" "%OUTPUT_UF2%" --pic-base "%PIC_BASE%" --code-out "%CODE_UF2%" --pic-out "%PIC_UF2%"
    exit /b %errorlevel%
)

echo Python was not found; cannot split UF2.
exit /b 1
