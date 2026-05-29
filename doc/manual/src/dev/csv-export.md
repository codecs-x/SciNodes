# `CsvExport` y export `.sod`

## `CsvExport` (`src/core/`)

```cpp
namespace scinodes::csv {
bool writeSink(const std::string& path,
               const std::string& sinkName,
               const SinkBuffer& buffer,
               std::string* err = nullptr);
}
```

`SinkBuffer` empareja `t` + canales del sumidero.  Encabezado
`t,y` o `t,ch0,ch1,...`.  Formato `%.6f`.  Sin dependencias
de ImGui — agnóstico al *frontend*.

## Export `.sod`

`AppWindow::onExportSod()` recolecta buffers de todos los
sumideros y pasa la lista a `ScilabBridge::saveSod(path)`.
El proceso del solver serializa con `save(path, vars...)`
nativo de Scilab.  Delegar a Scilab evita la dependencia
con `libhdf5` desde C++.

Nombres en el archivo: sanitización de los títulos visibles
(`Oscilloscope #3 → Oscilloscope_3`).
