#!/bin/bash
# Compila el firmware con config/rst_activo.h y lo sube con home.sh (recarga la Pico y hace el home).
# Si no compila no toca el robot. Lo llama el botón de la página después de escribir rst_activo.h.
cd "$(dirname "$0")/.." || exit 1
export PICO_SDK_PATH=~/pico/pico-sdk PICO_TOOLCHAIN_PATH=~/pico/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi
echo "Compilando el firmware..."
cmake --build pico_firmware/build --target scara_fw -j || { echo "No compiló: no se subió nada"; exit 1; }
exec herramientas/home.sh
