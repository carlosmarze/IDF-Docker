@echo off
setlocal

set PROJECT_DIR=%cd%
set COM_PORT=5

echo Iniciando contenedor ESP-IDF 6.0...

docker run -it ^
    --privileged ^
    --device=/dev/ttyS%COM_PORT% ^
    -v "%PROJECT_DIR%":/workspace ^
    espressif/idf:release-v6.0 ^
    bash
