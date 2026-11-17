// Control RST de posicion de los 3 ejes del SCARA (Arduino IDE, core "Raspberry Pi Pico/RP2040")
// Basado en bajo_nivel/pico del repo scara-pcb-firmware: mismos coeficientes, mismo home.
//
// Nucleo 1: lazo de control a Ts = 10 ms (lee encoders, RST, PWM).
// Nucleo 0: comandos por el Monitor Serie (115200, con "Nueva linea").
//
// Comandos:
//   h          home de los 3 ejes, en orden R1, R2, P
//   h1 h2 h3   home de un solo eje
//   r1,r2,p    mover a la posicion (rad, rad, mm). Ej: 0.5,-0.3,20
//   r1,r2,p,servo,bomba   igual + servo (grados) y bomba (0 / 1)
//   s          parar: cada eje se queda donde esta
//   x          apagar los 3 motores (PWM 0)
//   v          mostrar / ocultar el estado cada 500 ms
//   u          reiniciar en modo carga (sin BOOTSEL)
//
// Los EN de los drivers los prende el STM32. Si el STM32 corta un motor, ese eje deja de moverse.

#include <Servo.h>

// ---------- Controlador RST (Landau, incremental, con anti-windup) ----------
// S'(q^-1) du(k) = T(q^-1) y*(k) - R(q^-1) y(k),   u(k) = u(k-1) + du(k)
// y* = referencia suavizada por el modelo  Am y*(k) = Bm r(k-1).  y en rad del MOTOR, u en voltios.
struct RstParams {
  double R[5];
  double Sp[4];   // S' (el primer coeficiente es 1)
  double T[8];
  double Am[3];   // modelo de referencia (el primero es 1)
  double Bm;
};

// Coeficientes del repo (control/rst_motor_v3.m). Pololu #4753, Ts = 10 ms.
// Modelo Kv/(s(tau s+1)), Kv = 1.8 rad/s/V, tau = 0.04 s. Polos: doble en wn = 6 rad/s, auxiliares en 0.7.
const RstParams RST_MOTOR = {
  { 2.059447617952, -1.561290298166, -2.047948810692, 1.572789105426, 0.0 },
  { 1.0, -1.208998870792, 0.394471201417, 0.0 },
  { 251.156203565986, -1000.488037304793, 1585.381209607035, -1249.331488730012,
    489.710270900887, -76.405160424582, 0.0, 0.0 },
  { 1.0, -1.902459, 0.904837 },
  0.002378
};

static void empujar(double *h, int n, double v) {   // corre el historial y pone v al frente
  for (int i = n - 1; i > 0; i--) h[i] = h[i - 1];
  h[0] = v;
}

class Rst {
public:
  const RstParams *p;
  double uMax;
  double y[5], ys[8], du[3], rPrev, u;

  void reset(double y0) {   // arranca en reposo en y0 (sin salto en u)
    for (int i = 0; i < 5; i++) y[i] = y0;
    for (int i = 0; i < 8; i++) ys[i] = y0;
    for (int i = 0; i < 3; i++) du[i] = 0;
    rPrev = y0;
    u = 0;
  }

  double paso(double ref, double medida) {
    // 1. Historial de y, y referencia suavizada y*(k)
    empujar(y, 5, medida);
    empujar(ys, 8, -p->Am[1] * ys[0] - p->Am[2] * ys[1] + p->Bm * rPrev);
    rPrev = ref;
    // 2. Incremento de control
    double d = 0;
    for (int i = 0; i < 8; i++) d += p->T[i] * ys[i];
    for (int i = 0; i < 5; i++) d -= p->R[i] * y[i];
    for (int i = 0; i < 3; i++) d -= p->Sp[i + 1] * du[i];
    // 3. Integrar y saturar. Se guarda el incremento real (ya saturado): eso es el anti-windup
    double un = u + d;
    if (un > uMax) un = uMax;
    if (un < -uMax) un = -uMax;
    empujar(du, 3, un - u);
    u = un;
    return u;
  }
};


// ---------- Configuracion de cada eje (todo lo que cambia de un motor a otro) ----------
struct Config {
  const char *nombre;
  int ina, inb;          // PWM: positivo = INB, negativo = INA
  int encA, encB;
  int limHome, limFin;   // finales de carrera, activos en bajo (-1 = no hay)
  float cuentasVuelta;   // cuentas por vuelta del eje del MOTOR
  float relacion;        // rad del motor por unidad del eje: 3 en R1/R2 (3:1), pi en P (rad/mm)
  float signoEnc;        // +1 o -1: voltaje POSITIVO debe hacer que el angulo AUMENTE
  float maxDuty;         // tope del PWM (0..1); tambien satura al RST
  float homeVolts;       // voltaje de busqueda del home; su signo va hacia el final de home
  float homeObjetivo;    // desde el final de home hasta el cero (rad o mm del eje)
  unsigned long homeTimeoutMs;
  float minimo, maximo;  // rango permitido (rad o mm)
};

const float VFUENTE = 12.0f;   // voltaje de los motores
const int LECTURAS_FINAL = 3;  // un final cuenta con 3 lecturas seguidas (30 ms): un pico de ruido no lo dispara
const float MARGEN_FINAL = 0.02f;   // el objetivo se queda esta distancia antes del final de home (rad o mm)
const unsigned long TS_US = 10000;   // Ts = 10 ms, el mismo del diseno

// Nombres: R1 = CODO (motor en P1), R2 = HOMBRO (motor en P2), P = prismatico (P3).
// Finales (uno por eje, solo el de home, probado en el robot el 09-11): hombro (P1) -> GP19, codo (P2) -> GP17, P -> GP18.
//                  nombre ina inb encA encB home fin cuentas relacion     signo maxDuty homeV objetivo timeout  min      max
const Config CFG[3] = {
  { "R1",  0,  1,  10,  11,  19,  -1,  3200.0f, 3.0f,        1.0f, 0.50f,  3.5f, -1.48f, 15000, -1.57f,  1.57f },    // CODO (P1); final del lado positivo
  { "R2",  2,  3,  12,  13,  17,  -1,  3200.0f, 3.0f,       -1.0f, 0.50f,  3.5f, -1.83f, 15000, -1.885f, 1.885f },  // HOMBRO (P2); final del lado positivo
  { "P",   7,  6,  14,  15,  18,  -1,  3200.0f, 3.14159265f,  1.0f, 1.00f, -8.0f, 0.0f,   30000,  0.0f,  50.0f },   // 50 mm: el maximo mecanico
};


// ---------- Encoders (interrupciones) ----------
volatile long cuentas[3] = { 0, 0, 0 };
volatile uint8_t estadoEnc[3] = { 0, 0, 0 };
const int8_t TABLA[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

void leerEncoder(int k) {
  uint8_t ab = (digitalRead(CFG[k].encA) << 1) | digitalRead(CFG[k].encB);
  estadoEnc[k] = ((estadoEnc[k] << 2) | ab) & 0x0F;
  cuentas[k] += TABLA[estadoEnc[k]];
}
void isr0() { leerEncoder(0); }
void isr1() { leerEncoder(1); }
void isr2() { leerEncoder(2); }


// ---------- Estado de cada eje ----------
enum Estado : uint8_t { APAGADO, HOME_BUSCA, HOME_VA, LISTO, FALLA };
enum Falla : uint8_t { SIN_FALLA, F_TIMEOUT, F_FIN, F_SIGNO, F_NO_SE_MUEVE, F_FUERA_RANGO };
const char *TEXTO_FALLA[] = { "-", "home tardo demasiado", "toco el final del otro extremo",
                              "signo del encoder invertido (cambiar signoEnc)",
                              "no se mueve (EN apagado o motor desconectado)", "se salio del rango" };

struct Eje {
  Rst rst;
  // los escribe el nucleo 1
  volatile Estado estado = APAGADO;
  volatile Falla falla = SIN_FALLA;
  volatile float posicion = 0;   // rad o mm del eje
  volatile float voltios = 0;
  // los escribe el nucleo 0
  volatile float objetivo = 0;
  volatile bool pideHome = false;
  volatile bool pideApagar = false;
  // solo nucleo 1
  float motorRad = 0, inicioBusqueda = 0, dir = 1;
  long offset = 0;
  int lecturasFinal = 0;   // lecturas seguidas con el final de home presionado
  long cuentasFinal = 0;   // donde estaba en la PRIMERA de esas lecturas: ahi va el cero
  unsigned long ticks = 0, pausa = 0;
};
Eje eje[3];

bool presionado(int pin) { return pin >= 0 && digitalRead(pin) == LOW; }

// Manda un voltaje al motor (recortado a maxDuty) con PWM 20 kHz, rango 0..1000
void mover(int k, float v) {
  float lim = CFG[k].maxDuty * VFUENTE;
  if (v > lim) v = lim;
  if (v < -lim) v = -lim;
  int nivel = (int)(fabsf(v) / VFUENTE * 1000.0f);
  if (v >= 0) { analogWrite(CFG[k].ina, 0); analogWrite(CFG[k].inb, nivel); }
  else        { analogWrite(CFG[k].inb, 0); analogWrite(CFG[k].ina, nivel); }
  eje[k].voltios = v;
}

void fallar(int k, uint8_t f) {   // f es un valor de Falla (uint8_t por los prototipos de Arduino)
  mover(k, 0);
  eje[k].falla = (Falla)f;
  eje[k].estado = FALLA;
}

void ponerCero(int k) {   // el punto actual pasa a ser el cero
  noInterrupts(); eje[k].offset = cuentas[k]; interrupts();
  eje[k].motorRad = 0;
  eje[k].posicion = 0;
}


// ---------- Un ciclo de control de un eje (nucleo 1, cada 10 ms) ----------
void actualizar(int k) {
  const Config &c = CFG[k];
  Eje &e = eje[k];

  // 1. Medir
  long n = cuentas[k] - e.offset;
  e.motorRad = c.signoEnc * n * 6.28318531f / c.cuentasVuelta;
  e.posicion = e.motorRad / c.relacion;

  // 2. Ordenes del nucleo 0
  if (e.pideApagar) { e.pideApagar = false; mover(k, 0); e.estado = APAGADO; e.falla = SIN_FALLA; return; }
  if (e.pideHome) {
    e.pideHome = false;
    e.falla = SIN_FALLA;
    e.ticks = 0;
    e.inicioBusqueda = e.motorRad;
    e.estado = HOME_BUSCA;
    if (!presionado(c.limHome)) mover(k, c.homeVolts);
  }

  // 3. Home, fase 1: ir hacia el final de home
  if (e.estado == HOME_BUSCA || e.estado == HOME_VA) {
    if (++e.ticks * (TS_US / 1000) > c.homeTimeoutMs) { fallar(k, F_TIMEOUT); return; }
  }
  e.lecturasFinal = presionado(c.limHome) ? e.lecturasFinal + 1 : 0;
  if (e.estado == HOME_BUSCA) {
    if (e.lecturasFinal == 1) { noInterrupts(); e.cuentasFinal = cuentas[k]; interrupts(); }
    if (e.lecturasFinal >= LECTURAS_FINAL) {   // llego: aqui es el cero provisional
      mover(k, 0);
      ponerCero(k);
      e.offset = e.cuentasFinal;   // el cero donde se piso el final, no 30 ms despues con el eje andando
      e.pausa = 20;                // 200 ms quieto antes de devolverse
      e.estado = HOME_VA;
      return;
    }
    // Seguridad: a los 0.3 s debe moverse HACIA el final de home; a los 2 s, moverse algo
    float movido = e.motorRad - e.inicioBusqueda;
    if (e.ticks == 30 && movido * c.homeVolts < -0.3f) { fallar(k, F_SIGNO); return; }
    if (e.ticks == 200 && fabsf(movido) < 0.05f) { fallar(k, F_NO_SE_MUEVE); return; }
    return;
  }

  // 4. Home, fase 2: ir del final hasta el cero definitivo
  if (e.estado == HOME_VA) {
    if (presionado(c.limFin)) { fallar(k, F_FIN); return; }
    if (e.pausa > 0) {
      if (--e.pausa == 0) {
        e.dir = c.homeObjetivo > e.posicion ? 1.0f : -1.0f;
        if (c.homeObjetivo == 0.0f) e.pausa = 0, e.dir = 0;   // P: el cero es el mismo final
        mover(k, e.dir * fabsf(c.homeVolts));
      }
      if (e.pausa > 0) return;
    }
    if (e.dir == 0 || (c.homeObjetivo - e.posicion) * e.dir <= 0) {   // llego
      mover(k, 0);
      ponerCero(k);
      e.objetivo = 0;
      e.rst.reset(0);
      e.estado = LISTO;            // desde el proximo ciclo manda el RST
    }
    return;
  }

  // 5. Control RST
  if (e.estado == LISTO) {
    // si se sale bastante del rango, algo anda mal (signo, cable): apagar
    if (e.posicion < c.minimo - 0.3f * fabsf(c.maximo - c.minimo) ||
        e.posicion > c.maximo + 0.3f * fabsf(c.maximo - c.minimo)) { fallar(k, F_FUERA_RANGO); return; }

    // Tope: con el final de home presionado no se empuja hacia el (homeVolts tiene el signo hacia home).
    // Solo si el objetivo apunta al final: hacia afuera el RST trabaja normal, si no el eje quedaria atrapado
    bool tope = e.lecturasFinal >= LECTURAS_FINAL && (e.objetivo - e.posicion) * c.homeVolts >= 0;
    float u = e.rst.paso((tope ? e.posicion : e.objetivo) * c.relacion, e.motorRad);   // el RST trabaja en rad del motor
    if (tope && u * c.homeVolts > 0) {
      e.rst.reset(e.motorRad);   // sin esto el RST seguiria acumulando empuje contra el final
      u = 0;
    }
    if (presionado(c.limFin) && u * c.homeVolts < 0) u = 0;
    mover(k, u);
  }
}


// ---------- Nucleo 1: lazo a periodo fijo ----------
void setup1() {
  delay(500);   // espera a que setup() del nucleo 0 configure los pines
}

void loop1() {
  static unsigned long proximo = micros();
  proximo += TS_US;
  while ((long)(micros() - proximo) < 0) { }
  for (int k = 0; k < 3; k++) actualizar(k);
}


// ---------- Nucleo 0: comandos y estado ----------
Servo servo;
const int SERVO_PIN = 8, BOMBA_PIN = 9;
bool verEstado = true;
int homeEnCurso = -1;      // eje que esta haciendo home en la secuencia "h" (-1 = ninguno)
bool homeTodos = false;
char linea[64];
int nLinea = 0;

const char *textoEstado(int k) {
  switch (eje[k].estado) {
    case APAGADO: return "apagado";
    case HOME_BUSCA: return "home-busca";
    case HOME_VA: return "home-va";
    case LISTO: return "listo";
    default: return "FALLA";
  }
}

void setup() {
  Serial.begin(115200);

  analogWriteFreq(20000);
  analogWriteRange(1000);
  for (int k = 0; k < 3; k++) {
    eje[k].rst.p = &RST_MOTOR;
    eje[k].rst.uMax = CFG[k].maxDuty * VFUENTE;   // el RST satura igual que el PWM
    eje[k].rst.reset(0);
    analogWrite(CFG[k].ina, 0);
    analogWrite(CFG[k].inb, 0);
    pinMode(CFG[k].encA, INPUT);
    pinMode(CFG[k].encB, INPUT);
    pinMode(CFG[k].limHome, INPUT_PULLUP);
    if (CFG[k].limFin >= 0) pinMode(CFG[k].limFin, INPUT_PULLUP);
  }
  attachInterrupt(digitalPinToInterrupt(CFG[0].encA), isr0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CFG[0].encB), isr0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CFG[1].encA), isr1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CFG[1].encB), isr1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CFG[2].encA), isr2, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CFG[2].encB), isr2, CHANGE);

  servo.attach(SERVO_PIN, 1000, 2000);
  servo.write(90);
  pinMode(BOMBA_PIN, OUTPUT);
  digitalWrite(BOMBA_PIN, LOW);   // con el buck: solo prendido / apagado, sin PWM
}

void iniciarHome(int k) {
  eje[k].pideHome = true;
  Serial.printf("Home de %s...\n", CFG[k].nombre);
}

void procesarLinea(char *s) {
  // Comandos de una letra
  if (strcmp(s, "h") == 0) { homeTodos = true; homeEnCurso = 0; iniciarHome(0); return; }
  if (s[0] == 'h' && s[1] >= '1' && s[1] <= '3' && s[2] == 0) {
    homeTodos = false; homeEnCurso = s[1] - '1'; iniciarHome(homeEnCurso); return;
  }
  if (strcmp(s, "s") == 0) {
    for (int k = 0; k < 3; k++) eje[k].objetivo = eje[k].posicion;
    Serial.println("Parado"); return;
  }
  if (strcmp(s, "x") == 0) {
    homeEnCurso = -1;
    for (int k = 0; k < 3; k++) eje[k].pideApagar = true;
    Serial.println("Motores apagados"); return;
  }
  if (strcmp(s, "v") == 0) { verEstado = !verEstado; return; }
  if (strcmp(s, "u") == 0) {
    for (int k = 0; k < 3; k++) eje[k].pideApagar = true;
    digitalWrite(BOMBA_PIN, LOW);
    Serial.println("Reiniciando en modo carga...");
    delay(100);
    rp2040.rebootToBootloader();
  }

  // Posiciones: r1,r2,p  o  r1,r2,p,servo,bomba
  float t[5];
  int n = 0;
  char *p = s;
  while (n < 5) {
    char *fin;
    t[n] = strtof(p, &fin);
    if (fin == p) break;
    n++;
    if (*fin != ',') { p = fin; break; }
    p = fin + 1;
  }
  if ((n != 3 && n != 5) || *p != 0) { Serial.println("ERR: comando no valido"); return; }

  for (int k = 0; k < 3; k++) {
    float v = t[k];
    if (v < CFG[k].minimo) v = CFG[k].minimo;   // recortar al rango permitido
    if (v > CFG[k].maximo) v = CFG[k].maximo;
    // El final de home esta a -homeObjetivo del cero, del lado de homeVolts: el objetivo no se manda encima de el
    if (CFG[k].homeObjetivo != 0.0f) {
      float fin = fabsf(CFG[k].homeObjetivo) - MARGEN_FINAL;
      if (CFG[k].homeVolts > 0 && v > fin) v = fin;
      if (CFG[k].homeVolts < 0 && v < -fin) v = -fin;
    }
    if (eje[k].estado == LISTO) eje[k].objetivo = v;
  }
  if (n == 5) {
    servo.write(constrain((int)t[3], 0, 180));
    digitalWrite(BOMBA_PIN, t[4] > 0.5f ? HIGH : LOW);
  }
  Serial.printf("OK -> R1 %.3f rad  R2 %.3f rad  P %.1f mm\n", eje[0].objetivo, eje[1].objetivo, eje[2].objetivo);
}

void loop() {
  // 1. Leer comandos (una linea hasta Enter)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (nLinea > 0) { linea[nLinea] = 0; nLinea = 0; procesarLinea(linea); }
    } else if (nLinea < 63) linea[nLinea++] = c;
  }

  // 2. Secuencia de home: avisar cuando termina cada eje y seguir con el siguiente
  if (homeEnCurso >= 0) {
    Eje &e = eje[homeEnCurso];
    if (!e.pideHome && (e.estado == LISTO || e.estado == FALLA)) {
      if (e.estado == FALLA) {
        Serial.printf("%s: FALLA en el home: %s\n", CFG[homeEnCurso].nombre, TEXTO_FALLA[e.falla]);
        homeEnCurso = -1;
      } else {
        Serial.printf("%s: HOME OK\n", CFG[homeEnCurso].nombre);
        if (homeTodos && homeEnCurso < 2) iniciarHome(++homeEnCurso);
        else { homeEnCurso = -1; Serial.println("LISTO: manda posiciones r1,r2,p"); }
      }
    }
  }

  // 3. Avisar fallas nuevas fuera del home
  static Falla avisada[3] = { SIN_FALLA, SIN_FALLA, SIN_FALLA };
  for (int k = 0; k < 3; k++) {
    if (eje[k].falla != avisada[k]) {
      if (eje[k].falla != SIN_FALLA && k != homeEnCurso)
        Serial.printf("%s: FALLA: %s\n", CFG[k].nombre, TEXTO_FALLA[eje[k].falla]);
      avisada[k] = eje[k].falla;
    }
  }

  // 4. Estado cada 500 ms
  static unsigned long proxImp = 0;
  if (verEstado && millis() >= proxImp) {
    proxImp = millis() + 500;
    for (int k = 0; k < 3; k++) {
      Serial.printf("%s %-10s pos %8.3f obj %8.3f u %5.2f V %c%c | ", CFG[k].nombre, textoEstado(k),
                    eje[k].posicion, eje[k].objetivo, eje[k].voltios,
                    presionado(CFG[k].limHome) ? 'H' : '-', presionado(CFG[k].limFin) ? 'F' : '-');
    }
    Serial.println();
  }
}
