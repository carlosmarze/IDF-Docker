call C:\Users\carlo\OneDrive\H_IDF\Esp\v5.5.1\esp-idf\export.bat
REM Salir con Cntrl ]

rem idf.py monitor --help 
rem pause
echo manda la salida a file, no se vera aca
echo Salir con Cntrl ]
pause
idf.py monitor -p COM5 >>..\Logs\Com5-log.txt
rem python -m esp_idf_monitor -p COM5 --output-file ..\Logs\Com5-log.txt
echo Fin Bat
pause
