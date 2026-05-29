# Correr una simulación

Construir el grafo es la mitad del trabajo. La otra mitad es
mirarlo en movimiento: ver cómo evoluciona la señal en un
osciloscopio, ajustar una ganancia mientras el lazo está corriendo,
comparar la respuesta antes y después de un cambio. SciNodes hace
esa segunda mitad delegando la integración numérica a un
subproceso de Scilab.

## Antes de pulsar Run

El editor llama a `scilab-cli` como subproceso al arrancar la
simulación, así que ese binario tiene que estar disponible. SciNodes
busca primero en `/opt/scilab-2026.0.1/bin/`,
`/opt/scilab/bin/`, `/usr/bin/` y `/usr/local/bin/`, y si no lo
encuentra ahí cae al `PATH`. Si nada de eso funciona, el botón
**Run** queda habilitado pero la primera ejecución reporta un
error en la barra de estado y te explica que no encontró
`scilab-cli`.

El otro requisito es que el grafo respete la gramática. Si hay un
mensaje de regla rota visible en la barra (`R0`, `R1`, …), el
botón Run aparece deshabilitado.

## Los cinco botones

La barra de estado expone los controles de simulación como botones
con código de color:

- **▶ Run** (verde) arranca el subproceso de Scilab con el grafo
  actual. Sólo visible cuando el editor está en estado
  *Idle*.
- **⏸ Pause** (amarillo) pausa el avance del solver. El subproceso
  sigue vivo y el último estado se preserva.
- **■ Stop** (rojo) manda `quit` al subproceso y vuelve al estado
  Idle. Visible durante una simulación corriendo o pausada.
- **▶ Resume** (verde) reanuda una simulación pausada.
- **↺ Reset** (gris) detiene la simulación y limpia los buffers
  de los sumideros. Está siempre visible.

El botón visible cambia según el estado: en Idle ves Run y Reset;
en Simulating ves Pause, Stop y Reset; en Paused ves Resume, Stop
y Reset.

## Lo que pasa cuando pulsas Run

El editor toma el grafo, lo traduce a un *script* Scilab que
define el vector de estado y la función `dxdt(t, x)`, lanza
`scilab-cli` con las banderas `-nb -nwni -noatomsautoload` (sin
banner, modo terminal sin gráficos, sin *toolboxes*) y le pasa el
*script*. Acto seguido el editor empieza a pedirle pasos
línea-a-línea: cada *frame* del *loop* del editor, una llamada a
`ode("rk", ...)` que avanza la simulación `dt = 0.01 s`. La
salida de cada paso baja por *stdout* del subproceso, llega al
editor, y se reparte a los buffers de los sumideros del grafo. El
ritmo nominal es 60 Hz, con Scilab avanzando \\(\\sim 10\\) ms de
tiempo simulado por *frame*.

No hay un hilo dedicado del solver: el editor pide un paso, espera
la línea de respuesta, dibuja, y vuelve a pedir. Para grafos
pequeños el costo de un paso es del orden de los milisegundos
—suficiente para mantener la tasa nominal.

## Ajustar parámetros sin reiniciar

Mientras la simulación está corriendo puedes editar los
parámetros de cualquier nodo arrastrando los *sliders* o con un
doble click para escribir el valor exacto. El editor llama a
`sendParameter` en el subproceso —una escritura al *stdin* del
hijo que reasigna la variable de parámetro del nodo. El siguiente
paso del solver recoge el cambio. Es *fire-and-forget*: el editor
no espera respuesta porque el efecto se verá en el siguiente plot.

## Cuando algo diverge

Los lazos sin amortiguación, las ganancias demasiado altas o las
condiciones iniciales fuera de rango pueden hacer que el estado
diverja. Si en algún paso una variable se vuelve `NaN` o `Inf`, el
puente pasa a estado *Error* con el mensaje *"Solver produced
NaN/Inf"*. La barra de estado lo muestra y la simulación se
detiene. Revisar el grafo y volver a pulsar Run.

## El visor de sumideros

Cada nodo de tipo *Sink* (Oscilloscope, FFT Analyzer, Phase
Portrait, Data Logger) recibe muestras en cada paso y las
acumula en un *ring buffer* de 512 puntos. El panel de plots
abajo los renderiza según el tipo: forma de onda contra tiempo
para Oscilloscope y FFT, trayectoria en el espacio de estados
para Phase Portrait, contador de muestras para Data Logger. El
sumidero `Terminal Display` no usa el panel: imprime el último
valor recibido dentro de su propio cuerpo en el canvas.
