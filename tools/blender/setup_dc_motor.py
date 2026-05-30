# =============================================================================
# setup_dc_motor.py
#
# Pega este script en Blender:
#   1. Workspace "Scripting" (o ventana Text Editor).
#   2. New (en el Text Editor) → Paste.
#   3. Run Script (Alt + P, o icono ▶).
#
# Después de ejecutarlo, la escena queda con la jerarquía mínima que el
# contrato `DCMotor` de SciNodes requiere:
#
#   - shaft           Cilindro placeholder.  Role: part.
#   - housing         Cilindro placeholder.  Role: part.
#   - shaft_bearing   Empty con axis Z+.     Role: joint, type=revolute.
#   - terminal_plus   Empty.                 Role: anchor, kind=electrical.
#   - terminal_minus  Empty.                 Role: anchor, kind=electrical.
#
# Cada objeto lleva las Custom Properties que el exportador glTF de
# Blender pone bajo `extras` al exportar, en el formato plano:
#
#   extras["scinodes.role"]   = "part" | "joint" | "anchor"
#   extras["scinodes.name"]   = "shaft" | "housing" | ...
#
# Nota sobre el eje del joint:
#   NO escribimos `scinodes.axis` desde Python.  El motivo es que
#   Blender exporta a glTF Y-up por defecto (gira -90° en X), pero
#   las Custom Properties no se transforman.  Si pusiéramos
#   axis=(0,0,1) "para arriba" en Blender, post-export quedaría
#   apuntando a +Z en glTF cuando en realidad la geometría está
#   alineada a +Y → resultado: el eje rotaría como hélice.
#
#   En lugar de eso, el loader de SciNodes deriva el eje del joint
#   de la ROTACIÓN del empty (que sí se transforma con la geometría).
#   El empty SINGLE_ARROW por defecto apunta hacia +Z local; rotalo
#   en Blender para cambiar la dirección del eje físico.
#
# Modifica las geometrías a tu gusto y exporta:
#   File → Export → glTF 2.0 (.glb/.gltf)
#   ▸ Include → marca "Custom Properties"
#   ▸ Formato a elección (glTF Separate o Embedded)
#
# Después, en SciNodes, doble-click en el DC Motor Model →
# Cargar modelo 3D… → selecciona el .gltf exportado.
#
# Spec del contrato:    doc/geometry-contracts-design.md
# =============================================================================

import bpy
import math

# ---- 0. Borrar la escena por completo (incluyendo cámara y luz por
#         defecto que no nos sirven para esta plantilla). -----------------
def clean_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False, confirm=False)
    # también limpia datos huérfanos para que un re-run no acumule
    for block in (bpy.data.meshes, bpy.data.curves,
                  bpy.data.materials, bpy.data.cameras,
                  bpy.data.lights):
        for item in list(block):
            if item.users == 0:
                block.remove(item)

clean_scene()

# ---- Helper: setea custom properties en formato plano que glTF
#              exporta directo como extras planos.  Ver nota en el
#              encabezado sobre por qué NO escribimos `scinodes.axis`:
#              el eje del joint se deriva de la rotación del empty. -
def tag_scinodes(obj, role, name):
    obj["scinodes.role"] = role
    obj["scinodes.name"] = name

# ---- 1. HOUSING — cilindro estator placeholder ------------------------
# Diámetro 50 mm, alto 60 mm, centrado en el origen.
bpy.ops.mesh.primitive_cylinder_add(
    radius=0.025,
    depth=0.060,
    location=(0.0, 0.0, 0.0),
    rotation=(0.0, 0.0, 0.0),
)
housing = bpy.context.active_object
housing.name = "housing"
tag_scinodes(housing, role="part", name="housing")

# ---- 2. SHAFT — cilindro del eje, sale por la cara superior -----------
# Diámetro 5 mm, alto 40 mm, centrado en (0, 0, +0.020) — sobresale
# del housing 10 mm.
bpy.ops.mesh.primitive_cylinder_add(
    radius=0.0025,
    depth=0.040,
    location=(0.0, 0.0, 0.020),
)
shaft = bpy.context.active_object
shaft.name = "shaft"
tag_scinodes(shaft, role="part", name="shaft")

# ---- 3. JOINT — empty con eje Z+ apuntando hacia arriba ---------------
# Empty con "Single Arrow" muestra visualmente la dirección del eje
# en Blender, lo que ayuda a verificar que la rotación va a salir bien.
bpy.ops.object.empty_add(
    type='SINGLE_ARROW',
    location=(0.0, 0.0, 0.0),
)
bearing = bpy.context.active_object
bearing.name = "shaft_bearing"
bearing.empty_display_size = 0.030
# El empty SINGLE_ARROW apunta a +Z local por defecto; con rotation
# = (0,0,0) eso equivale a +Z en world Blender = +Y en glTF post-
# export, que es exactamente el eje del shaft cilíndrico también
# alineado a +Z en Blender.  Si necesitas cambiarlo, rota el empty
# en Blender — el loader lee node.rotation y deriva el eje.
tag_scinodes(bearing, role="joint", name="shaft_bearing")

# ---- 4. ANCHORS — terminales A+ / A- ----------------------------------
# Empties tipo esfera para que se vean como puntos.  Posiciones a 20 mm
# del eje en X, 50 mm por encima del centro del housing.
bpy.ops.object.empty_add(
    type='SPHERE',
    location=(0.020, 0.0, 0.050),
)
term_plus = bpy.context.active_object
term_plus.name = "terminal_plus"
term_plus.empty_display_size = 0.005
tag_scinodes(term_plus, role="anchor", name="terminal_plus")

bpy.ops.object.empty_add(
    type='SPHERE',
    location=(-0.020, 0.0, 0.050),
)
term_minus = bpy.context.active_object
term_minus.name = "terminal_minus"
term_minus.empty_display_size = 0.005
tag_scinodes(term_minus, role="anchor", name="terminal_minus")

# ---- 5. Ajustes finales del viewport ----------------------------------
# Encuadra todo y deja una luz simple para ver mejor en el viewport
# rendered (no afecta al export glTF, es estética).
bpy.ops.object.select_all(action='DESELECT')

# Una lampara point en la esquina para iluminar el viewport.
bpy.ops.object.light_add(type='POINT', location=(0.10, -0.10, 0.10))
bpy.context.active_object.data.energy = 50.0

# Frame view all (ajusta el zoom para que se vea toda la escena).
for area in bpy.context.screen.areas:
    if area.type == 'VIEW_3D':
        for region in area.regions:
            if region.type == 'WINDOW':
                ctx = {'area': area, 'region': region,
                       'screen': bpy.context.screen}
                try:
                    with bpy.context.temp_override(**ctx):
                        bpy.ops.view3d.view_all(use_all_regions=False)
                except Exception:
                    pass

# ---- 6. Mensaje al usuario ---------------------------------------------
print("=" * 70)
print("[SciNodes] Plantilla DC motor lista.")
print()
print("  Objetos creados:")
print("    - housing         (part)")
print("    - shaft           (part)")
print("    - shaft_bearing   (joint, axis Z+)")
print("    - terminal_plus   (anchor, electrical)")
print("    - terminal_minus  (anchor, electrical)")
print()
print("  Edita las geometrías a tu gusto y exporta:")
print("    File → Export → glTF 2.0")
print("    Marca: Include → Custom Properties")
print()
print("  Después, en SciNodes:")
print("    Doble-click en DC Motor Model → Cargar modelo 3D…")
print("=" * 70)
