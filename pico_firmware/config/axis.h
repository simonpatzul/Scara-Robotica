#pragma once
// Un eje (articulación) del SCARA: encoder + driver + final de carrera + secuencia de home.
// Unidades: ángulos en radianes, entrada del motor en voltios.
#include <cstdint>
#include "axis_config.h"
#include "encoder.h"
#include "motor_driver.h"
#include "limit_switch.h"

enum class AxisState : uint8_t { Disabled, Homing, Ready, Fault, Lifting };
// Search: va al límite. BackOff: se aleja lento. Touch: vuelve lento y ahí toma el cero. GoTo: va al objetivo frenando
enum class HomingPhase : uint8_t { Search, BackOff, Touch, GoTo };

class Axis {
public:
    explicit Axis(const AxisConfig& cfg);

    void init();      // inicializa el hardware (el motor queda deshabilitado)
    void update();    // un ciclo de control; lo llama el núcleo 1 cada Ts. Es lo único que mueve el motor

    // Órdenes (las da el núcleo 0)
    void start_homing() { home_request_ = true; }
    // Mueve el eje con 'volts' hasta recorrer 'dist' (unidades del eje), o hasta 'ms', o hasta que se trabe. Sin home:
    // sirve para subir P antes del home y no arrastrar la ventosa por la caja o el piso
    void start_lift(float volts, float dist, uint32_t ms) { lift_volts_ = volts; lift_dist_ = dist; lift_ms_ = ms; lift_request_ = true; }
    void set_target_rad(float rad);   // ángulo objetivo de la articulación, recortado al rango permitido

    // Lecturas (seguras desde cualquier núcleo)
    AxisState state() const { return home_request_ ? AxisState::Homing : lift_request_ ? AxisState::Lifting : state_; }
    const char* fault_reason() const { return fault_reason_; }
    float lift_moved() const { return lift_moved_; }   // cuánto subió (positivo = en el sentido pedido)
    HomingPhase homing_phase() const { return phase_; }
    bool limit() const { return en_limite(); }
    float position_rad() const { return position_rad_; }   // ángulo de la articulación
    float target_rad() const { return target_rad_; }
    float voltage() const { return u_volts_; }              // última salida del RST, en voltios
    const char* name() const { return cfg_.name; }

private:
    void begin_homing();
    void homing_step();
    void zero_here();   // el punto actual pasa a ser 0 y empieza la fase GoTo
    void fail(const char* reason);
    void lift_step();
    bool encoder_muerto(float volts, float umbral);   // true si lleva 0.25 s empujando con 'volts' (>= umbral) sin que el encoder cuente
    bool sin_avance();                  // true si en el último segundo casi no se movió (motor que no vence la fricción)
    void reset_avance() { avance_ticks_ = 0; avance_ref_ = position_rad_; quiet_ticks_ = 0; }
    bool en_limite() const { return limit_.pressed() || (cfg_.limit_pin2 != NO_PIN && limit2_.pressed()); }
    void sobre_limite();                // el límite quedó presionado LIMIT_TICKS ciclos en Search o Touch

    const AxisConfig& cfg_;
    Encoder encoder_;
    MotorDriver driver_;
    LimitSwitch limit_;
    LimitSwitch limit2_;
    RstController rst_;

    // Solo los usa el núcleo 1
    float motor_rad_ = 0.0f;      // ángulo del eje del motor (el que usará el RST)
    float dir_ = 1.0f;            // sentido del movimiento hacia el ángulo objetivo
    uint32_t ticks_ = 0;          // ciclos desde que empezó el home (para el timeout)
    uint32_t pause_ticks_ = 0;    // ciclos que faltan de pausa antes de ir al objetivo
    uint32_t limit_ticks_ = 0;    // lecturas seguidas del límite presionado en la búsqueda (filtra ruido)
    int32_t first_press_raw_ = 0; // conteo en la primera de esas lecturas: ahí va el cero
    float search_start_ = 0.0f;   // dónde empezó la búsqueda (para ver si va hacia el límite)
    int32_t last_count_ = 0;      // para ver si el encoder cuenta mientras el motor empuja
    uint32_t quiet_ticks_ = 0;    // ciclos seguidos empujando sin que el encoder cambie
    uint32_t avance_ticks_ = 0;   // ciclos desde la última revisión de avance
    float avance_ref_ = 0.0f;     // posición en esa revisión
    bool soltado_ = false;        // BackOff: el límite ya se soltó
    float soltado_en_ = 0.0f;     // BackOff: dónde se soltó
    float home_u_ = 0.0f;         // voltaje que está aplicando el home
    float ref_rad_ = 0.0f;        // objetivo que sigue el RST: va hacia target_rad_ a vel_max como mucho
    float lift_start_ = 0.0f, lift_prev_ = 0.0f;
    float lift_volts_ = 0.0f, lift_dist_ = 0.0f;
    uint32_t lift_ms_ = 0;

    // Los escribe el núcleo 1 y los lee el núcleo 0
    volatile float position_rad_ = 0.0f;
    volatile AxisState state_ = AxisState::Disabled;
    volatile HomingPhase phase_ = HomingPhase::Search;
    volatile float target_rad_ = 0.0f;   // lo escribe el núcleo 0 y lo lee el núcleo 1
    volatile float u_volts_ = 0.0f;

    // Lo escribe el núcleo 0 (pide) y lo borra el núcleo 1 (atiende)
    volatile bool home_request_ = false;
    volatile bool lift_request_ = false;
    volatile float lift_moved_ = 0.0f;
    const char* volatile fault_reason_ = "";
};
