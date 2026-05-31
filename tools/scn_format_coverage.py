#!/usr/bin/env python3
"""
Capa 9 — Audit de cobertura del formato .scn.

Para cada campo JSON que aparece en src/core/ScnSerializer.cpp,
verifica que esté documentado en:
  - doc/db/file_format.json   (BD)
  - doc/manual/src/dev/scn-format.md  (manual técnico)

Estrategia: regex sobre string literals `"X"` en ScnSerializer.cpp,
filtrando los que son nombres de claves JSON (heurística: aparecen
junto a `j[...]` o `j.contains(...)` o son strings de assignment).
"""
import json
import re
from pathlib import Path

REPO   = Path(__file__).resolve().parent.parent

def color(s, code): return f"\033[{code}m{s}\033[0m"
def ok(s):   return color("✓", "32") + " " + s
def err(s):  return color("✗", "31") + " " + s
def warn(s): return color("·", "33") + " " + s

# Strings que aparecen en el código pero NO son claves JSON
# (son valores literales, constantes, magic strings, etc.)
NOT_FIELDS = {
    's',         # unidad de tiempo
    'rad',       # unidad de ángulo
    'in', 'out', # podrían ser keys, hay que verificar contexto
    'in0', 'out0',  # idem
}

def parse_scn_fields():
    """Extrae claves JSON usadas en ScnSerializer.cpp."""
    src = (REPO / 'src/core/ScnSerializer.cpp').read_text()
    # Heurística: una clave JSON aparece como `"X"` en uno de los
    # contextos: j["X"], j.contains("X"), j.value("X", ...),
    # j["X"] = ..., o como key en serialize.
    patterns = [
        r'(?:j|jn|je|jo|jdu|jobjs|jnodes|jedges)\s*\[\s*"([a-z_][a-z_0-9]*)"',
        r'\.contains\(\s*"([a-z_][a-z_0-9]*)"\s*\)',
        r'\.value\(\s*"([a-z_][a-z_0-9]*)"\s*,',
    ]
    fields = set()
    for p in patterns:
        for m in re.finditer(p, src):
            f = m.group(1)
            if f not in NOT_FIELDS:
                fields.add(f)
    return fields

def db_documented():
    """Devuelve set de campos listados en file_format.json."""
    db = json.load(open(REPO / 'doc/db/file_format.json'))
    out = set()
    out.add(db.get('version_field', ''))
    for section in ('fields', 'node_fields', 'edge_fields',
                    'object_fields', 'display_unit_fields',
                    'subgraph_fields'):
        for f in db.get(section, []):
            out.add(f['name'])
    out.discard('')
    return out

def md_documented():
    """Cuenta menciones en scn-format.md."""
    md = (REPO / 'doc/manual/src/dev/scn-format.md').read_text()
    return md

def main():
    print(color("━━━ Auditoría Capa 9: campos .scn ↔ doc ━━━", "1;36"))
    print()
    code_fields = parse_scn_fields()
    db_fields = db_documented()
    md = md_documented()

    print(f"  Campos en código (ScnSerializer.cpp): {len(code_fields)}")
    print(f"  Campos en BD (file_format.json)     : {len(db_fields)}")
    print()

    # Gaps
    in_code_not_in_db = sorted(code_fields - db_fields)
    in_db_not_in_code = sorted(db_fields - code_fields)
    in_code_not_in_md = sorted(f for f in code_fields if f not in md)

    if in_code_not_in_db:
        print(err(f"En código y NO en file_format.json: {len(in_code_not_in_db)}"))
        for f in in_code_not_in_db:
            print(f"    {f}")
    else:
        print(ok("Todos los campos del código están en file_format.json"))

    print()
    if in_db_not_in_code:
        print(warn(f"En BD y NO detectados en código: {len(in_db_not_in_code)} (puede ser falso positivo)"))
        for f in in_db_not_in_code:
            print(f"    {f}")
    else:
        print(ok("Todos los campos de la BD se ven en el código"))

    print()
    if in_code_not_in_md:
        print(err(f"En código y NO mencionados en scn-format.md: {len(in_code_not_in_md)}"))
        for f in in_code_not_in_md:
            print(f"    {f}")
    else:
        print(ok("Todos los campos del código se mencionan en scn-format.md"))

    print()
    # Versión actual vs BD
    db = json.load(open(REPO / 'doc/db/file_format.json'))
    src = (REPO / 'src/core/ScnSerializer.cpp').read_text()
    m = re.search(r'FILE_VERSION\s*=\s*"([0-9.]+)"', src)
    if not m:
        m = re.search(r'FILE_VERSION\s*\{\s*"([0-9.]+)"', src)
    if m:
        code_ver = m.group(1)
        db_ver = db.get('current_version', '?')
        if code_ver == db_ver:
            print(ok(f"Version: code={code_ver} == DB={db_ver}"))
        else:
            print(err(f"Version: code={code_ver} ≠ DB={db_ver}"))

    if not in_code_not_in_db and not in_code_not_in_md:
        print()
        print(color("CONSISTENT — todos los campos del .scn están documentados.", "1;32"))
    else:
        print()
        print(color("INCONSISTENT — actualizar BD y/o scn-format.md.", "1;31"))

if __name__ == '__main__':
    main()
