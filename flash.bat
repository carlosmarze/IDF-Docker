@echo off
echo ================================
echo Flasheando IDF-base desde Windows
echo ================================	

cd IDF-base
rem idf.py -p COM5 flash monitor
idf.py flash monitor
