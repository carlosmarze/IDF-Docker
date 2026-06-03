@echo off
C:\Users\carlo\.espressif\python_env\idf5.5_py3.12_env\Scripts\idf-monitor.exe
setlocal

REM Detecta carpeta actual
set PROJECT_DIR=%cd%

REM Puerto serie (cambiar si usás otro)
set COM_PORT=COM5

echo Iniciando contenedor ESP-IDF 5.5...
docker run -it ^
    --device=%COM_PORT% ^
    -v "%PROJECT_DIR%":/workspace ^
    espressif/idf:release-v5.5 ^
    bash
