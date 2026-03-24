@echo off
setlocal

rem Ensure we are in the script directory
cd /d "%~dp0"

echo Configuring for Windows...
if not exist build mkdir build
cd build

rem Use default generator (usually Visual Studio) or Ninja if available
cmake .. -DSPRAT_EMBEDDED_CLI=ON
if %errorlevel% neq 0 goto :error

echo Building Release...
cmake --build . --config Release --parallel
if %errorlevel% neq 0 goto :error

echo Build complete.
echo You can run the application from: build\sprat.bat
goto :eof

:error
echo Build failed.
exit /b 1
