# Introducción

SciNodes es un editor visual de bloques para diseñar, simular y
observar sistemas dinámicos en tiempo real. La idea es la misma
que en cualquier editor nodal moderno: el usuario arrastra
bloques al canvas, los conecta con cables, ajusta parámetros, y
mira los resultados sin escribir una sola línea de código. Por
debajo, SciNodes traduce el grafo a un *script* Scilab y lo
ejecuta paso a paso vía el subproceso `scilab-cli`, dejando una
ventana de tiempo entre cada paso en la que el usuario puede
tocar parámetros y ver el efecto inmediatamente.

## Para qué sirve

Para diseñar sistemas de control y observar su respuesta sin
tener que abrir un IDE numérico, ni mantener mentalmente la
estructura del diagrama mientras se escribe la EDO. Un caso
típico: arrastras un `Step Signal`, un `Summation`, un
`PID Controller` y un `DC Motor Model`, los cableas en lazo
cerrado, conectas un `Oscilloscope` a la salida del motor,
pulsas Run, y mientras la simulación corre subes `Kp` con el
*slider* del PID viendo cómo cambia la respuesta del motor en
tiempo real.

## Qué hay en esta versión

El catálogo es cerrado: 23 tipos de nodo repartidos en tres
familias. Las fuentes —`Voltage Source`, `Current Source`,
`Step Signal`, `Sine Signal`, `Ramp Signal`— generan la señal
de entrada al sistema. Los transformadores —`Gain`,
`Summation`, `Integrator`, `Differentiator`, `Low-Pass Filter`,
`PID Controller`, `Transfer Function`, `Transfer Function (2nd)`,
`Saturation`, `DC Motor Model`, `Gear Transmission`,
`Inverse Kinematics`— operan sobre la señal. Y los sumideros
—`Oscilloscope`, `FFT Analyzer`, `Phase Portrait`,
`Data Logger`, `Terminal Display`, `3D View Sink`— la observan.

Una gramática ligera de seis reglas (R0–R5) valida cada cable en
el momento en que el usuario lo intenta tender. Si la conexión
no respeta las reglas de composición (categoría, puertos
disponibles, no auto-conexiones, no duplicados), el editor la
rechaza con un mensaje específico en la barra de estado.

El grafo se persiste en formato `.scn`, un JSON legible con el
campo de versión `scnodes_version: "0.3"`. La pila de undo/redo
guarda 50 *snapshots*. La simulación se acopla a un subproceso
de Scilab que el editor lanza con las banderas `-nb -nwni
-noatomsautoload`, y un hilo dedicado del solver dentro de
`ScilabBridge` le pide pasos a Scilab sin bloquear la UI; los
lazos cerrados con al menos un nodo con estado se manejan
automáticamente por *cycle breaking*: el nodo con estado provee
la variable de retroalimentación.

El visor 3-D auxiliar carga modelos `.obj` y `.stl` en
*wireframe* con cámara orbital, desacoplado del solucionador en
esta versión —es para inspeccionar geometría, no para
visualizar la simulación.

Los sumideros tienen tratamiento especializado: el
`FFT Analyzer` calcula el espectro de magnitud con una FFT
radix-2 Cooley-Tukey propia (`Fft.cpp`) sobre la ventana más
reciente del *ring buffer*; el `Phase Portrait` lee dos canales
(`x(t)`, `dx/dt(t)`) del mismo nodo y los dibuja como
trayectoria 2-D; el `Oscilloscope` y el `Data Logger` se quedan
con forma de onda contra tiempo.

Una suite de tests respalda el comportamiento: 192 aserciones de
gramática (R0–R5, alcanzabilidad, operaciones del `NodeGraph` y
ciclo undo/redo) y 234 aserciones de integración repartidas en
15 escenarios *end-to-end* que lanzan `scilab-cli` real.

## Cómo está organizado este manual

El manual está dividido en dos partes. El **manual de usuario**
explica cómo usar el editor: instalar, construir un grafo,
cablear, simular, leer plots, mirar geometría. El **manual de
desarrollador** explica cómo está construido por dentro: la
arquitectura en capas, el modelo del grafo, la gramática, el
generador de código, el puente con Scilab y el formato `.scn`.

Cada nueva versión del repositorio amplía el catálogo y refina
el flujo de trabajo. Para una vista de qué entró en cada
versión, consulta el
[CHANGELOG](https://github.com/codecs-x/SciNodes/blob/main/CHANGELOG.md).
