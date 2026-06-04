# Ejemplos guiados

Esta sección recorre los grafos de ejemplo del repo
(`examples/graphs/walkthrough_E*.scn`). Cada uno **reproduce un caso de
una referencia citada** —verificada contra el libro, no inventada— y
sirve como tutorial de cómo armar ese tipo de sistema en SciNodes.

Para cada ejemplo se describe **cómo construir el grafo** paso a paso y se
cierra con un **pantallazo del grafo terminado**. No hace falta armarlos a
mano para probarlos: todos se cargan listos desde **Ayuda → Ejemplos**.

> Convención: "añadir un nodo" es **Shift + A** sobre el canvas y elegir el
> tipo; los parámetros se editan inline en el cuerpo del nodo (ver
> [Uso básico](usage.md)); cablear es arrastrar de un puerto de salida a uno
> de entrada (ver [Cablear nodos](wiring.md)).

---

## E1 — Lazo PID de Ogata (Ejemplo 8-1)

**Qué demuestra:** un lazo cerrado PID sobre la planta canónica
`G(s) = 1/[s(s+1)(s+5)]`, reproduciendo la respuesta al escalón de Ogata,
Ec. (8-2). Es el ejemplo de control clásico de referencia.

**Cómo armarlo:**

1. Añadí un **Step Signal** (Amplitude = 1, Step Time = 0) — el *setpoint*.
2. Añadí un **Summation** y poné `Sign1 = +1`, `Sign2 = −1`: calcula el error
   `ref − realimentación`.
3. Añadí un **PID Controller** con `Kp = 39.42`, `Ki = 12.8112`,
   `Kd = 30.3219`, `N = 100` (los valores de Ogata Ec. 8-2).
4. Añadí un **Integrator** — es el factor `1/s` de la planta.
5. Añadí un **Transfer Function (2nd)** con `num = [1, 0]`, `den = [5, 6]`,
   o sea `1/(s²+6s+5) = 1/[(s+1)(s+5)]`. En serie con el integrador del paso 4
   da la planta completa `1/[s(s+1)(s+5)]`.
6. Añadí un **Oscilloscope** (Time Window = 14) y un **Gain** (`K = 1`) para la
   realimentación unitaria.
7. **Cableá** el lazo:
   `Step → Summation(in 0)`; `Summation → PID → Integrator → Transfer Function (2nd)`;
   `Transfer Function (2nd) → Oscilloscope`; `Transfer Function (2nd) → Gain`;
   `Gain → Summation(in 1)` (la realimentación que cierra el lazo).
8. Pulsá **Run**. La respuesta al escalón reproduce la Figura 8-10 del libro
   (≈ 28 % de sobre-impulso).

**Referencia:** K. Ogata, *Modern Control Engineering* 5e, Ejemplo 8-1,
Ec. (8-2) — verificado contra el PDF (Kp/Ki/Kd y planta coinciden exacto).

> 📷 _Pantallazo del grafo terminado: pendiente (`ex_E1.png`)._

---

*(Los demás ejemplos —E1-DC, E2, E3/E3b, E4, E5, E6, E7, E9— siguen este
mismo formato; se irán completando.)*
