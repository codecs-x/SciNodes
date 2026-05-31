#!/usr/bin/env python3
"""
Capa 7 — Audit de cobertura descriptiva del catálogo de nodos.

La Capa 1 (triple_audit.py) verifica que cada nodo del catálogo tenga
entrada en doc/db/node_types.json con sus puertos/params correctos.
Pero el manual mdBook (la doc que el usuario final lee) puede no
mencionar ese nodo aunque esté en la BD.

Este audit cubre el gap: para cada uno de los 64 tipos del catálogo,
busca menciones literales del 'type' técnico y del 'label' legible
en cualquier página del manual mdBook + capítulos de la tesis.

Categorías de cobertura:
  ✓ documentado     — el nombre del nodo aparece en alguna página
  · solo-tesis      — solo en la tesis, no en el manual (raro)
  · mención mínima  — aparece 1 vez (puede ser solo en una tabla o
                     en una lista; no necesariamente con descripción)
  ✗ sin mencionar   — el nodo está en la BD pero ninguna página
                     menciona ni su type ni su label.

El reporte distingue 'sin doc' (el problema serio: el nodo es invisible
para el usuario final) de 'mención mínima' (presente, pero quizás merece
más prosa).
"""
import json
import re
from pathlib import Path

REPO   = Path(__file__).resolve().parent.parent
THESIS = REPO.parent / "SciNodes-thesis"
MDBOOK = REPO / "doc/manual/src"

def color(s, code): return f"\033[{code}m{s}\033[0m"
def ok(s):   return color("✓", "32") + " " + s
def err(s):  return color("✗", "31") + " " + s
def warn(s): return color("·", "33") + " " + s

def db_nodes():
    db = json.load(open(REPO / "doc/db/node_types.json"))
    return [{
        'type': r['type'],
        'label': r['label'],
        'category': r['category'],
    } for r in db['rows']]

def all_files(root, exts):
    return [p for p in root.rglob("*") if p.suffix in exts and p.is_file()]

def count_mentions(needle, files):
    """Cuenta cuántas veces aparece needle en el conjunto de files."""
    if not needle: return 0
    total = 0
    for f in files:
        try:
            txt = f.read_text(errors='ignore')
        except Exception:
            continue
        total += txt.count(needle)
    return total

def main():
    print(color("━━━ Auditoría Capa 7: catálogo descriptivo ↔ doc ━━━", "1;36"))
    print()

    nodes = db_nodes()
    print(f"  Catálogo : {len(nodes)} tipos")

    md_files = all_files(MDBOOK, {'.md'})
    tex_files = all_files(THESIS / "chapters", {'.tex'}) if THESIS.exists() else []
    print(f"  mdBook   : {len(md_files)} archivos .md")
    print(f"  Tesis    : {len(tex_files)} archivos .tex")
    print()

    # Por nodo, contar menciones
    results = []
    for n in nodes:
        md_type = count_mentions(n['type'], md_files)
        md_label = count_mentions(n['label'], md_files)
        th_type = count_mentions(n['type'], tex_files)
        th_label = count_mentions(n['label'], tex_files)
        md_total = md_type + md_label
        th_total = th_type + th_label
        total = md_total + th_total
        results.append({
            **n,
            'md_type': md_type, 'md_label': md_label,
            'th_type': th_type, 'th_label': th_label,
            'md_total': md_total, 'th_total': th_total,
            'total': total,
        })

    # Clasificación
    no_doc = [r for r in results if r['total'] == 0]
    only_thesis = [r for r in results if r['md_total'] == 0 and r['th_total'] > 0]
    only_mdbook = [r for r in results if r['md_total'] > 0 and r['th_total'] == 0]
    minimal_md = [r for r in results if r['md_total'] in (1, 2)]
    well_doc = [r for r in results if r['md_total'] >= 3]

    print(color("━━━ Por categoría ━━━", "1;36"))
    # agrupar por categoría
    by_cat = {}
    for r in results:
        by_cat.setdefault(r['category'], []).append(r)
    for cat in ['Source', 'Transformer', 'Device', 'Sink']:
        items = by_cat.get(cat, [])
        n_no = sum(1 for r in items if r['total'] == 0)
        n_min = sum(1 for r in items if 1 <= r['md_total'] <= 2)
        n_ok = sum(1 for r in items if r['md_total'] >= 3)
        n_only_th = sum(1 for r in items if r['md_total'] == 0 and r['th_total'] > 0)
        print(f"  {cat:12s}: {len(items):2d} total  OK={n_ok:2d}  mínima={n_min:2d}  solo-tesis={n_only_th:2d}  GAP={n_no:2d}")

    print()
    print(color("━━━ Resumen global ━━━", "1;36"))
    n = len(nodes)
    print(f"  Bien documentados (3+ menciones en mdBook): {len(well_doc)}/{n} ({100*len(well_doc)//n}%)")
    print(f"  Mención mínima (1-2 en mdBook)             : {len(minimal_md)}")
    print(f"  Solo en tesis, no en mdBook                : {len(only_thesis)}")
    print(f"  GAP (sin doc en ningún lado)               : {len(no_doc)}")

    if no_doc:
        print()
        print(color("━━━ GAPS críticos (sin doc) ━━━", "1;36"))
        for r in no_doc:
            print(f"  ✗ {r['type']:30s} ({r['category']}) — label: {r['label']!r}")

    if only_thesis:
        print()
        print(color("━━━ Solo en tesis, falta en mdBook ━━━", "1;33"))
        for r in only_thesis:
            print(f"  · {r['type']:30s} ({r['category']}) — th={r['th_total']}, md=0")

    if minimal_md:
        print()
        print(color("━━━ Mención mínima en mdBook (1-2 veces) ━━━", "1;33"))
        for r in minimal_md[:20]:
            print(f"  · {r['type']:30s} md={r['md_total']}  th={r['th_total']}")
        if len(minimal_md) > 20:
            print(f"  ... ({len(minimal_md)-20} más)")

if __name__ == '__main__':
    main()
