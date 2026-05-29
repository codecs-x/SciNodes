# Nodos personalizados desde JSON

El catálogo del editor no está cerrado al código fuente.
Cualquier ingeniero describe un tipo nuevo en un descriptor
JSON y lo dropea sin recompilar.

## Estructura mínima

```json
{
    "type_id":     "AbsValue",
    "label":       "Absolute Value",
    "description": "|u1| — passes the magnitude of its input.",
    "category":    "transformer",
    "input_ports":  1,
    "output_ports": 1,
    "params": [],
    "expression": "abs(u1)"
}
```

| Campo          | Significado                                  |
|----------------|----------------------------------------------|
| `type_id`      | Identificador único.                         |
| `label`        | Texto que aparece en el popup `Shift+A`.     |
| `description`  | Tooltip.                                     |
| `category`     | `transformer`, `source`, o `sink`.           |
| `input_ports`  | Cantidad de puertos de entrada.              |
| `output_ports` | Cantidad de puertos de salida.               |
| `params`       | Parámetros editables.                        |
| `expression`   | Línea Scilab con placeholders.               |

## Placeholders en la expresión

| Placeholder  | Se sustituye por…                                       |
|--------------|---------------------------------------------------------|
| `u1`, `u2`, …| Expresión del puerto de entrada correspondiente.       |
| `p_<name>`   | Valor actual del parámetro `<name>`.                    |

## Dónde van

Los descriptores viven en `doc/custom_nodes/` del repo.
El editor los carga al arrancar.  El repo trae dos
ejemplos: `abs_value.json` y `tripler.json`.

## Restricciones

- Sin estado interno (no son *stateful*).  Para
  integradores y derivadores, el catálogo nativo es la
  vía.
- La plantilla de expresión es una sola línea de Scilab.
