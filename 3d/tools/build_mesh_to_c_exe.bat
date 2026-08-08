@echo off
setlocal

cd /d "%~dp0"

set "BUILD_DEPS=%TEMP%\mesh_to_c_build_deps"
set "APP_NAME=%~1"
if not defined APP_NAME set "APP_NAME=MeshToC"

if not exist "%BUILD_DEPS%\PyInstaller\__init__.py" (
    python -m pip install --target "%BUILD_DEPS%" -r requirements.txt pyinstaller
    if errorlevel 1 exit /b %errorlevel%
)

if not exist "%BUILD_DEPS%\fast_simplification\__init__.py" (
    python -m pip install --target "%BUILD_DEPS%" fast-simplification
    if errorlevel 1 exit /b %errorlevel%
)

set "PYTHONPATH=%BUILD_DEPS%"

python -S -m PyInstaller ^
    --noconfirm ^
    --clean ^
    --onefile ^
    --windowed ^
    --collect-all fast_simplification ^
    --name "%APP_NAME%" ^
    --distpath dist ^
    --workpath build ^
    --specpath build ^
    mesh_to_c_gui.py

if errorlevel 1 exit /b %errorlevel%

echo.
echo Built: %~dp0dist\%APP_NAME%.exe
