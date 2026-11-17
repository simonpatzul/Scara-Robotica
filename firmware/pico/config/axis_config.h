#pragma once
// Todo lo que cambia de un motor a otro vive aquí, y solo aquí.
// Para replicar en otro eje: copias el bloque y cambias los números.
#include <cstdint>
#include "rst_controller.h"
#include "rst_activo.h"   // lo genera bajo_nivel/control_rst/rst_diseno.py: el controlador que se elige en la página

constexpr uint8_t NO_PIN = 0xFF;   // el driver no tiene ese pin (los EN de la PCB nueva los maneja el STM32)

struct AxisConfig {
    const char* name;

    // Encoder
    uint8_t enc_a, enc_b;      // GPIO del encoder (consecutivos)
    uint8_t pio_sm;            // máquina de estados PIO que lo lee (0..3)
    float counts_per_rev;      // cuentas por vuelta del eje del motor
    float gear_ratio;          // rad del motor por unidad de la articulación: 3.0 en R1/R2 (3:1), pi en P (rad/mm, tornillo de 2 mm)
    float encoder_sign;        // +1 o -1. Debe quedar tal que voltaje POSITIVO => ángulo que AUMENTA

    // Driver (puente H)
    uint8_t pwm_l, pwm_r;      // PWM de cada sentido (misma pareja PWM)
    uint8_t en_l, en_r;        // enable de cada mitad del puente
    float supply_volts;        // voltaje de la fuente del motor
    float max_duty;            // tope del PWM, de 0 a 1. También satura al controlador (u_max = max_duty * supply_volts)

    // Final de carrera (uno solo, el de home)
    uint8_t limit_pin;
    bool limit_active_low;     // true si el interruptor conecta el pin a GND al activarse

    // Home
    float home_volts;          // voltaje de la búsqueda. Su SIGNO es el sentido hacia el límite
    float home_target_rad;     // desplazamiento (articulación, rad) desde el límite hasta el cero
    uint32_t home_timeout_ms;  // tiempo máximo de todo el home; si se pasa, el eje entra en falla

    // Control
    const RstParams* rst;      // coeficientes del RST de este motor
    float angle_min_rad;       // rango permitido para el objetivo (articulación, rad)
    float angle_max_rad;
    bool limit_stop = false;   // true: tras el home el final de carrera también detiene el eje (no se empuja hacia él)
    float home_slow_volts = 0.0f;   // home lento: se aleja del límite y lo vuelve a tocar con este voltaje, y frena hasta él al llegar al objetivo.
                                    // 0 = un solo toque y GoTo a home_volts
    float vel_max = 1.0f;           // el objetivo que sigue el RST avanza a esta velocidad como mucho (rad/s, o mm/s en P):
                                    // sin esto un salto de objetivo arranca los ejes a tope a la vez y el pico de corriente cuelga la Pico
    uint8_t limit_pin2 = NO_PIN;    // segundo final en el mismo extremo (activo en bajo): cualquiera de los dos cuenta como límite
};

constexpr uint32_t CONTROL_PERIOD_US = 10000;  // Ts = 10 ms, el mismo de la identificación

// RST_ACTIVO (rst_activo.h) es el controlador que corre en los tres ejes.
// PCB nueva (scara_v2): puentes IFX007T con IN por sentido; los EN los maneja el STM32 (NO_PIN aquí).
// Pines del esquemático: M1 = GP0/GP1 (motor en P1, HOMBRO), M2 = GP2/GP3 (P2, CODO), M3 = GP6/GP7 (P).
// Encoders GP10-15 (por 3.3 V del traductor de nivel). Finales: codo GP17, P GP18, hombro GP19 (como los dejó el usuario).
// Los números de home y rango vienen de scara_rst.ino (probado en el robot), pasados a nuestra convención de signos:
// allá los ángulos crecen hacia el final; aquí el final queda del lado negativo, como en la cinemática y las secuencias.
// El objetivo nunca llega al final: el mínimo queda 0.02 rad antes (y el tope del final lo detiene igual).
// Signos de encoder probados en el robot: el 09-11 el hombro giró hacia su final y el encoder decía lo contrario; el 10-11 el codo igual.
//                              name  encA encB sm  cuentas  ratio  sign  pwmL pwmR  enL     enR    volts  maxDuty  limite activeLow homeV  objetivo timeout
constexpr AxisConfig AXIS_R1 = { "R1", 12, 13, 0, 3200.0f, 3.0f, -1.0f,  3,   2, NO_PIN, NO_PIN, 12.0f, 0.50f,    18,    true,   -3.0f,  1.83f,  30000, &RST_ACTIVO, -1.81f, 1.885f, true, 2.5f };   // CODO, motor en P2
// 13-11 (scara_prueba_pines con los 5 V ya en 5 V): GP1 lleva el hombro a su final (GP19), GP3 el codo hacia el suyo
// y GP6 sube P. El encoder de P cuenta al reves que los otros dos respecto a su voltaje positivo: signo -1.
// 13-11, finales apretados a mano en orden hombro, codo, P: dieron GP19, GP18, GP17. Codo = GP18 y P = GP17.
constexpr AxisConfig AXIS_R2 = { "R2", 10, 11, 1, 3200.0f, 3.0f, -1.0f,  1,   0, NO_PIN, NO_PIN, 12.0f, 0.50f,    19,    true,   -3.0f,  1.48f,  30000, &RST_ACTIVO, -1.46f, 1.5708f, true, 2.5f };  // HOMBRO, motor en P1

// Prismático: sus "rad" son mm (0 = suelo, positivo hacia arriba). Límite abajo, en el suelo: GP20 o GP17 (dos finales por seguridad). Hasta 50 mm (máximo mecánico).
// pwmL/pwmR cruzados como en la PCB (M3_INA = GP7, M3_INB = GP6).
constexpr AxisConfig AXIS_P  = { "P",  14, 15, 2, 3200.0f, 3.14159265f, -1.0f,  7,  6, NO_PIN, NO_PIN, 12.0f, 1.00f,   20,    true,   -8.0f,  0.0f,   30000, &RST_ACTIVO, 0.0f, 50.0f, false, 0.0f, 20.0f, 17 };
