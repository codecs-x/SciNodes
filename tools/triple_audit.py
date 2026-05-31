#!/usr/bin/env python3
"""
Triple-strategy audit: BD (doc/db/) vs código.

Para cada capa auditable del editor (catálogo de nodos, menús, atajos,
paneles, conteos de tests), tres ventanas independientes sobre el
mismo objeto:

1. PARSE  — regex sobre el código fuente. Captura lo que el archivo
            fuente declara textualmente.
2. INTROSPECT — ejecuta ./build/dump_catalog que llama a nodeRegistry()
            en runtime y emite JSON. Captura lo que el binario, ya
            linkeado, realmente expone. (Solo aplica a la capa catálogo
            por ahora.)
3. RUNTIME — corre ./build/test_grammar y ./build/test_integration y
            parsea su output. Captura los conteos reales de aserciones
            y pruebas que el código ejerce.

Si las tres estrategias coinciden con la BD, la sincronización es real.
Si discrepan, el desacuerdo dice qué capa está mintiendo.

Cobertura por capa:
  catálogo nodos   PARSE  ↔ INTROSPECT ↔ DB
  test counts      RUNTIME            ↔ DB
  menú             PARSE              ↔ DB (i18n keys ↔ labels)
  atajos           PARSE              ↔ DB
  paneles          PARSE              ↔ DB (miembros AppWindow ↔ modules)
"""
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DB   = REPO / "doc" / "db"
I18N = REPO / "i18n"

def color(s, code):
    return f"\033[{code}m{s}\033[0m"

def banner(s):
    print()
    print(color(f"━━━ {s} ━━━", "1;36"))

def ok(s):    return color("✓", "32") + " " + s
def err(s):   return color("✗", "31") + " " + s
def warn(s):  return color("·", "33") + " " + s

# =========================================================================
# CAPA 1 — Catálogo de nodos
# =========================================================================

def parse_catalog():
    src = (REPO / "src/core/NodeType.cpp").read_text()
    re_header = re.compile(r'NodeType::(\w+),\s+NodeCategory::(\w+),', re.MULTILINE)
    positions = [(m.start(), m.group(1), m.group(2)) for m in re_header.finditer(src)]
    re_p = re.compile(r'\{\s*"([^"]+)",\s*([-\d.e]+),\s*"([^"]*)"\s*\}')
    out = {}
    for i, (pos, t, cat) in enumerate(positions):
        end = positions[i+1][0] if i+1 < len(positions) else len(src)
        block = src[pos:end]
        m_lbl = re.search(r'"([^"]+)"', block[block.find(','):])
        m_ports = re.search(r'(\d+),\s*(\d+),(?:[\s/].*?)*?\s*\{', block, re.DOTALL)
        params = [{'name': n, 'default': float(v), 'unit': u}
                  for n, v, u in re_p.findall(block)]
        if m_lbl and m_ports:
            out[t] = {
                'category': cat,
                'label': m_lbl.group(1),
                'input_ports': int(m_ports.group(1)),
                'output_ports': int(m_ports.group(2)),
                'params': params,
            }
    return out

def introspect_catalog():
    proc = subprocess.run([str(REPO / "build/dump_catalog")],
                          capture_output=True, text=True, check=True)
    data = json.loads(proc.stdout)
    return {r['type']: {
        'category': r['category'],
        'label': r['label'],
        'input_ports': r['input_ports'],
        'output_ports': r['output_ports'],
        'params': r['params'],
    } for r in data['rows']}

def db_catalog():
    db = json.load(open(DB / "node_types.json"))
    return {r['type']: {
        'category': r['category'],
        'label': r['label'],
        'input_ports': r['input_ports'],
        'output_ports': r['output_ports'],
        'params': r['params'],
    } for r in db['rows']}

def diff_catalogs(a, b):
    diffs = []
    keys = set(a) | set(b)
    for t in sorted(keys):
        if t not in a:
            diffs.append((t, '<missing>', b[t]))
            continue
        if t not in b:
            diffs.append((t, a[t], '<missing>'))
            continue
        for k in ['category', 'label', 'input_ports', 'output_ports']:
            if a[t][k] != b[t][k]:
                diffs.append((f'{t}.{k}', a[t][k], b[t][k]))
        ap, bp = a[t]['params'], b[t]['params']
        if len(ap) != len(bp):
            diffs.append((f'{t}.params.len', len(ap), len(bp)))
        else:
            for i, (xp, yp) in enumerate(zip(ap, bp)):
                if xp != yp:
                    diffs.append((f'{t}.params[{i}]', xp, yp))
    return diffs

# =========================================================================
# CAPA 2 — Tests
# =========================================================================

def run_tests():
    g = subprocess.run([str(REPO / "build/test_grammar")],
                       capture_output=True, text=True)
    i = subprocess.run([str(REPO / "build/test_integration")],
                       capture_output=True, text=True)
    def parse(out):
        m = re.search(r'(\d+) passed,\s+(\d+) failed', out)
        return (int(m.group(1)), int(m.group(2))) if m else (None, None)
    return {
        'test_grammar':     parse(g.stdout),
        'test_integration': parse(i.stdout),
    }

# =========================================================================
# CAPA 3 — Menús (i18n keys del código vs labels de la BD)
# =========================================================================

# Patrón canónico en AppWindow.cpp:
#   ImGui::MenuItem(tr("menu.file.new").c_str(),  "Ctrl+N")
#   ImGui::BeginMenu(tr("menu.view").c_str())
RE_MENU = re.compile(
    r'ImGui::(MenuItem|BeginMenu)\(\s*tr\("([^"]+)"\)\.c_str\(\)'
    r'(?:\s*,\s*"([^"]*)")?'  # opt: shortcut string
)

def load_i18n_es():
    return json.load(open(I18N / "es.json"))

def parse_menu_keys_from_code():
    src = (REPO / "src/app/AppWindow.cpp").read_text()
    items = []
    for m in RE_MENU.finditer(src):
        kind, key, shortcut = m.group(1), m.group(2), m.group(3) or ""
        items.append({'kind': kind, 'i18n_key': key, 'shortcut': shortcut})
    return items

def db_menu_items():
    db = json.load(open(DB / "menu_items.json"))
    return db['rows']

def audit_menus():
    i18n = load_i18n_es()
    code = parse_menu_keys_from_code()
    db = db_menu_items()

    # Pivote estable: i18n_key (independiente del idioma activo).
    code_items = [c for c in code if c['kind'] == 'MenuItem']
    code_keys = {c['i18n_key']: c for c in code_items}
    db_keys = {r['i18n_key']: r for r in db if 'i18n_key' in r}

    missing_i18n = [k for k in code_keys if k not in i18n]
    in_code_only = [{'key': k, 'label': i18n.get(k, ''), 'shortcut': c['shortcut']}
                    for k, c in code_keys.items() if k not in db_keys]
    in_db_only = [{'key': k, 'label': r.get('label_es', '')}
                  for k, r in db_keys.items() if k not in code_keys]

    # Comprobación adicional: cada menú debe aparecer en algún
    # archivo del manual mdBook. Match laxo: acepta el label_es,
    # un fallback derivado del key (estilo inglés "Reset Canvas")
    # o el último componente del key como hint.
    md_dir = REPO / 'doc/manual/src'
    md_files = list(md_dir.rglob('*.md')) if md_dir.exists() else []
    md_text = ""
    for f in md_files:
        try:
            md_text += f.read_text(errors='ignore') + "\n"
        except Exception:
            continue
    def english_from_key(k):
        """menu.view.reset_canvas -> 'Reset Canvas'"""
        last = k.rsplit('.', 1)[-1]
        return ' '.join(w.capitalize() for w in last.split('_'))
    missing_in_md = []
    for k, c in code_keys.items():
        label_es = i18n.get(k, '').rstrip('…').strip()
        eng = english_from_key(k)
        if label_es and label_es in md_text:
            continue
        if eng and eng in md_text:
            continue
        missing_in_md.append({'key': k, 'label': label_es or eng})

    return {
        'code_count': len(code_items),
        'db_count': len(db),
        'in_code_only': in_code_only,
        'in_db_only': in_db_only,
        'missing_i18n': missing_i18n,
        'missing_in_md': missing_in_md,
    }

# =========================================================================
# CAPA 4 — Atajos de teclado
# =========================================================================

# Atajos en menús: ImGui::MenuItem(..., "Ctrl+N")
# Atajos en NodeCanvas: ImGui::IsKeyPressed(ImGuiKey_X, ...) precedido por
#   GetIO().KeyShift / KeyCtrl
# Hacemos una pasada por AppWindow para los del menú y otra por canvas
# para los del editor.

RE_MENU_SHORTCUT = re.compile(r'ImGui::MenuItem\(\s*tr\("[^"]+"\)\.c_str\(\)\s*,\s*"([^"]+)"')

# Pattern dentro de canvas: detecta los más comunes (no exhaustivo)
RE_KEY = re.compile(r'ImGui::IsKeyPressed\(ImGuiKey_([A-Za-z0-9_]+)')

def parse_shortcuts_from_code():
    """Devuelve set de combos detectados, incluyendo modifier inference."""
    combos = set()

    # 1) Menú: el shortcut viene literal
    aw = (REPO / "src/app/AppWindow.cpp").read_text()
    for m in RE_MENU_SHORTCUT.finditer(aw):
        combos.add(m.group(1))

    # 2) Canvas: hay que inferir Ctrl/Shift mirando el contexto ±3 líneas
    canvas = (REPO / "src/ui/NodeCanvas.cpp").read_text()
    lines = canvas.split('\n')
    for ln_idx, line in enumerate(lines):
        for m in RE_KEY.finditer(line):
            key = m.group(1)
            # mira ±4 líneas para detectar modificadores
            context = '\n'.join(lines[max(0, ln_idx-4):ln_idx+1])
            mods = []
            if 'KeyCtrl' in context:
                mods.append('Ctrl')
            if 'KeyShift' in context:
                mods.append('Shift')
            if 'KeyAlt' in context:
                mods.append('Alt')
            # Normalizar nombres de tecla
            key_str = key
            if key in ('Delete', 'Backspace', 'Home', 'Tab', 'Escape', 'Enter'):
                pass  # ya está bien
            elif len(key) == 1 or key.startswith('F') and key[1:].isdigit():
                pass  # A, B, ..., F1, F12
            combos.add('+'.join(mods + [key_str]))
    return combos

def db_shortcuts():
    db = json.load(open(DB / "shortcuts.json"))
    return {r['combo'] for r in db['rows']}

def audit_shortcuts():
    code = parse_shortcuts_from_code()
    db = db_shortcuts()

    # Alias: NodeCanvas usa "Z" para undo (con Ctrl detectado), "Y" para redo,
    # pero no detecta el Ctrl en line 996 sin contexto. Hacemos algunas normalizaciones.
    # No tocamos esto por ahora — el script reporta el set real y el usuario decide.
    only_code = sorted(code - db)
    only_db = sorted(db - code)
    return {
        'code_count': len(code),
        'db_count': len(db),
        'only_code': only_code,
        'only_db': only_db,
    }

# =========================================================================
# CAPA 5 — Paneles (miembros del AppWindow vs modules.json layer=ui)
# =========================================================================

# Detecta miembros tipo "XPanel m_x;" o "XBrowser m_x;" en AppWindow.hpp
RE_PANEL_MEMBER = re.compile(
    r'^\s*(\w*(?:Panel|Browser|Canvas|StatusBar|Palette))\s+m_\w+\s*;',
    re.MULTILINE
)

def parse_panels_from_code():
    src = (REPO / "src/app/AppWindow.hpp").read_text()
    return set(RE_PANEL_MEMBER.findall(src))

def db_panels():
    db = json.load(open(DB / "modules.json"))
    ui = [r for r in db['rows'] if r['layer'] == 'ui']
    # El "module" puede ser compuesto: "OutlinerPanel + AssetMappingPanel"
    # Lo dividimos por "+", "/" y palabras clave.
    names = set()
    for r in ui:
        parts = re.split(r'[+/]', r['module'])
        for p in parts:
            p = p.strip()
            # Quitar sufijos como "split", "renderers"
            p = re.sub(r'\s+split\b|\s+renderers\b', '', p, flags=re.I).strip()
            if p:
                names.add(p)
    return names

def audit_panels():
    code = parse_panels_from_code()
    db = db_panels()
    return {
        'code_count': len(code),
        'db_count_modules_ui': len(db),
        'code_panels': sorted(code),
        'only_code': sorted(code - db),
        'only_db_modules': sorted(db - code),
    }

# =========================================================================
# Main
# =========================================================================

def banner_diff(label, diffs):
    if not diffs:
        print(ok(f"{label}: 0 diffs"))
    else:
        print(err(f"{label}: {len(diffs)} diffs"))
        for path, a, b in diffs[:10]:
            print(f"    {path}: {a!r}  vs  {b!r}")

def main():
    # ---------- Capa 1: Catálogo de nodos ---------------------------------
    banner("Capa 1: Catálogo de nodos — PARSE ↔ INTROSPECT ↔ DB")

    parse = parse_catalog()
    print(f"  PARSE       : {len(parse)} tipos")
    if not (REPO / "build/dump_catalog").exists():
        print(err("build/dump_catalog no existe. cmake --build build --target dump_catalog"))
        sys.exit(1)
    intro = introspect_catalog()
    print(f"  INTROSPECT  : {len(intro)} tipos")
    db = db_catalog()
    print(f"  DB          : {len(db)} tipos")
    banner_diff("  DB ↔ PARSE        ", diff_catalogs(db, parse))
    banner_diff("  DB ↔ INTROSPECT   ", diff_catalogs(db, intro))
    banner_diff("  PARSE ↔ INTROSPECT", diff_catalogs(parse, intro))

    catalog_ok = (not diff_catalogs(db, parse) and not diff_catalogs(db, intro)
                  and not diff_catalogs(parse, intro))

    # ---------- Capa 2: Tests ---------------------------------------------
    banner("Capa 2: Tests — RUNTIME ↔ DB")
    tests = run_tests()
    tg_pass, tg_fail = tests['test_grammar']
    ti_pass, ti_fail = tests['test_integration']
    db_tests = json.load(open(DB / "test_scenarios.json"))['totals']
    tests_ok = True
    if tg_pass != db_tests['test_grammar_asserts'] or tg_fail != 0:
        print(err(f"  test_grammar: DB={db_tests['test_grammar_asserts']} ≠ runtime={tg_pass}/{tg_fail}"))
        tests_ok = False
    else:
        print(ok(f"  test_grammar:     DB={db_tests['test_grammar_asserts']} == runtime={tg_pass}"))
    if ti_pass != db_tests['test_integration_asserts'] or ti_fail != 0:
        print(err(f"  test_integration: DB={db_tests['test_integration_asserts']} ≠ runtime={ti_pass}/{ti_fail}"))
        tests_ok = False
    else:
        print(ok(f"  test_integration: DB={db_tests['test_integration_asserts']} == runtime={ti_pass}"))

    # ---------- Capa 3: Menús --------------------------------------------
    banner("Capa 3: Menús — PARSE (i18n keys) ↔ DB")
    m = audit_menus()
    print(f"  CODE        : {m['code_count']} MenuItems (con tr(\"menu.X\"))")
    print(f"  DB          : {m['db_count']} rows en menu_items.json")
    menus_ok = True
    if m['missing_i18n']:
        print(err(f"  i18n keys sin traducción en es.json: {len(m['missing_i18n'])}"))
        for k in m['missing_i18n'][:5]:
            print(f"    {k}")
        menus_ok = False
    if m['in_code_only']:
        print(err(f"  En código y NO en BD: {len(m['in_code_only'])}"))
        for c in m['in_code_only']:
            sc = f" [{c['shortcut']}]" if c['shortcut'] else ""
            print(f"    {c['label']:35s} ({c['key']}){sc}")
        menus_ok = False
    if m['in_db_only']:
        print(err(f"  En BD y NO en código: {len(m['in_db_only'])}"))
        for r in m['in_db_only']:
            print(f"    {r['label']:35s} (menu={r['menu']})")
        menus_ok = False
    if m['missing_in_md']:
        print(err(f"  Labels SIN mención en el manual mdBook: {len(m['missing_in_md'])}"))
        for r in m['missing_in_md']:
            print(f"    {r['label']:35s} ({r['key']})")
        menus_ok = False
    if menus_ok:
        print(ok(f"  Menús sincronizados (BD + manual)"))

    # ---------- Capa 4: Atajos -------------------------------------------
    banner("Capa 4: Atajos — PARSE (AppWindow + NodeCanvas) ↔ DB")
    s = audit_shortcuts()
    print(f"  CODE        : {s['code_count']} combos detectados")
    print(f"  DB          : {s['db_count']} combos en shortcuts.json")
    shortcuts_ok = True
    if s['only_code']:
        print(err(f"  En código y NO en BD: {len(s['only_code'])}"))
        for c in s['only_code'][:15]:
            print(f"    {c}")
        shortcuts_ok = False
    if s['only_db']:
        print(warn(f"  En BD y NO detectado en código: {len(s['only_db'])} (puede ser falso positivo del regex)"))
        for c in s['only_db'][:10]:
            print(f"    {c}")
    if not s['only_code']:
        print(ok(f"  Atajos del código presentes en BD"))

    # ---------- Capa 5: Paneles -----------------------------------------
    banner("Capa 5: Paneles — PARSE (miembros AppWindow.hpp) ↔ modules.json")
    p = audit_panels()
    print(f"  CODE        : {p['code_count']} paneles miembros (Panel/Browser/Canvas/StatusBar)")
    print(f"  DB modules  : {p['db_count_modules_ui']} entradas con layer=ui")
    print(f"  paneles en código: {', '.join(p['code_panels'])}")
    panels_ok = True
    if p['only_code']:
        print(err(f"  En código y NO en modules.json: {len(p['only_code'])}"))
        for c in p['only_code']:
            print(f"    {c}")
        panels_ok = False
    if p['only_db_modules']:
        # Esto es informativo; modules.json puede listar módulos no-panel
        # (NodeCanvas, INodeRenderer, etc.)
        pass
    if panels_ok:
        print(ok(f"  Paneles del código listados en modules.json"))

    # ---------- Veredicto -----------------------------------------------
    print()
    consistent = catalog_ok and tests_ok and menus_ok and shortcuts_ok and panels_ok
    if consistent:
        print(color("CONSISTENT — las 5 capas están sincronizadas con la BD.", "1;32"))
        sys.exit(0)
    else:
        capas = []
        if not catalog_ok:   capas.append("catálogo")
        if not tests_ok:     capas.append("tests")
        if not menus_ok:     capas.append("menús")
        if not shortcuts_ok: capas.append("atajos")
        if not panels_ok:    capas.append("paneles")
        print(color(f"INCONSISTENT en {len(capas)} capa(s): {', '.join(capas)}.", "1;31"))
        sys.exit(1)

if __name__ == '__main__':
    main()
