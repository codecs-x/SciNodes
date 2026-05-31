#!/usr/bin/env python3
"""
Capa 10 — Audit de cobertura de internacionalización.

Para cada llamada `tr("X")` en src/, verifica que la key X exista en
i18n/es.json. Reporta:

  ✓ traducidas         — keys del código que están en es.json
  ✗ sin traducir       — keys del código que faltan en es.json
                          (el usuario verá el fallback derivado del key)
  · zombies            — keys en es.json que no se usan en código
                          (traducciones obsoletas)

Convención del proyecto (v0.0.8): "fuente única para inglés" inline
en el fallback de tr(), por eso no existe en.json. Solo se mantiene
es.json explícitamente.
"""
import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

def color(s, code): return f"\033[{code}m{s}\033[0m"
def ok(s):   return color("✓", "32") + " " + s
def err(s):  return color("✗", "31") + " " + s
def warn(s): return color("·", "33") + " " + s

RE_TR = re.compile(r'\b(?:tr|trOr)\(\s*"([^"]+)"')

def extract_keys_from_code():
    """Devuelve set de keys usadas en src/**/*.{cpp,hpp}."""
    keys = set()
    for ext in ('*.cpp', '*.hpp'):
        for f in (REPO / 'src').rglob(ext):
            try:
                txt = f.read_text(errors='ignore')
            except Exception:
                continue
            for m in RE_TR.finditer(txt):
                keys.add(m.group(1))
    return keys

def load_es_keys():
    d = json.load(open(REPO / 'i18n/es.json'))
    return set(d.keys())

def load_catalog_types():
    """Devuelve set de NodeType names del catálogo."""
    db = json.load(open(REPO / 'doc/db/node_types.json'))
    return {r['type'] for r in db['rows']}

def categorize_zombies(zombies, catalog):
    """Separa zombies en (preparados para i18n del catálogo, otros)."""
    prepared = []
    real_zombies = []
    for k in zombies:
        # node.<TypeName>.{label,description,input_label.N,...}
        parts = k.split('.')
        if (len(parts) >= 3 and parts[0] == 'node'
                and parts[1] in catalog):
            prepared.append(k)
        else:
            real_zombies.append(k)
    return prepared, real_zombies

def main():
    print(color("━━━ Auditoría Capa 10: i18n ↔ código ━━━", "1;36"))
    print()
    code_keys = extract_keys_from_code()
    es_keys = load_es_keys()
    catalog = load_catalog_types()

    print(f"  Keys usadas en src/  (tr/trOr): {len(code_keys)}")
    print(f"  Keys en i18n/es.json          : {len(es_keys)}")
    print()

    missing = sorted(code_keys - es_keys)
    zombies = sorted(es_keys - code_keys)
    common = code_keys & es_keys
    prepared, real_zombies = categorize_zombies(zombies, catalog)

    print(color("━━━ Cobertura ━━━", "1;36"))
    print(f"  Traducidas               : {len(common)} / {len(code_keys)}  ({100*len(common)//max(len(code_keys),1)}%)")
    print(f"  Sin traducir (gap)       : {len(missing)}")
    print(f"  Preparadas para futuro   : {len(prepared)}  (keys node.X.* para X en el catálogo)")
    print(f"  Zombies reales           : {len(real_zombies)}")

    if missing:
        print()
        print(err(f"━━━ Keys del código sin traducción en es.json ━━━"))
        for k in missing:
            print(f"    {k}")
    else:
        print()
        print(ok("Todas las keys del código tienen traducción en es.json"))

    if real_zombies:
        print()
        print(warn(f"━━━ Zombies reales en es.json (no son keys node.X.*) ━━━"))
        for k in real_zombies[:30]:
            print(f"    {k}")
        if len(real_zombies) > 30:
            print(f"    ... ({len(real_zombies)-30} más)")
    else:
        print()
        print(ok("Sin zombies reales — todas las keys huérfanas son node.X.* preparadas para i18n del catálogo (feature pendiente)"))

    if prepared:
        print()
        print(color(f"━━━ Resumen 'preparadas para futuro' ({len(prepared)}) ━━━", "1;36"))
        # agrupar por tipo
        by_type = {}
        for k in prepared:
            t = k.split('.')[1]
            by_type.setdefault(t, []).append(k)
        coverage = len(by_type) / len(catalog) * 100
        print(f"    {len(by_type)} de {len(catalog)} tipos del catálogo tienen entradas en es.json ({coverage:.0f}%)")
        print("    Estado: las keys existen, pero el código NO las consume.")
        print("    Cuando el i18n del catálogo se active (tr('node.' + typeName + '.label')),")
        print("    estas keys pasarán a 'traducidas' sin requerir cambios al JSON.")

    print()
    if not missing:
        print(color("CONSISTENT — todas las keys del código tienen traducción.", "1;32"))
    else:
        print(color("INCONSISTENT — agregar traducciones a es.json.", "1;31"))

if __name__ == '__main__':
    main()
