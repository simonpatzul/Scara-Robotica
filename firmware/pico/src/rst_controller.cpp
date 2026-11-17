#include "rst_controller.h"

// Se usa double dentro del controlador: los coeficientes de R y T son de ~1000 y se
// restan entre sí (suman ~3.5). A 100 Hz el costo del double por software es despreciable.

void RstController::reset(float y0) {
    for (double& v : y_)  v = y0;
    for (double& v : ys_) v = y0;
    for (double& v : du_) v = 0.0;
    r_prev_ = y0;
    u_ = 0.0;
}

static void push(double* h, int n, double v) {   // desplaza el historial y pone v al frente
    for (int i = n - 1; i > 0; --i) h[i] = h[i - 1];
    h[0] = v;
}

// RST posicional: voltaje absoluto. du_ guarda aquí los u pasados, no los incrementos
float RstController::step_posicional(float ref) {
    double u = p_.T[0] * ref;
    for (int i = 0; i < 5; ++i) u -= p_.R[i] * y_[i];
    for (int i = 0; i < 3; ++i) u -= p_.Sp[i + 1] * du_[i];
    if (u >  u_max_) u =  u_max_;
    if (u < -u_max_) u = -u_max_;
    push(du_, 3, u);
    u_ = u;
    return static_cast<float>(u_);
}

float RstController::step(float ref, float y) {
    // 1. Historial de y y referencia suavizada y*(k)
    push(y_, 5, y);
    if (p_.posicional) return step_posicional(ref);
    push(ys_, 8, -p_.Am[1] * ys_[0] - p_.Am[2] * ys_[1] + p_.Bm * r_prev_);
    r_prev_ = ref;

    // 2. Incremento de control
    double du = 0.0;
    for (int i = 0; i < 8; ++i) du += p_.T[i] * ys_[i];
    for (int i = 0; i < 5; ++i) du -= p_.R[i] * y_[i];
    for (int i = 0; i < 3; ++i) du -= p_.Sp[i + 1] * du_[i];

    // 3. Integrar y saturar. Se guarda el incremento REAL (ya saturado): eso es el anti-windup
    double u = u_ + du;
    if (u >  u_max_) u =  u_max_;
    if (u < -u_max_) u = -u_max_;
    push(du_, 3, u - u_);
    u_ = u;
    return static_cast<float>(u_);
}
