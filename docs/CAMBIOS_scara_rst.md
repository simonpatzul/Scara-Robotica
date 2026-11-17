# Cambios en `pico/scara_rst/scara_rst.ino`

Solo cambió este archivo. Los pines, el STM32 y el resto quedaron igual.

| # | Cambio | Por qué |
|---|---|---|
| 1 | P va de 0 a **50 mm** (antes 85) | 50 mm es el máximo mecánico |
| 2 | El final de carrera cuenta con **3 lecturas seguidas** (30 ms) | 20 ms de ruido (bomba, motores) bastaban para que el home se detuviera antes y el cero quedara corrido |
| 3 | El cero se toma en la **primera** de esas lecturas | Así el filtro del punto 2 no corre el cero con el eje en movimiento |
| 4 | El objetivo **nunca pasa del final de home**: se queda 0.02 rad antes | R1 llegaba a 1.57 con su final en 1.48 y R2 a 1.885 con su final en 1.83: el motor empujaba contra el final |
| 5 | **Tope**: con el final presionado no se empuja hacia él y el RST se reinicia | Antes ponía u = 0 pero el RST seguía acumulando empuje; al salir daba un salto |

## Probado
Solo en el PC: se compiló la parte de control del `.ino` con un eje simulado. No se probó en el robot.

| Prueba | Antes | Ahora |
|---|---|---|
| 20 ms de ruido en el final al buscar | Cero corrido 0.45 rad | Sigue buscando |
| Error del cero | — | 0.010 rad (R1), 0.003 rad (R2) |
| Pedir más allá del final | Llega a 1.57 | Se queda en 1.46 (R1) y 1.81 (R2) |
| Final 0.1 rad antes de lo esperado | — | Se detiene en el final, 0 V, y puede salir |
| Pedir P = 85 | 85 | 50 |

## Para revisar
- Los valores de `homeObjetivo` y del rango de R1 y R2 parecen los del hombro y el codo de la versión anterior, pero intercambiados. Confirmar qué articulación mueve el motor en P1.
- En el esquemático el servo y la Pico usan el mismo regulador de 5 V: si la Pico se reinicia al mover el servo, puede ser por eso.
