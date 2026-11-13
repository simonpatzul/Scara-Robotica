# Problema: los motores no se mueven porque la protección del STM32 se dispara con una corriente falsa en P

## Reglas de trabajo
- Responde en **español**.
- **Pide permiso antes de crear o editar archivos.**
- Explicaciones cortas y por bloques (el usuario las presenta a su profesor).

## Contexto
- Robot SCARA RRP: ejes R1, R2 (rotacionales) y P (prismático).
- **Raspberry Pi Pico** (Arduino IDE): control RST de posición, en `scara_v2\pico\scara_rst\scara_rst.ino`.
- **STM32F030C8** (STM32CubeIDE, proyecto `scara_v2\scara_stm32`): mide la corriente de los 3 drivers (IFX007T) con el ADC, prende y apaga los EN de los drivers (PB12, PB13, PB14) y publica por MQTT (`scara/estado`, `scara/evento`) → `server.js` → PostgreSQL → Grafana.
- Ruta del proyecto: `C:\Users\oscar\OneDrive\Documentos\Simon\UNIVERSIDAD\IoT\scara_v2`

## Síntoma
1. En la Pico (`scara_rst`), el comando `h1` (home de R1) termina en:
   `R1: FALLA en el home: no se mueve (EN apagado o motor desconectado)`.
   El programa aplica −3.5 V durante 2 s y el encoder no se mueve.
2. En Grafana ("Corrientes de los motores"), con **todos los motores quietos**:
   - **P marca entre 15 y 22 A todo el tiempo**, con ruido y sube y baja solo.
   - R1 y R2 marcan 0 A.

## Diagnóstico
- 18 A con el motor quieto **no es una corriente real**.
- La protección de `Core/Src/scara/scara.c` corta si la corriente pasa de **4.5 A durante 20 ms** (o de 3 A durante 500 ms). Con la lectura falsa de P se dispara apenas arranca y **apaga los EN de los 3 motores** (queda enclavado hasta reiniciar con SW1). Por eso ninguno se mueve.
- R1 y R2 dan 0 A porque, con los EN apagados, el IS queda por debajo del cero que se calibró al arrancar, y la lectura se recorta a 0.
- Conversión: `A = cuentas × 3.3 / 4095 / 470 × 19500 ≈ 0.0334 A por cuenta`. Entonces 18 A ≈ 540 cuentas ≈ **0.43 V** en el pin del ADC.
- Hipótesis principal: **PA4 o PA5 (ADC_IN4 / ADC_IN5, el IS del driver de P) está flotando**: falta la señal o hay un pin del header de 40 pines entre tarjetas sin contacto. Una entrada flotante da justo ese tipo de lectura ruidosa que se mueve sola.
- Otra posibilidad: el cero (`offset`) se calibra en `scara_init()` al arrancar. Si los 12 V de los drivers llegaron **después** de que prendió el STM32, el cero queda mal.

## Cómo está la protección hoy (`scara.c`)
```c
// Canales: M1 (R1) = IN0 + IN1, M2 (R2) = IN2 + IN3, M3 (P) = IN4 + IN5
#define AMP_POR_CUENTA   (3.3f / 4095.0f / 470.0f * 19500.0f)
#define LIMITE_SOSTENIDO_A   3.0f   // mas de 3 A ...
#define TIEMPO_SOSTENIDO_MS  500    // ... durante 500 ms  -> corte
#define LIMITE_PICO_A        4.5f   // mas de 4.5 A ...
#define TIEMPO_PICO_MS       20     // ... durante 20 ms   -> corte inmediato
```
- `scara_proteccion()` corre cada 1 ms: `corriente[k] = (sin_offset(2k) + sin_offset(2k+1)) * AMP_POR_CUENTA`.
- Si un eje falla, se apagan **los 3** EN.

## Mediciones pendientes (multímetro, punta negra en GND, motores quietos)
| Punto | Esperado | Si da otra cosa |
|---|---|---|
| PA4 y PA5 del STM32 (ADC_IN4/IN5) | ~0 V | ~0.4 V o un valor que salta → el IS de P no llega (revisar el header de 40 pines y los pines ADC_IN4/IN5) |
| PA0–PA3 (R1, R2) | ~0 V | como referencia |
| M1_EN / M2_EN / M3_EN (header J5) | 3.3 V al arrancar; 0 V después del corte | si dan 0 V desde el arranque, la protección se disparó |
| Fuente de 12 V | conectada **antes** de prender el STM32 | si no, el cero queda mal |

## Solución propuesta (no aplicada todavía)
En `scara.c`:
1. **Protección activable por eje**: `const uint8_t PROTEGER[3] = {1, 1, 0};`. R1 y R2 siguen protegidos y P se mide y se publica, pero **no corta**, hasta arreglar su sensor.
2. **Filtro promedio** de las lecturas (por ejemplo, promedio móvil de 16 muestras de 1 ms), para que el ruido no dispare picos falsos.
3. **Umbrales en un solo bloque, por eje** (`LIMITE_SOSTENIDO_A[3]`, `LIMITE_PICO_A[3]`), para ajustarlos fácil.
4. (Opcional) **Comprobar el cero al arrancar**: si el offset de un canal da muy alto (> ~100 cuentas) o el canal tiene mucho ruido, marcarlo como "sensor no válido" y publicarlo en lugar de dispararse.

**No hay que subir los umbrales a más de 25 A**: así la protección dejaría de servir para todos los ejes.

Después de cambiarlo: recompilar en CubeIDE y volver a grabar el STM32 (con la Pico usada como programador / OpenOCD). Luego repetir `h1` en la Pico.

## Verificación
- En Grafana, con los motores quietos, R1 y R2 deben estar cerca de 0 A. P también, una vez arreglado el sensor.
- En la Pico, `h` debe hacer el home de R1, R2 y P sin la falla "no se mueve".
- En `scara/evento` no deben llegar fallas mientras el robot está quieto.
