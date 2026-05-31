# Backends del solver

A partir de v0.0.7 el solver es una **interface**, no un
módulo concreto. El editor habla con `IComputeBackend`; el
backend efectivo se elige en runtime y, en el caso del
backend in-process, en build time vía un flag de CMake.

## La interface: `IComputeBackend`

```cpp
class IComputeBackend {
public:
    virtual void reset(const SimSpec& spec) = 0;
    virtual bool step(double dt) = 0;
    virtual void sendParameter(const std::string& path,
                               int paramIdx, double value) = 0;
    virtual void stop() = 0;
    virtual Status status() const = 0;
    virtual double time() const = 0;
    virtual int channelCount(int nodeId) const = 0;
    virtual const double* buffer(int nodeId, int channel) const = 0;
    // ...
};
```

`SimSpec` reúne lo que un backend necesita para arrancar: el
código generado por `ScilabCodeGen`, los identificadores de
los sumideros con su número de canales, los parámetros
iniciales. El resto del editor sólo conoce este contrato.

## `ISimSession`: la cara que ve el editor

Por encima de `IComputeBackend` hay una interface más alta,
`ISimSession`, que es lo que los paneles (`PlotPanel`,
`View3DPanel`, `StatusBar`) ven cuando consultan la simulación
corriente. Expone `status()`, `time()`, `channelCount(nodeId)`,
`buffer(nodeId, ch)` y el handle al backend activo. El
`ScilabBridge` implementa `ISimSession`; los paneles no
conocen ni a `ScilabSubprocessBackend` ni a
`ScilabCallApiBackend`, solo hablan con la sesión.

Esta separación deja agregar un backend nuevo (otro lenguaje
de host, otro intérprete) implementando `IComputeBackend` sin
tocar ninguna implementación de `ISimSession`.

## `ScilabSubprocessBackend` (default)

La implementación que ya existía en *tags* anteriores, ahora
formalizada bajo la interfaz. Lanza `scilab-cli` con las
banderas `-nb -nwni -noatomsautoload`, escribe el *driver* al
*stdin* y lee las muestras línea-a-línea del *stdout*. Es el
backend por defecto: no requiere ningún paquete extra más
allá de Scilab instalado.

## `ScilabCallApiBackend` (opt-in)

Embebe Scilab en el mismo proceso del editor vía
`StartScilab` / `SendScilabJob`. La diferencia técnica con el
subprocess: no hay pipe, así que el *round-trip* de un
`sendParameter` o un `step` evita el syscall de
escritura/lectura. La diferencia práctica: hay un costo fijo
de inicialización (carga de la JVM del intérprete) y la
ganancia neta depende del tamaño del paso —en pasos cortos
de simulación-real-time el subprocess gana porque su pipe
está *line-buffered* y el costo dominante es el dispatcher
del intérprete, no el syscall.

Para compilar el editor con este backend incluido:

```bash
cmake -B build -DSCINODES_WITH_CALLAPI=ON \
    -DSCILAB_PREFIX=/opt/scilab-2026.0.1
```

Si la cabecera `call_scilab.h` no se encuentra en
`$SCILAB_PREFIX/include/scilab/`, CMake reporta el problema y
la compilación falla pronto, sin tocar el resto del binario.

## Selección en runtime

`ComputeBackendSelector::pick(...)` decide cuál backend
construir cuando el usuario presiona Run. Si el binario se
compiló con `SCINODES_WITH_CALLAPI=ON`, hay un menú
desplegable en el panel de simulación; si no, el subprocess
es el único disponible y la UI ni siquiera muestra el menú.

## El precedente de la anti-corruption layer

`IComputeBackend` es el ejemplo canónico del patrón
*anti-corruption layer* en SciNodes: el editor habla su
propio idioma —`reset(spec)`, `step(dt)`, `buffers`— y el
backend traduce a llamadas del intérprete (escribir al pipe,
mandar `SendScilabJob`, etc.). Cuando llegue un tercer backend
(p. ej. Julia, GNU Octave o un solver propio en C++), se
implementa la interface y el resto del editor no se entera.
