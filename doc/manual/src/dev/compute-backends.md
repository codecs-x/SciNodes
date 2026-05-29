# `IComputeBackend`

El motor numérico de SciNodes se expresa como un contrato
abstracto.  Hay dos implementaciones: el subproceso
histórico (default) y `call_scilab` *in-process* (opt-in).

## El contrato

```cpp
struct IComputeBackend {
    virtual bool prepare(const BackendPrepareSpec& spec) = 0;
    virtual bool step(double dt,
                      std::vector<SinkSample>& outSamples,
                      int* outOffendingNodeId) = 0;
    virtual bool setParameter(int nodeId, int paramIdx, double value) = 0;
    virtual bool exportHistory(const std::string& path,
                               std::string* result) = 0;
    virtual void shutdown() = 0;

    virtual Status      status()    const = 0;
    virtual std::string lastError() const = 0;
};
```

`BackendPrepareSpec` separa **autoría** (`NodeGraph` →
`ScilabCodeGen::generateSpec`) de **ejecución** (cuerpo
`dynamics(t, x)` + estado + sumideros + parámetros).

## Subproceso `scilab-cli` (default)

`ScilabBridge` con `fork` + `pipe` a `scilab-cli`.
Portable, estable, aísla *crashes*.  Costo típico
~16 ms/step para el motor DC.

## `ScilabCallApiBackend` (opt-in)

`src/core/backends/ScilabCallApiBackend.{cpp,hpp}` embebe
Scilab en el mismo proceso vía `call_scilab` C API.
Compilar con `-DSCINODES_WITH_CALLAPI=ON` (requiere
`libscilab-dev`).  Costo típico ~5 ms/step, picos de hasta
168 ms por GC de la JVM.

## Selección en *runtime*

```bash
./SciNodes                      # subproceso
SCINODES_BACKEND=callapi ./SciNodes  # in-process si el binario soporta
```

Si el binario no se compiló con `WITH_CALLAPI`, cae al
subproceso con *warning* en `stderr`.
