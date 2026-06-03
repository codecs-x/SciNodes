#!/usr/bin/env python3
"""
Capa 8 — Audit de cobertura de APIs públicas (clases/structs).

Para cada clase y struct definidos en src/**/*.hpp, cuenta menciones
en el manual mdBook (especialmente la sección dev/) y la tesis. Las
clases públicas del runtime (IComputeBackend, ScilabBridge, NodeGraph,
GrammarParser, etc.) deberían aparecer en el dev guide; los structs
internos no.

Categorías de salida:
  ✓ documentado     — al menos 3 menciones en mdBook
  · mención mínima  — 1-2 menciones (presente en lista, sin párrafo)
  ✗ sin doc         — 0 menciones en ningún lado

Heurística para clasificar 'pública' vs 'interna': si el header está
en src/core/ es probable que sea API pública del runtime; si está en
src/ui/ es UI implementación (suele documentarse menos). La salida
muestra ambos pero los gaps de core/ pesan más.
"""
import re
from pathlib import Path

REPO   = Path(__file__).resolve().parent.parent
THESIS = REPO.parent / "SciNodes-thesis"
MDBOOK = REPO / "doc/manual/src"

def color(s, code): return f"\033[{code}m{s}\033[0m"
def ok(s):   return color("✓", "32") + " " + s
def err(s):  return color("✗", "31") + " " + s
def warn(s): return color("·", "33") + " " + s

# Pattern: solo class/struct con cuerpo (descarta forward decls que
# terminan en ';')
RE_TYPE = re.compile(r'^(class|struct)\s+([A-Z][A-Za-z0-9_]+)\b(?!\s*;)', re.MULTILINE)

# Structs y clases que el regex detecta pero NO son APIs públicas
# (detalles de implementación: return types, structs de configuración,
# adapters internos, types intermedios). Filtrarlos saca el ruido del
# reporte sin perder señales reales.
SKIP_STRUCTS = {
    # Internos del codegen / eventos
    'CanvasDims', 'CanvasPos', 'CodegenSeedState', 'FrameStats',
    'GeneratedPlan', 'GeneratedSpec', 'GraphFingerprint',
    'GraphSnapshot', 'LegacyMeta', 'LinkCreatedEvent',
    'BackendPrepareSpec', 'DimensionalAnalysis',
    # DeviceAsset detalles internos
    'AssetAnchor', 'AssetJointFrame', 'AssetMesh',
    # ContractRegistry detalles internos
    'ContractAnchor', 'ContractJoint', 'ContractPart', 'DeviceContract',
    # ExampleLibrary detalles internos
    'ExampleEntry', 'SearchHit', 'SearchQuery',
    # Return types triviales
    'ParseQuantityResult', 'ParseUnitResult', 'ImportedObject',
    'RejectedEdge', 'SinkExport',
    # Type expressions internas
    'GeometryType', 'TensorType',
    # UI internos (adapters, structs de View3DPanel)
    'MachineGeometry', 'Mesh3D', 'SharedAssetBBox', 'V3',
}
SKIP_CLASSES = {
    # Adapters internos del PanelInterface (cada panel tiene su Adapter)
    'NodeEditorPanelAdapter', 'View3DPanelAdapter',
    'PlotsPanelAdapter', 'OutlinerPanelAdapter',
    # Sub-interfaces del Canvas internas
    'INodeRendererCore', 'INodeRendererQuery', 'INodeRendererSelection',
    # Area: detalle de layout del PanelRegistry
    'Area',
}

def discover_types():
    """Itera src/**/*.hpp y devuelve [(file, kind, name)] únicos."""
    seen = {}
    for f in (REPO / "src").rglob("*.hpp"):
        txt = f.read_text(errors='ignore')
        for m in RE_TYPE.finditer(txt):
            kind, name = m.group(1), m.group(2)
            if kind == 'struct' and name in SKIP_STRUCTS:
                continue
            if kind == 'class' and name in SKIP_CLASSES:
                continue
            if name not in seen:
                seen[name] = {
                    'kind': kind,
                    'file': str(f.relative_to(REPO)),
                    'layer': 'core' if '/core/' in str(f) else (
                             'app'  if '/app/'  in str(f) else 'ui'),
                }
    return seen

def all_files(root, exts):
    return [p for p in root.rglob("*") if p.suffix in exts and p.is_file()]

def count_in(needle, files):
    total = 0
    for f in files:
        try:
            txt = f.read_text(errors='ignore')
        except Exception:
            continue
        total += txt.count(needle)
    return total

def main():
    print(color("━━━ Auditoría Capa 8: APIs públicas ↔ doc ━━━", "1;36"))
    print()

    types = discover_types()
    print(f"  Clases/structs descubiertos : {len(types)}")
    by_layer = {'core': [], 'app': [], 'ui': []}
    for name, info in types.items():
        by_layer[info['layer']].append((name, info))
    for layer in ['core', 'app', 'ui']:
        print(f"    {layer:4s}: {len(by_layer[layer])}")

    md_files = all_files(MDBOOK, {'.md'})
    tex_files = all_files(THESIS / "chapters", {'.tex'}) if THESIS.exists() else []
    print(f"  mdBook archivos : {len(md_files)}")
    print(f"  Tesis archivos  : {len(tex_files)}")
    print()

    # Contar menciones
    results = []
    for name, info in types.items():
        md = count_in(name, md_files)
        th = count_in(name, tex_files)
        results.append({
            'name': name, **info,
            'md': md, 'th': th, 'total': md + th,
        })

    print(color("━━━ Por capa ━━━", "1;36"))
    for layer in ['core', 'app', 'ui']:
        items = [r for r in results if r['layer'] == layer]
        n_ok = sum(1 for r in items if r['md'] >= 3)
        n_min = sum(1 for r in items if 1 <= r['md'] <= 2)
        n_th_only = sum(1 for r in items if r['md'] == 0 and r['th'] > 0)
        n_gap = sum(1 for r in items if r['total'] == 0)
        print(f"  src/{layer:4s}/  total={len(items):2d}  OK={n_ok:2d}  "
              f"mín={n_min:2d}  solo-tesis={n_th_only:2d}  GAP={n_gap:2d}")

    print()
    print(color("━━━ Resumen ━━━", "1;36"))
    n = len(results)
    n_ok = sum(1 for r in results if r['md'] >= 3)
    n_doc = sum(1 for r in results if r['total'] > 0)
    n_gap = sum(1 for r in results if r['total'] == 0)
    print(f"  Total                          : {n}")
    print(f"  Con alguna mención (md o tex)  : {n_doc} ({100*n_doc//n}%)")
    print(f"  Bien documentados (3+ en md)   : {n_ok} ({100*n_ok//n}%)")
    print(f"  GAP (sin doc en ningún lado)   : {n_gap}")

    gaps = sorted([r for r in results if r['total'] == 0],
                  key=lambda r: (r['layer'] != 'core', r['name']))
    if gaps:
        print()
        print(color("━━━ GAPs (sin doc) ━━━", "1;36"))
        for r in gaps:
            sev = "CRÍTICA" if r['layer'] == 'core' else "menor"
            print(f"  ✗ {r['name']:30s} ({r['kind']}, {r['layer']:4s}, {r['file']}) [{sev}]")

    minimal_core = sorted([r for r in results
                           if r['layer'] == 'core' and 1 <= r['md'] <= 2],
                          key=lambda r: r['name'])
    if minimal_core:
        print()
        print(color("━━━ Core con mención mínima en mdBook ━━━", "1;33"))
        for r in minimal_core:
            print(f"  · {r['name']:30s} md={r['md']}  th={r['th']}  ({r['file']})")

    # Falla solo ante GAP real (API pública sin doc en ningún lado);
    # las menciones mínimas son advertencia, no error.
    if n_gap:
        raise SystemExit(1)

if __name__ == '__main__':
    main()
