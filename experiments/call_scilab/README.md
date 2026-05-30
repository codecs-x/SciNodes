# Smoke test para `call_scilab`

Esta carpeta contiene una prueba aislada para responder **una sola pregunta**\:

> ¿Puedo enlazar contra `libscilab`, inicializar Scilab desde C++, ejecutar
> una expresión simple y recuperar el resultado de vuelta?

Si la respuesta es sí en menos de **4 horas** de trabajo, el experimento sigue;
si no, se aborta y se vuelve al *tag* `pre-experiments`.

## Estructura

```
experiments/call_scilab/
├── CMakeLists.txt    # build aislado, no toca el CMakeLists raíz
├── smoke.cpp         # el programa de prueba (3 pasos: init, job, read-back)
└── README.md         # este archivo
```

El experimento es **completamente independiente** del binario principal de
SciNodes: si se descarta, basta borrar la carpeta.

## Requisitos del entorno

1. Una instalación de Scilab con los headers de desarrollo. En el sistema
   actual, eso es `/opt/scilab-2026.0.1/`. Verifica que existan:
   ```
   /opt/scilab-2026.0.1/include/scilab/call_scilab.h
   /opt/scilab-2026.0.1/include/scilab/api_scilab.h
   /opt/scilab-2026.0.1/lib/scilab/libscilab.so
   ```
2. CMake $\geq$ 3.16 y un compilador C++17.

## Compilar

```bash
cd experiments/call_scilab
mkdir -p build && cd build
cmake -DSCILAB_PREFIX=/opt/scilab-2026.0.1 ..
make
```

Si `find_path` o `find_library` falla, CMake aborta con un error específico
que indica qué pieza falta.

## Ejecutar

```bash
export SCI=/opt/scilab-2026.0.1/share/scilab
export LD_LIBRARY_PATH=/opt/scilab-2026.0.1/lib/scilab:\
/opt/scilab-2026.0.1/thirdparty/java/lib:\
/opt/scilab-2026.0.1/thirdparty/java/lib/server:$LD_LIBRARY_PATH
./smoke
```

Salida esperada (en `stdout`):

```
c = 5
```

Y en `stderr` un *trace* paso a paso con los `[OK]` correspondientes.
El programa retorna `0` si todo pasa, `1` si algo falló en los pasos
intermedios, `2` si llegó al final pero el valor leído no es `5`.

### Status: smoke test pasa en Scilab 2026.0.1

Confirmado el 2026-05-15 con la instalación `/opt/scilab-2026.0.1/`.
Los obstáculos encontrados quedaron documentados abajo.

## Hallazgos sobre `call_scilab` en Scilab 2026

Tres puntos no obvios que hubo que resolver para que el smoke compilara y corriera:

1. **El API moderno (`scilab_getVar`, `scilab_getDouble`) NO es para embedding.**
   Esas funciones exigen un `scilabEnv` que solo está disponible dentro de un
   *gateway* (toolbox llamada desde Scilab). Para `call_scilab` hay que usar
   el API legacy "stack" (`api_stack_double.h`)\,:
   ```c
   int getNamedScalarDouble(void* _pvCtx, const char* _pstName, double* _pdblReal);
   ```
   El primer argumento (`pvApiCtx`) puede ser `NULL` en contexto embebido — la
   función opera sobre el estado global de la sesión de Scilab.

2. **Hay que enlazar DOS bibliotecas, no una.**
   `libscilab.so` contiene el API de variables\;
   `libscicall_scilab.so` contiene `StartScilab`/`SendScilabJob`/`TerminateScilab`.
   El CMakeLists localiza las dos por separado y enlaza ambas.

3. **Scilab arrastra su propio JRE.**
   `libscilab.so` depende de `libjava.so`, `libjvm.so` y `libverify.so` que viven
   en `thirdparty/java/lib/` y `thirdparty/java/lib/server/`. El CMake configura
   `BUILD_RPATH` con la ruta principal\; para los Java se necesita `LD_LIBRARY_PATH`
   en tiempo de ejecución (alternativa: extender el rpath en CMake).

4. **El warning de Tcl es ruidoso pero no fatal.**
   Scilab intenta inicializar su consola gráfica y falla con
   `TCL Initialization failed`. Para nuestro caso (cómputo numérico embebido)
   esto no importa\: el resto de `StartScilab` sigue normalmente.

## Lo que se aprende con el resultado

| Resultado | Diagnóstico | Acción |
|---|---|---|
| `c = 5`, exit 0 | La API funciona en este entorno. | Seguir al paso 2: refactor de `ScilabBridge` a `call_scilab`. |
| Falla en `StartScilab` | `SCI` mal apuntado, falta `libscilab-java`, conflicto de versión. | Investigar dependencias de Scilab; quizá la API no es viable. |
| Falla en `SendScilabJob` | Inicialización incompleta, *stack* mal dimensionado. | Probar con `StackSize` explícito (3er argumento de `StartScilab`). |
| Falla la lectura | La API de `api_scilab` cambió de firma entre versiones. | Inspeccionar `api_scilab.h` en `${SCILAB_PREFIX}/include/scilab/` y ajustar. |
| Crash en runtime | Conflicto con bibliotecas del proceso (Java, GTK). | Indicador fuerte de que `call_scilab` no es viable para SciNodes. |

## Retorno al estado previo

Si el experimento se aborta:

```bash
git checkout pre-experiments
git checkout -b experiment/own-api   # plan B
# o
git checkout develop                 # plan C
```

La carpeta `experiments/call_scilab/` puede dejarse en la rama experimental
como evidencia del intento\; nunca se mergea a `develop` si la prueba no
pasó.
