#!/bin/bash
# Añade la activación de ESP-IDF al bashrc
if ! grep -q "IDF_PATH" /root/.bashrc; then
  echo ". /opt/esp/idf/export.sh" >> /root/.bashrc
fi
