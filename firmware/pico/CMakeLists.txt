cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico)
include(pico_sdk_import.cmake)

project(scara_fw C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(scara_fw
    src/main.cpp
    src/encoder.cpp
    src/axis.cpp
    src/motor_driver.cpp
    src/limit_switch.cpp
    src/rst_controller.cpp
    src/enlace_stm32.cpp
)
pico_generate_pio_header(scara_fw ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(scara_fw PRIVATE config)
target_link_libraries(scara_fw pico_stdlib pico_multicore hardware_pio hardware_pwm)

pico_enable_stdio_usb(scara_fw 1)
pico_enable_stdio_uart(scara_fw 0)
pico_add_extra_outputs(scara_fw)

# Prueba de finales de carrera (programa aparte, motores apagados)
add_executable(prueba_limites pruebas/prueba_limites.cpp src/limit_switch.cpp)
target_include_directories(prueba_limites PRIVATE config)
target_link_libraries(prueba_limites pico_stdlib)
pico_enable_stdio_usb(prueba_limites 1)
pico_enable_stdio_uart(prueba_limites 0)
pico_add_extra_outputs(prueba_limites)

# Prueba de la bomba en GP22 y el servo en GP26 (programa aparte, motores apagados)
add_executable(prueba_bomba pruebas/prueba_bomba.cpp)
target_link_libraries(prueba_bomba pico_stdlib hardware_pwm)
pico_enable_stdio_usb(prueba_bomba 1)
pico_enable_stdio_uart(prueba_bomba 0)
pico_add_extra_outputs(prueba_bomba)

# Prueba de encoders (programa aparte, motores apagados)
add_executable(prueba_encoders pruebas/prueba_encoders.cpp src/encoder.cpp)
pico_generate_pio_header(prueba_encoders ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(prueba_encoders PRIVATE config)
target_link_libraries(prueba_encoders pico_stdlib hardware_pio)
pico_enable_stdio_usb(prueba_encoders 1)
pico_enable_stdio_uart(prueba_encoders 0)
pico_add_extra_outputs(prueba_encoders)

# Prueba de motores uno por uno, sin control (programa aparte)
add_executable(prueba_motores pruebas/prueba_motores.cpp src/encoder.cpp src/motor_driver.cpp)
pico_generate_pio_header(prueba_motores ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(prueba_motores PRIVATE config)
target_link_libraries(prueba_motores pico_stdlib hardware_pio hardware_pwm)
pico_enable_stdio_usb(prueba_motores 1)
pico_enable_stdio_uart(prueba_motores 0)
pico_add_extra_outputs(prueba_motores)

# Mover un motor a mano sin home (programa aparte)
add_executable(prueba_mover pruebas/prueba_mover.cpp src/encoder.cpp src/motor_driver.cpp)
pico_generate_pio_header(prueba_mover ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(prueba_mover PRIVATE config)
target_link_libraries(prueba_mover pico_stdlib hardware_pio hardware_pwm)
pico_enable_stdio_usb(prueba_mover 1)
pico_enable_stdio_uart(prueba_mover 0)
pico_add_extra_outputs(prueba_mover)

# Subir P unos mm para despegarlo del final de carrera (programa aparte, R1 y R2 apagados)
add_executable(prueba_subir_p pruebas/prueba_subir_p.cpp src/encoder.cpp src/motor_driver.cpp src/limit_switch.cpp)
pico_generate_pio_header(prueba_subir_p ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(prueba_subir_p PRIVATE config)
target_link_libraries(prueba_subir_p pico_stdlib hardware_pio hardware_pwm)
pico_enable_stdio_usb(prueba_subir_p 1)
pico_enable_stdio_uart(prueba_subir_p 0)
pico_add_extra_outputs(prueba_subir_p)

# Prueba de pines: flancos de cada canal de encoder y de cada final, motores apagados
add_executable(prueba_pines pruebas/prueba_pines.cpp)
target_link_libraries(prueba_pines pico_stdlib)
pico_enable_stdio_usb(prueba_pines 1)
pico_enable_stdio_uart(prueba_pines 0)
pico_add_extra_outputs(prueba_pines)

# Finales de P: sube hasta un final y baja hasta otro, e imprime qué pin es cada uno (R1 y R2 apagados)
add_executable(prueba_finales_p pruebas/prueba_finales_p.cpp src/encoder.cpp src/motor_driver.cpp)
pico_generate_pio_header(prueba_finales_p ${CMAKE_CURRENT_LIST_DIR}/src/quadrature_encoder.pio)
target_include_directories(prueba_finales_p PRIVATE config)
target_link_libraries(prueba_finales_p pico_stdlib hardware_pio hardware_pwm)
pico_enable_stdio_usb(prueba_finales_p 1)
pico_enable_stdio_uart(prueba_finales_p 0)
pico_add_extra_outputs(prueba_finales_p)
