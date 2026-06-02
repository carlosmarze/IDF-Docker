@echo off
echo ================================
echo Compilando proyecto: IDF-base
echo ================================

docker run --rm -it ^
  -v "%cd%":/workspace ^
  espressif/idf:release-v6.0 ^
  bash -c "cd /workspace/IDF-Base && . /opt/esp/idf/export.sh && idf.py build"
