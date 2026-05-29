# SciNodes

SciNodes es un editor visual de nodos en tiempo real para
Scilab.  El usuario construye diagramas de bloques en un
canvas tipo Blender; el editor traduce el grafo a código
Scilab y lo ejecuta a 60 Hz, dejando una ventana entre paso
y paso para ajustar parámetros sin reiniciar la simulación.

## Lo que SciNodes intenta resolver

Las herramientas tradicionales de simulación basadas en
bloques (Xcos, Simulink) tienen tres limitaciones que se
vuelven evidentes cuando un ingeniero necesita explorar
diseños mecatrónicos de forma fluida:

1. **Catálogo cerrado** — agregar un bloque nuevo requiere
   escribir módulos en el lenguaje del entorno, recompilar
   y recargar *toolboxes*.
2. **Validación tardía** — los errores de tipo o de cableado
   aparecen recién al ejecutar la simulación, no al cablear.
3. **Sin *live tuning*** — cada cambio de parámetro requiere
   detener, editar y reiniciar la corrida desde \\(t=0\\).

SciNodes plantea una alternativa apoyada en tres ideas: una
**gramática formal** que evalúa el grafo al cablear, un
*bridge* ligero con `scilab-cli` que delega la integración
numérica al solucionador maduro de Scilab, y un **hilo
dedicado** del solver que acepta ajustes de parámetro entre
paso y paso.
