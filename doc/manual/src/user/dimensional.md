# Análisis dimensional: unidades + R7

A partir de v0.0.9 el editor no trata todos los puertos como
"números genéricos". Cada puerto declara una **unidad**
(volts, kg·m², rad/s, ...) y cada cable que tendes se valida
contra la regla **R7**: las unidades de los dos extremos
tienen que ser dimensionalmente compatibles. Si la salida es
en grados y la entrada espera radianes, el editor rechaza la
conexión y te dice por qué.

## Cómo escribir unidades

El parser acepta la notación natural que esperarías:

| Ejemplo del usuario | Cómo lo entiende el editor |
|---------------------|----------------------------|
| `2k Ω`              | 2 000 Ω                    |
| `100 cm`            | 1 m (re-normalizado)       |
| `60 Hz`             | 60 s⁻¹                     |
| `9.81 m/s^2`        | aceleración SI             |
| `kg·m^2`            | momento de inercia         |
| `V·s/rad`           | constante de back-EMF      |

El widget de parámetros es **prefix-aware**: si escribes `2k`
en un campo de Ohms, el editor lo lee como `2 kΩ`. Si
escribes sólo un número (sin sufijo), el campo se resetea a
su unidad canónica SI (un `Ω` en este caso).

## Conversores: `Deg → Rad` y `Rad → Deg`

La gramática R7 te impide cablear un puerto que entrega
grados a otro que espera radianes. El único camino legal es
insertar un nodo conversor. `Deg → Rad` multiplica por π/180;
`Rad → Deg` por 180/π. La declaración explícita evita
errores silenciosos por convención (¿la salida está en
grados? ¿en radianes? — el cable lo dice).

## Fields ideales vs físicos

Algunos parámetros son **escalares puros** sin dimensión
física: ganancias, factores de saturación, exponentes de
filtro. Los fields ideales aceptan sólo números sin unidad y
no participan en R7. Los fields físicos llevan unidad y
participan en la propagación dimensional.

## Propagación por el grafo

Las unidades se propagan **forward y backward**. Si conectas
un `Integrator` (que multiplica por `s`, equivalente
dimensional a `· time`) a una señal en `V`, el editor sabe
que la salida es `V·s`. Si el siguiente nodo declara que
espera `Wb` (weber, `V·s`), la conexión pasa. Si declara
`m/s`, la conexión se rechaza con un mensaje específico.

## El nodo culpable

Cuando R7 rechaza una conexión, la barra de estado dice qué
unidades estaba esperando cada lado del cable. El badge rojo
en el nodo destino te muestra cuál es el responsable de la
declaración fallida; si el nodo origen es el equivocado, lo
sustituyes por un conversor o ajustas la unidad del field.

## Cuándo NO aplicar R7

Cuando R7 te resulta una molestia (por ejemplo, durante un
experimento exploratorio) podés desactivarlo desde
**Preferencias → Análisis dimensional**. El editor mantendrá
las inferencias y mostrará advertencias en la barra de
estado, pero no rechazará cables.
