#!/usr/bin/env python3
"""
Capa 14 — Audit de cobertura de los nodos custom de ejemplo.

Cierra el último punto ciego de fácil verificación: `custom_nodes.json` (la
tabla descriptiva) debe reflejar los descriptores reales en
`doc/custom_nodes/*.json`, que son la fuente de verdad que el editor carga.

Verifica:
  - el conjunto de type_id coincide (cada archivo tiene fila y viceversa).
  - por type_id: label, description, expression, input_ports, output_ports
    coinciden; category coincide (case-insensitive: el archivo usa
    "transformer", la BD "Transformer"); los params coinciden por
    (name, default) (la BD usa la clave "unit" y el archivo "units" — esa
    diferencia de nombre de clave NO se audita, sólo name+default).
  - la fila apunta al archivo correcto vía su campo "file" y ese archivo existe.

Exit 0 si todo coincide; 1 si hay drift.
"""
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CUSTOM_DIR = REPO / 'doc/custom_nodes'

def color(s, code): return f"\033[{code}m{s}\033[0m"
def ok(s):  return color("✓", "32") + " " + s
def err(s): return color("✗", "31") + " " + s

def param_key(params):
    """Lista canónica (name, default) para comparar ignorando unit/units."""
    return sorted((p.get('name'), p.get('default')) for p in (params or []))

def files_by_id():
    out = {}
    for f in sorted(CUSTOM_DIR.glob('*.json')):
        j = json.load(open(f))
        out[j['type_id']] = (j, f.relative_to(REPO).as_posix())
    return out

def db_by_id():
    db = json.load(open(REPO / 'doc/db/custom_nodes.json'))
    return {row['type_id']: row for row in db['rows']}

# campos comparados exactos (str/int) entre archivo (fuente) y fila de BD
EXACT = ['label', 'description', 'expression', 'input_ports', 'output_ports']

def main():
    print(color("━━━ Auditoría Capa 14: custom_nodes.json ↔ doc/custom_nodes/*.json ━━━", "1;36"))
    print()
    files = files_by_id()
    db    = db_by_id()

    print(f"  type_id en archivos : {', '.join(sorted(files))}")
    print(f"  type_id en la BD    : {', '.join(sorted(db))}")
    print()

    bad = False

    # 1) conjuntos
    file_not_db = sorted(set(files) - set(db))
    db_not_file = sorted(set(db) - set(files))
    if file_not_db:
        print(err(f"Descriptor sin fila en la BD: {', '.join(file_not_db)}"))
        bad = True
    if db_not_file:
        print(err(f"Fila en la BD sin descriptor real: {', '.join(db_not_file)}"))
        bad = True
    if not file_not_db and not db_not_file:
        print(ok("el conjunto de type_id coincide"))

    # 2) campos por type_id (sólo los que están en ambos)
    for tid in sorted(set(files) & set(db)):
        fj, fpath = files[tid]
        row = db[tid]
        diffs = []
        for k in EXACT:
            if fj.get(k) != row.get(k):
                diffs.append(f"{k}: archivo={fj.get(k)!r} BD={row.get(k)!r}")
        if str(fj.get('category', '')).lower() != str(row.get('category', '')).lower():
            diffs.append(f"category: archivo={fj.get('category')!r} BD={row.get('category')!r}")
        if param_key(fj.get('params')) != param_key(row.get('params')):
            diffs.append(f"params: archivo={param_key(fj.get('params'))} BD={param_key(row.get('params'))}")
        if row.get('file') != fpath:
            diffs.append(f"file: BD={row.get('file')!r} esperado={fpath!r}")
        if diffs:
            print(err(f"{tid}: " + "; ".join(diffs)))
            bad = True
        else:
            print(ok(f"{tid}: coincide con {fpath}"))

    print()
    if bad:
        print(color("INCONSISTENT — actualizar custom_nodes.json contra doc/custom_nodes/*.json.", "1;31"))
        sys.exit(1)
    print(color("CONSISTENT — los nodos custom de ejemplo están sincronizados.", "1;32"))

if __name__ == '__main__':
    main()
