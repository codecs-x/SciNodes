#!/usr/bin/env python3
"""
Capa 13 — Audit de cobertura de los controles de simulación.

Cierra otro punto ciego: ninguna capa verificaba que `simulation_controls.json`
coincida con el enum `SimAction` que el código realmente define.

Fuente de verdad: enum class SimAction en src/core/SimTypes.hpp.
  `None` es el estado por defecto (ningún botón presionado); el resto son
  acciones que la barra de estado expone como botones.

Verifica contra `doc/db/simulation_controls.json`:
  - cada valor accionable del enum (todos menos None) tiene una fila.
  - ninguna fila referencia un SimAction:: que el enum ya no define.
  - el conteo que menciona _doc ("SimAction tiene N valores") == len(enum).

Exit 0 si todo coincide; 1 si hay drift.
"""
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

def color(s, code): return f"\033[{code}m{s}\033[0m"
def ok(s):  return color("✓", "32") + " " + s
def err(s): return color("✗", "31") + " " + s

def enum_values():
    """Valores del enum class SimAction, en orden."""
    src = (REPO / 'src/core/SimTypes.hpp').read_text()
    m = re.search(r'enum\s+class\s+SimAction\s*\{(.*?)\}', src, re.DOTALL)
    if not m:
        print(err("no encontré enum class SimAction en SimTypes.hpp"))
        sys.exit(1)
    body = m.group(1)
    # quitar comentarios // ... antes de extraer identificadores
    body = re.sub(r'//.*', '', body)
    return [tok.strip() for tok in body.split(',') if tok.strip()]

def db_facts():
    db = json.load(open(REPO / 'doc/db/simulation_controls.json'))
    actions = []
    for row in db['rows']:
        m = re.search(r'SimAction::(\w+)', row.get('action', ''))
        if m:
            actions.append(m.group(1))
    # conteo mencionado en _doc ("SimAction tiene N valores")
    dm = re.search(r'SimAction tiene (\d+) valores', db.get('_doc', ''))
    doc_count = int(dm.group(1)) if dm else None
    return actions, doc_count

def main():
    print(color("━━━ Auditoría Capa 13: controles de simulación ↔ SimAction ━━━", "1;36"))
    print()
    enum = enum_values()
    actionable = [v for v in enum if v != 'None']
    db_actions, doc_count = db_facts()

    print(f"  SimAction (SimTypes.hpp): {', '.join(enum)}")
    print(f"  Acciones en la BD       : {', '.join(db_actions)}")
    print()

    bad = False

    # 1) cada valor accionable tiene fila; ninguna fila inventa un valor
    enum_not_db = [v for v in actionable if v not in db_actions]
    db_not_enum = [v for v in db_actions if v not in enum]
    if enum_not_db:
        print(err(f"En el enum y SIN fila en la BD: {', '.join(enum_not_db)}"))
        bad = True
    if db_not_enum:
        print(err(f"En la BD y NO en el enum (¿stale?): {', '.join(db_not_enum)}"))
        bad = True
    if not enum_not_db and not db_not_enum:
        print(ok("toda acción del enum tiene fila y ninguna fila sobra"))

    # 2) conteo de _doc
    if doc_count is None:
        print(err("_doc no menciona 'SimAction tiene N valores' (no se pudo chequear el conteo)"))
        bad = True
    elif doc_count != len(enum):
        print(err(f"_doc dice {doc_count} valores; el enum tiene {len(enum)}"))
        bad = True
    else:
        print(ok(f"_doc concuerda con el enum ({len(enum)} valores, None incluido)"))

    print()
    if bad:
        print(color("INCONSISTENT — actualizar simulation_controls.json contra SimAction.", "1;31"))
        sys.exit(1)
    print(color("CONSISTENT — los controles de simulación están sincronizados.", "1;32"))

if __name__ == '__main__':
    main()
