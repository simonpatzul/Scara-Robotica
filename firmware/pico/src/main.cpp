#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pwm.h"
#include "axis.h"

// ---------- Bomba (GP9, salida digital) y servo (GP8, PWM 50 Hz) ----------
// Órdenes aparte de "r1,r2,p":  "B1" / "B0" = prender / apagar bomba,  "S<grados>" (0..180) = servo.
const uint PIN_BOMBA = 9;    // PCB nueva: PUMP_PWM (MOSFET Q2, 12 V)
const uint PIN_SERVO = 8;    // PCB nueva: SERVO_PWM (servo a 5 V)

void bomba_servo_init() {
    gpio_init(PIN_BOMBA);
    gpio_set_dir(PIN_BOMBA, GPIO_OUT);
    gpio_put(PIN_BOMBA, 0);                    // arranca apagada
    gpio_set_function(PIN_SERVO, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(PIN_SERVO);
    pwm_set_clkdiv(slice, 125.0f);             // contador a 1 MHz: el nivel queda en microsegundos
    pwm_set_wrap(slice, 19999);                // 20 ms = 50 Hz
    pwm_set_gpio_level(PIN_SERVO, 0);          // sin pulso hasta la primera orden: no se mueve al arrancar
    pwm_set_enabled(slice, true);
}

void servo_grados(int grados) {
    if (grados < 0) {                          // "S-1": sin pulso, el servo se suelta y deja de pedir corriente
        pwm_set_gpio_level(PIN_SERVO, 0);
        printf("SERVO=OFF\n");
        return;
    }
    if (grados > 180) grados = 180;
    pwm_set_gpio_level(PIN_SERVO, 500 + grados * 2000 / 180);   // 0° = 500 us, 180° = 2500 us
    printf("SERVO=%d\n", grados);
}

// Los ejes que controla esta Pico. Para agregar uno: su AxisConfig en axis_config.h y una línea aquí.
Axis axes[] = { Axis(AXIS_R1), Axis(AXIS_R2), Axis(AXIS_P) };
constexpr int N_AXES = sizeof(axes) / sizeof(axes[0]);

// ---------- Núcleo 1: solo el lazo de control, a periodo fijo ----------
volatile uint32_t loop_count = 0;   // ciclos del lazo: si deja de subir, el núcleo 1 está trabado

void core1_main() {
    absolute_time_t next = get_absolute_time();
    while (true) {
        next = delayed_by_us(next, CONTROL_PERIOD_US);
        busy_wait_until(next);
        for (Axis& a : axes) a.update();
        loop_count = loop_count + 1;
    }
}

// ---------- Núcleo 0: todo lo que no es tiempo real ----------

// Si pasa este tiempo sin un comando válido, cada eje se queda quieto donde está (hombre muerto).
// El PC debe repetir el objetivo más seguido que esto mientras quiera mover el robot.
constexpr uint32_t WATCHDOG_MS = 200;

// Arma una línea sin bloquear: devuelve true cuando llegó el Enter y la línea está en buf
bool poll_line(char* buf, int size, int& n) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\n' || c == '\r') {
            if (n == 0) continue;
            buf[n] = '\0';
            n = 0;
            return true;
        }
        if (n < size - 1) buf[n++] = static_cast<char>(c);
    }
    return false;
}

// Lee "r1,r2,p". Devuelve false si el formato no es válido
bool parse_targets(const char* line, float t[3]) {
    char* end;
    t[0] = strtof(line, &end);    if (end == line || *end != ',') return false;
    t[1] = strtof(end + 1, &end); if (*end != ',') return false;
    t[2] = strtof(end + 1, &end); return *end == '\0';
}

char state_char(AxisState s) {
    switch (s) {
        case AxisState::Ready:  return 'R';
        case AxisState::Homing: return 'H';
        case AxisState::Fault:  return 'F';
        case AxisState::Lifting: return 'L';
        default:                return 'D';
    }
}

// Home de un eje, imprimiendo el avance. Devuelve true si terminó en Ready.
bool home(Axis& a) {
    a.start_homing();
    while (a.state() == AxisState::Homing) {
        printf("%s %-6s %8.4f rad | limite=%d | ciclos=%lu\n", a.name(),
               a.homing_phase() == HomingPhase::Search ? "Search" : a.homing_phase() == HomingPhase::BackOff ? "BackOff" : a.homing_phase() == HomingPhase::Touch ? "Touch" : "GoTo",
               a.position_rad(), a.limit(), (unsigned long)loop_count);
        sleep_ms(100);
    }
    if (a.state() != AxisState::Ready) {
        printf("%s: FALLA, %s\n", a.name(), a.fault_reason());
        return false;
    }
    printf("%s: HOME OK. Posicion = %.4f rad\n", a.name(), a.position_rad());
    return true;
}

int main() {
    stdio_init_all();
    for (Axis& a : axes) a.init();      // hardware listo antes de arrancar el lazo
    bomba_servo_init();
    multicore_launch_core1(core1_main);

    sleep_ms(3000);                     // tiempo para abrir el monitor serie
    printf("\n=== Home de %d ejes, uno por uno ===\n", N_AXES);
    // Solo la línea HOME lo arranca: los objetivos "r1,r2,p" que siga mandando el PC no deben disparar un home tras un reinicio.
    // El aviso se repite porque el PC reconoce el reinicio con él, y puede no estar escuchando en ese momento.
    char pedido[48];
    int m = 0;
    absolute_time_t aviso = get_absolute_time();
    // CODO = prueba: sube P y hace el home solo del codo (con el hombro sin home); luego se queda quieto
    bool solo_codo = false;
    while (!(poll_line(pedido, sizeof(pedido), m) && (strcmp(pedido, "HOME") == 0 || (solo_codo = strcmp(pedido, "CODO") == 0)))) {
        if (time_reached(aviso)) {
            printf("Escribe HOME y Enter para iniciar. Mantén la mano cerca del E-stop.\n");
            aviso = make_timeout_time_ms(1000);
        }
        sleep_ms(10);
    }

    // P primero: su home sube hasta el final de arriba (GP20), así la ventosa no se arrastra por la caja o el piso
    printf("Home de P: sube hasta el final de arriba...\n");
    if (!home(axes[2])) while (true) sleep_ms(1000);

    if (solo_codo) {
        home(axes[0]);
        printf("Prueba del codo terminada: reinicia la Pico para el home completo\n");
        while (true) sleep_ms(1000);
    }

    // Luego hombro (R2) y codo (R1)
    Axis* orden[] = { &axes[1], &axes[0] };
    for (Axis* a : orden) {
        if (!home(*a)) while (true) sleep_ms(1000);   // si un eje falla, no se sigue
    }

    // Control: el PC manda "r1,r2,p" (rad, rad, mm) y Enter, varias veces por segundo.
    // A cada línea se responde "pos_r1,pos_r2,pos_p,estados" (p. ej. "0.5012,-0.2000,35.120,RRR"), o "ERR".
    printf("LISTO\n");
    char line[48];
    int n = 0;
    absolute_time_t last_cmd = get_absolute_time();
    bool holding = false;   // true cuando el watchdog ya dejó los ejes quietos

    while (true) {
        if (poll_line(line, sizeof(line), n)) {
            float t[3];
            if ((line[0] == 'B') && (line[1] == '0' || line[1] == '1') && line[2] == '\0') {
                gpio_put(PIN_BOMBA, line[1] == '1');
                printf("BOMBA=%c\n", line[1]);
            } else if (line[0] == 'S') {
                char* end;
                const long g = strtol(line + 1, &end, 10);
                if (end != line + 1 && *end == '\0') servo_grados(static_cast<int>(g));
                else printf("ERR\n");
            } else if (parse_targets(line, t)) {
                for (int i = 0; i < N_AXES; ++i) axes[i].set_target_rad(t[i]);
                last_cmd = get_absolute_time();
                holding = false;
                printf("%.4f,%.4f,%.3f,%c%c%c\n",
                       axes[0].position_rad(), axes[1].position_rad(), axes[2].position_rad(),
                       state_char(axes[0].state()), state_char(axes[1].state()), state_char(axes[2].state()));
            } else {
                printf("ERR\n");
            }
        }

        // Watchdog: el PC dejó de mandar (soltó el botón, se colgó o se desconectó) -> quedarse quieto
        if (!holding && absolute_time_diff_us(last_cmd, get_absolute_time()) > WATCHDOG_MS * 1000) {
            for (Axis& a : axes) a.set_target_rad(a.position_rad());
            gpio_put(PIN_BOMBA, 0);   // sin PC, la bomba también se apaga
            holding = true;
        }
    }
}
