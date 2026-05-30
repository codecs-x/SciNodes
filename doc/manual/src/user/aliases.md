# Aliases — evitar cables que cruzan el canvas

Un `Alias` es un nodo que **referencia** otro nodo sin necesitar
un cable visible que cruce el canvas.  Sirve para que un grafo
con realimentación o señales reusadas no quede ilegible por
cruces de cables a 200 píxeles.

## Cuándo usar Alias

- Tenés una señal `ω` que sale del motor y se consume en cuatro
  lugares distintos del canvas (lazo de realimentación, plot,
  motor 3D, logger).  Cuatro cables largos hacen el grafo
  ilegible.
- Tenés un parámetro común (ej. `Kp`) que necesitás referenciar
  desde varios nodos custom.
- Querés "saltar" una señal hacia otra zona del canvas sin
  tener que reordenar todo.

## Cómo crear un Alias

El gesto principal es **drag desde un pin de input al canvas
vacío**:

1. Posicioná el cursor sobre el círculo de **entrada** del nodo
   destino (lado izquierdo).
2. Mantené apretado el botón izquierdo y arrastrá hacia el
   canvas vacío.
3. Soltá.  Aparece el popup de añadir nodos.
4. Abrí la sección **"Nodos en el canvas"**.  Lista los outputs
   de cada nodo existente.
5. Buscá el nodo por **nombre** — escribí en la barra de
   búsqueda.  El match es case-insensitive y funciona contra:
   - El nombre custom del nodo (si fue editado con <kbd>F2</kbd>).
   - El label traducido del NodeType si no hay nombre custom.
6. Click en la entrada `→ NombreDelNodo [out 1]`.

SciNodes crea un nodo `Alias` cableado al input desde donde
arrancaste el drag.  El Alias guarda internamente
`target_node_id` + `target_port` — al simular, lee el valor del
puerto referenciado como si hubiera un cable directo.

## Cómo se ve un Alias

El nodo Alias se dibuja como un nodo pequeño con el nombre del
nodo referenciado (estilo "→ ω").  No tiene parámetros propios
ni computa nada — sólo redirige.

## Edge directo vs Alias — la regla

| Dirección del drag           | Si elegís nodo existente del popup | Resultado                       |
|------------------------------|------------------------------------|---------------------------------|
| Desde un pin **OUT**          | Lista de **inputs** existentes     | Edge directo (cable visible)    |
| Desde un pin **IN**           | Lista de **outputs** existentes    | **Alias** insertado en el canvas |

La asimetría tiene sentido geométrico: cuando arrastrás desde
un OUT, podés cablear a un input existente cercano (un cable
extra rara vez molesta).  Cuando arrastrás desde un IN, en
general el output que querés ya tiene varios consumidores
distantes — un cable más cruzando el canvas empeora el layout.
El Alias se inserta justo donde soltaste, sin cruces.

## Limitaciones

- No se puede aliasear un Alias (el popup no lista nodos
  Alias en la sección "Nodos en el canvas").
- El Alias hereda la unidad del puerto referenciado vía el
  análisis dimensional — si la fuente es `rad/s`, el Alias lo
  es también.  Cambios de unidad en la fuente se propagan.
- Si borrás el nodo referenciado, los Aliases que lo apuntan
  quedan "rotos" y el cable hacia ellos se pone rojo (R1
  fallido).  Borrá los Aliases huérfanos o reapuntalos.
