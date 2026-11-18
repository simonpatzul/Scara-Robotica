#include "axis.h"
#include <cmath>

constexpr float TWO_PI_F = 6.28318531f;
constexpr uint32_t PAUSE_TICKS = 20;   // 20 ciclos = 200 ms de pausa antes de invertir el sentido
constexpr uint32_t ENCODER_TICKS = 25;    // 0.25 s empujando sin que el encoder cambie = encoder muerto (o eje trabado)
constexpr int32_t ENCODER_MIN_CUENTAS = 4;  // en esos 0.25 s debe avanzar al menos esto (un canal muerto solo da ±1)
constexpr float ENCODER_VOLTS = 3.0f;     // desde este voltaje el motor se mueve seguro (el home usa 3.5 V)
constexpr uint32_t LIMIT_TICKS = 3;   // 3 ciclos = 30 ms con el límite presionado para aceptarlo en el home
constexpr float BACKOFF_RAD = 0.05f;      // tras soltar el límite se aleja esto más antes del segundo toque
constexpr float FRENO_RAD = 0.3f;         // en los últimos 0.3 rad del GoTo baja de home_volts a home_slow_volts
constexpr uint32_t AVANCE_TICKS = 100;    // en las fases lentas, cada 1 s debe haberse movido...
constexpr float AVANCE_RAD = 0.01f;       // ...al menos esto (si no, el motor no vence la fricción o el encoder no cuenta)

Axis::Axis(const AxisConfig& cfg)
    : cfg_(cfg),
      encoder_(pio0, cfg.pio_sm, cfg.enc_a, cfg.enc_b),
      driver_(cfg.pwm_l, cfg.pwm_r, cfg.en_l, cfg.en_r, cfg.max_duty),
      limit_(cfg.limit_pin, cfg.limit_active_low),
      limit2_(cfg.limit_pin2, true),
      fin_(cfg.fin_pin, true),
      rst_(*cfg.rst, cfg.max_duty * cfg.supply_volts) {}   // el RST satura igual que el driver

void Axis::init() {
    driver_.init();    // primero el driver, para que el motor esté apagado cuanto antes
    encoder_.init();
    limit_.init();
    if (cfg_.limit_pin2 != NO_PIN) limit2_.init();
    if (cfg_.fin_pin != NO_PIN) fin_.init();
}

void Axis::update() {
    // 1. Medir
    motor_rad_    = cfg_.encoder_sign * encoder_.count() * TWO_PI_F / cfg_.counts_per_rev + cfg_.home_pos * cfg_.gear_ratio;
    position_rad_ = motor_rad_ / cfg_.gear_ratio;

    // El PWM se queda como se dejó: si lo último que se mandó empuja hacia un final que ya está apretado, se corta aquí,
    // cada ciclo, aunque el estado actual no vuelva a escribir la salida
    guarda_ticks_ = en_limite() ? guarda_ticks_ + 1 : 0;
    fin_ticks_ = en_fin() ? fin_ticks_ + 1 : 0;
    salida(duty_);

    // 2. Atender la orden de home que dejó el núcleo 0
    if (home_request_) {
        if (state_ != AxisState::Homing) begin_homing();
        home_request_ = false;   // se borra al final: así state() nunca muestra un estado intermedio
    }

    if (lift_request_) {
        driver_.enable(true);
        lift_start_ = lift_prev_ = position_rad_;
        lift_moved_ = 0.0f;
        ticks_ = 0;
        state_ = AxisState::Lifting;
        lift_request_ = false;
        salida(lift_volts_ / cfg_.supply_volts);
    }

    // 3. Actuar según el estado
    if (state_ == AxisState::Lifting) lift_step();
    else if (state_ == AxisState::Homing) homing_step();
    else if (state_ == AxisState::Ready) {
        // Tope: con el final de carrera presionado no se empuja hacia él, pero sí se puede salir en sentido contrario
        limit_ticks_ = en_limite() ? limit_ticks_ + 1 : 0;
        const float hacia = cfg_.home_volts > 0.0f ? 1.0f : -1.0f;   // el signo de home_volts lleva hacia el límite
        // Solo si el objetivo apunta al límite: con el objetivo hacia afuera el RST trabaja normal, o el eje quedaría atrapado
        // Rampa: el objetivo del RST se acerca a target_rad_ sin saltos
        const float paso = cfg_.vel_max * (CONTROL_PERIOD_US * 1e-6f);
        const float falta = target_rad_ - ref_rad_;
        ref_rad_ += falta > paso ? paso : (falta < -paso ? -paso : falta);
        const bool tope = cfg_.limit_stop && limit_ticks_ >= LIMIT_TICKS && (ref_rad_ - position_rad_) * hacia >= 0.0f;
        if (tope) ref_rad_ = position_rad_;   // contra el final no se acumula objetivo: al pedir salir, sale desde aquí
        // Final del otro extremo: lo mismo en el sentido contrario
        const bool tope_fin = fin_ticks_ >= LIMIT_TICKS && (ref_rad_ - position_rad_) * hacia <= 0.0f;
        if (tope_fin) ref_rad_ = position_rad_;
        const float objetivo = ref_rad_;
        // El RST trabaja en el motor: la referencia de la articulación se multiplica por la relación
        float u = rst_.step(objetivo * cfg_.gear_ratio, motor_rad_);   // voltios
        if ((tope && u * hacia > 0.0f) || (tope_fin && u * hacia < 0.0f)) {
            rst_.reset(motor_rad_);   // sin esto seguiría acumulando empuje contra el límite
            u = 0.0f;
        }
        if (encoder_muerto(u, ENCODER_VOLTS)) {
            fail("el encoder no cuenta con el motor empujando");
            return;
        }
        salida(u / cfg_.supply_volts);
        u_volts_ = u;
    }
}

// Única salida al motor: con el final apretado (LIMIT_TICKS lecturas seguidas) nunca empuja hacia él, esté en el
// estado que esté (subida previa, home, RST). Alejarse sí se puede
void Axis::salida(float duty) {
    const float hacia = cfg_.home_volts > 0.0f ? 1.0f : -1.0f;
    if (guarda_ticks_ >= LIMIT_TICKS && duty * hacia > 0.0f) duty = 0.0f;
    if (fin_ticks_ >= LIMIT_TICKS && duty * hacia < 0.0f) duty = 0.0f;   // el otro extremo
    duty_ = duty;
    driver_.set(duty);
}

void Axis::set_target_rad(float rad) {
    if (rad < cfg_.angle_min_rad) rad = cfg_.angle_min_rad;
    if (rad > cfg_.angle_max_rad) rad = cfg_.angle_max_rad;
    target_rad_ = rad;
}

// ---------------------------------------------------------------------
// Home: Search (ir al límite) -> el cero queda ahí -> GoTo (ir al ángulo objetivo)
// ---------------------------------------------------------------------

void Axis::lift_step() {
    const float sentido = lift_volts_ > 0.0f ? 1.0f : -1.0f;
    lift_moved_ = (position_rad_ - lift_start_) * sentido;
    bool fin = lift_moved_ >= lift_dist_ || ++ticks_ * (CONTROL_PERIOD_US / 1000) > lift_ms_
               || lift_moved_ < -2.0f;   // el encoder dice que va al revés: parar
    if (ticks_ % 50 == 0 && ticks_ > 50) {   // cada 0.5 s (desde el primer segundo, que arranca lento) debe avanzar algo; si no, topó
        if (std::fabs(position_rad_ - lift_prev_) < 0.2f) fin = true;
        lift_prev_ = position_rad_;
    }
    if (fin) {
        salida(0.0f);
        state_ = AxisState::Disabled;
    }
}

// Un encoder que no cuenta deja al eje empujando sin fin (al RST y al home les parece que no llega nunca)
bool Axis::encoder_muerto(float volts, float umbral) {
    const int32_t c = encoder_.count();
    // Avance real = ENCODER_MIN_CUENTAS o más. Un canal suelto hace que la cuenta solo tiemble ±1 y eso no es avance
    const int32_t avance = c > last_count_ ? c - last_count_ : last_count_ - c;
    if (avance >= ENCODER_MIN_CUENTAS || std::fabs(volts) < umbral) {
        last_count_ = c;
        quiet_ticks_ = 0;
        return false;
    }
    return ++quiet_ticks_ >= ENCODER_TICKS;
}

bool Axis::sin_avance() {
    if (++avance_ticks_ < AVANCE_TICKS) return false;
    const bool quieto = std::fabs(position_rad_ - avance_ref_) < AVANCE_RAD;
    reset_avance();
    return quieto;
}

void Axis::fail(const char* reason) {
    salida(0.0f);
    driver_.enable(false);
    fault_reason_ = reason;
    state_ = AxisState::Fault;
}

void Axis::begin_homing() {
    driver_.enable(true);
    ticks_ = 0;
    fault_reason_ = "";
    state_ = AxisState::Homing;
    limit_ticks_ = 0;
    search_start_ = position_rad_;
    if (en_limite()) {
        first_press_raw_ = encoder_.raw();
        phase_ = HomingPhase::Search;
        sobre_limite();                                  // ya está sobre el límite: cero ahí, o alejarse y volver lento
    } else {
        phase_ = HomingPhase::Search;
        pause_ticks_ = 0;
        quiet_ticks_ = 0;
        salida(cfg_.home_volts / cfg_.supply_volts);   // el signo de home_volts lleva hacia el límite
    }
}

void Axis::zero_here() {
    salida(0.0f);
    encoder_.reset_at(first_press_raw_);   // el cero donde se pisó el límite, no 30 ms después con el eje andando
    motor_rad_ = cfg_.home_pos * cfg_.gear_ratio;
    position_rad_ = cfg_.home_pos;
    pause_ticks_ = PAUSE_TICKS;
    phase_ = HomingPhase::GoTo;
}

// Límite presionado: con home lento, el primer toque solo avisa dónde está; el cero sale del segundo toque, lento
void Axis::sobre_limite() {
    if (phase_ == HomingPhase::Search && cfg_.home_slow_volts > 0.0f) {
        salida(0.0f);
        limit_ticks_ = 0;
        soltado_ = false;
        pause_ticks_ = PAUSE_TICKS;
        phase_ = HomingPhase::BackOff;
        return;
    }
    zero_here();
}

void Axis::homing_step() {
    // Protección: el home entero tiene un tiempo máximo
    if (++ticks_ * (CONTROL_PERIOD_US / 1000) > cfg_.home_timeout_ms) {
        fail("el home tardo demasiado");
        return;
    }

    const float hacia = cfg_.home_volts > 0.0f ? 1.0f : -1.0f;   // sentido hacia el límite
    const float lento = cfg_.home_slow_volts / cfg_.supply_volts;

    if (phase_ == HomingPhase::Search || phase_ == HomingPhase::Touch) {
        if (pause_ticks_ > 0) {                          // Touch: pausa y luego vuelve lento al límite
            if (--pause_ticks_ == 0) { salida(hacia * lento); reset_avance(); }
            return;
        }
        // Solo cuenta si el límite sigue presionado LIMIT_TICKS ciclos seguidos; un pico de ruido no detiene la búsqueda
        limit_ticks_ = en_limite() ? limit_ticks_ + 1 : 0;
        if (limit_ticks_ == 1) first_press_raw_ = encoder_.raw();
        if (limit_ticks_ >= LIMIT_TICKS) { sobre_limite(); return; }
        if (phase_ == HomingPhase::Touch) {
            if (encoder_muerto(cfg_.home_slow_volts, 0.0f)) fail("el encoder no cuenta volviendo lento al limite");
            else if (sin_avance()) fail("no vuelve al limite (voltaje lento muy bajo)");
            return;
        }
        // Seguridad (PCB nueva): a los 0.3 s debe ir HACIA el límite; a los 2 s, haberse movido algo
        const float movido = (position_rad_ - search_start_) * hacia;
        if (ticks_ == 30 && movido < -0.03f) fail("signo del encoder invertido (cambiar encoder_sign)");
        else if (ticks_ == 200 && std::fabs(movido) < 0.02f) fail("no se mueve (EN apagado, sin 12 V o motor desconectado)");
        else if (encoder_muerto(cfg_.home_volts, ENCODER_VOLTS)) fail("el encoder no cuenta (o el motor no se mueve) buscando el limite");
        return;
    }

    if (phase_ == HomingPhase::BackOff) {
        if (pause_ticks_ > 0) {
            if (--pause_ticks_ == 0) { salida(-hacia * lento); reset_avance(); }
            return;
        }
        if (encoder_muerto(cfg_.home_slow_volts, 0.0f)) { fail("el encoder no cuenta alejandose del limite"); return; }
        if (sin_avance()) { fail("no se aleja del limite (voltaje lento muy bajo)"); return; }
        if (!soltado_) {
            limit_ticks_ = en_limite() ? 0 : limit_ticks_ + 1;   // LIMIT_TICKS lecturas seguidas suelto
            if (limit_ticks_ >= LIMIT_TICKS) { soltado_ = true; soltado_en_ = position_rad_; }
        } else if ((soltado_en_ - position_rad_) * hacia >= BACKOFF_RAD) {
            salida(0.0f);
            limit_ticks_ = 0;
            pause_ticks_ = PAUSE_TICKS;
            phase_ = HomingPhase::Touch;
        }
        return;
    }

    // GoTo: primero la pausa, luego se mueve hacia el objetivo (frenando al final si hay home lento) y se detiene al alcanzarlo
    if (pause_ticks_ > 0) {
        if (--pause_ticks_ == 0) {
            dir_ = cfg_.home_pos + cfg_.home_target_rad > position_rad_ ? 1.0f : -1.0f;
            reset_avance();
        }
        return;
    }
    home_u_ = std::fabs(cfg_.home_volts);
    if (cfg_.home_slow_volts > 0.0f) {
        const float falta = std::fabs(cfg_.home_pos + cfg_.home_target_rad - position_rad_);
        const float k = falta < FRENO_RAD ? falta / FRENO_RAD : 1.0f;
        home_u_ = cfg_.home_slow_volts + (home_u_ - cfg_.home_slow_volts) * k;
        if (sin_avance()) { fail("no avanza hacia el objetivo (voltaje lento muy bajo)"); return; }
    }
    if (encoder_muerto(home_u_, cfg_.home_slow_volts > 0.0f ? 0.0f : ENCODER_VOLTS)) {
        fail("el encoder no cuenta (se movio sin lecturas)");
        return;
    }
    salida(dir_ * home_u_ / cfg_.supply_volts);
    if ((cfg_.home_pos + cfg_.home_target_rad - position_rad_) * dir_ <= 0.0f) {   // ya llegó o se pasó
        salida(0.0f);
        encoder_.reset();                // este punto pasa a valer home_pos (0 en R1 y R2): el cero definitivo
        motor_rad_ = cfg_.home_pos * cfg_.gear_ratio;
        position_rad_ = cfg_.home_pos;
        target_rad_ = cfg_.home_pos;     // se queda aquí hasta que llegue una orden
        ref_rad_ = cfg_.home_pos;
        rst_.reset(motor_rad_);          // el controlador arranca en reposo
        state_ = AxisState::Ready;       // desde el próximo ciclo manda el RST
    }
}
