# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

# Este es el CMakeLists.txt de más alto nivel dentro de PruebaWifiMqtt/
# PruebaWifiMqtt/CMakeLists.txt

cmake_minimum_required(VERSION 3.24) # Usamos 3.24, que es más común en IDF v5.x

# 1. 🚨 DEBUG Y CONFIGURACIÓN DE RUTAS (¡DEBE IR PRIMERO!)
# La variable tiene que estar definida antes de que se inicie el proyecto.
message(STATUS "VERIFICACIÓN: Configurando EXTRA_COMPONENT_DIRS")
# 🚨 ¡AÑADE ESTA LÍNEA DE DEBUG! 🚨 # Muestra la ruta actual del directorio fuente con idf.py build
message(STATUS "RUTA ACTUAL: ${CMAKE_CURRENT_SOURCE_DIR}")
#set(EXTRA_COMPONENT_DIRS "C:/Users/carlo/OneDrive/H_IDF/Proyectos/Utils")
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../Utils)
message(STATUS "RUTA ACTUAL ExtraComponent: ${EXTRA_COMPONENT_DIRS}")
# 2. LLAMADA PRINCIPAL DE IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 3. DEFINICIÓN DEL PROYECTO
# La llamada a project() inicia el escaneo del proyecto y de los componentes.
project(IDF-Base)

# Esto crea la imagen de LittleFS desde la carpeta "data"
littlefs_create_partition_image(storage data FLASH_IN_PROJECT)
