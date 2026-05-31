#!/usr/bin/env python3
"""
Capa 10 — Audit de cobertura de internacionalización.

Para cada llamada `tr("X")` en src/, verifica que la key X exista en
i18n/es.json e i18n/en.json. Reporta:

  ✓ traducidas         — keys del código que están en es.json y en.json
  ✗ sin traducir       — keys del código que faltan en es.json o en.json
                          (el usuario verá el fallback derivado del key)
  · zombies            — keys en alguno de los JSON que no se usan en código
                          (traducciones obsoletas)
  ! desbalance         — keys que están en uno de los dos JSON pero no en
                          el otro (paridad rota entre idiomas)

Convención del proyecto (v0.1.1): es.json y en.json son tablas simétricas
explícitas; cualquier key del código debe existir en ambas, y cualquier
key de uno debe existir en el otro.
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

def load_lang_keys(lang):
    d = json.load(open(REPO / f'i18n/{lang}.json'))
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
    es_keys = load_lang_keys('es')
    en_keys = load_lang_keys('en')
    catalog = load_catalog_types()

    print(f"  Keys usadas en src/  (tr/trOr): {len(code_keys)}")
    print(f"  Keys en i18n/es.json          : {len(es_keys)}")
    print(f"  Keys en i18n/en.json          : {len(en_keys)}")
    print()

    # Una key está "traducida" si está en AMBOS idiomas.
    translated_keys = es_keys & en_keys
    missing = sorted(code_keys - translated_keys)
    zombies = sorted((es_keys | en_keys) - code_keys)
    common = code_keys & translated_keys
    prepared, real_zombies = categorize_zombies(zombies, catalog)

    # Paridad entre es y en: keys que existen en uno pero no en el otro.
    only_es = sorted(es_keys - en_keys)
    only_en = sorted(en_keys - es_keys)

    print(color("━━━ Cobertura ━━━", "1;36"))
    print(f"  Traducidas               : {len(common)} / {len(code_keys)}  ({100*len(common)//max(len(code_keys),1)}%)")
    print(f"  Sin traducir (gap)       : {len(missing)}")
    print(f"  Preparadas para futuro   : {len(prepared)}  (keys node.X.* para X en el catálogo)")
    print(f"  Zombies reales           : {len(real_zombies)}")

    if missing:
        print()
        print(err(f"━━━ Keys del código sin traducción en ambos idiomas ━━━"))
        for k in missing:
            in_es = "es" if k in es_keys else "  "
            in_en = "en" if k in en_keys else "  "
            print(f"    [{in_es} {in_en}]  {k}")
    else:
        print()
        print(ok("Todas las keys del código tienen traducción en es.json y en.json"))

    if only_es or only_en:
        print()
        print(err(f"━━━ Desbalance es ↔ en (paridad rota) ━━━"))
        for k in only_es:
            print(f"    solo en es.json: {k}")
        for k in only_en:
            print(f"    solo en en.json: {k}")
    else:
        print()
        print(ok("es.json y en.json tienen exactamente las mismas keys"))

    if real_zombies:
        print()
        print(warn(f"━━━ Zombies reales en es.json/en.json (no son keys node.X.*) ━━━"))
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
    if not missing and not only_es and not only_en:
        print(color("CONSISTENT — todas las keys del código tienen traducción "
                    "en ambos idiomas, y es/en están sincronizados.", "1;32"))
    else:
        print(color("INCONSISTENT — sincronizar es.json y en.json.", "1;31"))

if __name__ == '__main__':
    main()
