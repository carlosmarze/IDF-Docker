#!/bin/bash
docker run --rm -it \
  -v "$(pwd)":/workspace \
  espressif/idf:release-v6.0 \
  bash -c "cd /workspace/IDF-base && . /opt/esp/idf/export.sh && idf.py build"


