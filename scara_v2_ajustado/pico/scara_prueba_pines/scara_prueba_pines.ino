// Prueba de pines de la Pico en la PCB del SCARA (Arduino IDE, core "Raspberry Pi Pico/RP2040")
// Sirve para verificar la placa ANTES del control: motores, encoders, finales, servo y bomba.
//
// Monitor serie a 115200. Teclas:
//   1 / 2 / 3   elegir eje (R1, R2, P). Al cambiar, el motor se para
//   + / -       subir / bajar el PWM de a 10 % (el signo es el sentido)
//   0           parar el motor
//   s / d       servo -10° / +10°
//   b           bomba prendida / apagada
//   z           poner en cero las cuentas del encoder
//   u           reiniciar en modo carga (como apretar BOOTSEL, sin tocar la placa)
// Cada 200 ms imprime todo.
//
// Grabar sin el boton BOOTSEL: desde la segunda vez, el IDE reinicia la Pico solo por USB
// (aunque este alimentada a 5 V). Si alguna vez no lo logra, mandar 'u' por el monitor serie.
//
// Los EN de los drivers los maneja el STM32 (no la Pico): deben estar en 1 para que el motor se mueva.
// Que revisar: con PWM POSITIVO las cuentas del eje deben SUBIR. Si bajan, anotar el signo para el control.

#include <Servo.h>

// ---------- Pines (MCU.SchDoc) ----------
// Motores: dos PWM por motor (INA / INB del IFX007T)
const int M_INA[3] = { 0, 2, 7 };     // R1, R2, P  (P cruzado: positivo = subir)
const int M_INB[3] = { 1, 3, 6 };
// Encoders (ya pasados a 3.3 V por el SN74LVC8T245)
const int ENC_A[3] = { 10, 12, 14 };
const int ENC_B[3] = { 11, 13, 15 };
// Finales de carrera, activos en bajo: home y fin de cada eje
const int LIM_HOME[3] = { 17, 19, 21 };
const int LIM_FIN[3]  = { 18, 20, 22 };
// Actuadores
const int SERVO_PIN = 8;
const int BOMBA_PIN = 9;
// I2C con el STM32: GP4 = SCL, GP5 = SDA (invertidos respecto al I2C del RP2040) -> no se usa aqui

const char *NOMBRE[3] = { "R1", "R2", "P" };
const int HOME_SIGNO = -1;            // los tres ejes van hacia home con PWM negativo
const int PWM_MAX = 60;               // % maximo en la prueba


// ---------- Encoders (interrupciones) ----------
volatile long cuentas[3] = { 0, 0, 0 };
volatile uint8_t estado[3] = { 0, 0, 0 };
// Tabla de cuadratura: estado anterior (2 bits) + nuevo (2 bits) -> -1, 0 o +1
const int8_t TABLA[16] = { 0, -1, 1, 0,  1, 0, 0, -1,  -1, 0, 0, 1,  0, 1, -1, 0 };

void leerEncoder(int k) {
  uint8_t ab = (digitalRead(ENC_A[k]) << 1) | digitalRead(ENC_B[k]);
  estado[k] = ((estado[k] << 2) | ab) & 0x0F;
  cuentas[k] += TABLA[estado[k]];
}
void isrEnc0() { leerEncoder(0); }
void isrEnc1() { leerEncoder(1); }
void isrEnc2() { leerEncoder(2); }


// ---------- Motores ----------
// pwm de -100 a 100 (%). Positivo = INB, negativo = INA. Nunca los dos a la vez.
void motor(int k, int pwm) {
  int nivel = abs(pwm) * 10;          // rango 0..1000
  if (pwm >= 0) {
    analogWrite(M_INA[k], 0);
    analogWrite(M_INB[k], nivel);
  } else {
    analogWrite(M_INB[k], 0);
    analogWrite(M_INA[k], nivel);
  }
}

bool presionado(int pin) { return digitalRead(pin) == LOW; }


// ---------- Programa ----------
Servo servo;
int eje = 0;
int pwm = 0;
int servoDeg = 90;
bool bomba = false;
unsigned long proxImpresion = 0;

void setup() {
  Serial.begin(115200);

  // PWM de motores: 20 kHz, rango 0..1000
  analogWriteFreq(20000);
  analogWriteRange(1000);
  for (int k = 0; k < 3; k++) motor(k, 0);

  // Encoders
  for (int k = 0; k < 3; k++) {
    pinMode(ENC_A[k], INPUT);
    pinMode(ENC_B[k], INPUT);
  }
  attachInterrupt(digitalPinToInterrupt(ENC_A[0]), isrEnc0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B[0]), isrEnc0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_A[1]), isrEnc1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B[1]), isrEnc1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_A[2]), isrEnc2, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B[2]), isrEnc2, CHANGE);

  // Finales de carrera (la placa ya tiene pull-up de 10k)
  for (int k = 0; k < 3; k++) {
    pinMode(LIM_HOME[k], INPUT_PULLUP);
    pinMode(LIM_FIN[k], INPUT_PULLUP);
  }

  // Servo al centro (pulso 1000..2000 us) y bomba apagada
  servo.attach(SERVO_PIN, 1000, 2000);
  servo.write(servoDeg);
  pinMode(BOMBA_PIN, OUTPUT);
  digitalWrite(BOMBA_PIN, LOW);
}

void loop() {
  // Teclas
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '3') { motor(eje, 0); pwm = 0; eje = c - '1'; }
    if (c == '+' && pwm < PWM_MAX)  pwm += 10;
    if (c == '-' && pwm > -PWM_MAX) pwm -= 10;
    if (c == '0') pwm = 0;
    if (c == 's' && servoDeg >= 10)  { servoDeg -= 10; servo.write(servoDeg); }
    if (c == 'd' && servoDeg <= 170) { servoDeg += 10; servo.write(servoDeg); }
    if (c == 'b') { bomba = !bomba; digitalWrite(BOMBA_PIN, bomba ? HIGH : LOW); }
    if (c == 'z') { noInterrupts(); cuentas[0] = cuentas[1] = cuentas[2] = 0; interrupts(); }
    if (c == 'u') {                               // modo carga sin boton
      for (int k = 0; k < 3; k++) motor(k, 0);    // motores parados antes de reiniciar
      digitalWrite(BOMBA_PIN, LOW);
      Serial.println("Reiniciando en modo carga (aparece la unidad RPI-RP2)...");
      delay(100);
      rp2040.rebootToBootloader();
    }
  }

  // Seguridad: no empujar contra un final de carrera presionado
  bool home = presionado(LIM_HOME[eje]);
  bool fin  = presionado(LIM_FIN[eje]);
  if (home && pwm * HOME_SIGNO > 0) pwm = 0;
  if (fin  && pwm * HOME_SIGNO < 0) pwm = 0;
  motor(eje, pwm);

  // Imprimir cada 200 ms
  if (millis() >= proxImpresion) {
    proxImpresion = millis() + 200;
    Serial.printf("Eje %s  PWM %4d %%  |  cuentas R1 %7ld  R2 %7ld  P %7ld  |  finales ",
                  NOMBRE[eje], pwm, cuentas[0], cuentas[1], cuentas[2]);
    for (int k = 0; k < 3; k++) {
      Serial.printf("%s:%c%c ", NOMBRE[k], presionado(LIM_HOME[k]) ? 'H' : '-', presionado(LIM_FIN[k]) ? 'F' : '-');
    }
    Serial.printf(" |  servo %3d°  bomba %s\n", servoDeg, bomba ? "ON" : "off");
  }
}
