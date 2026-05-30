#!/usr/bin/env python3
"""
Triple-strategy audit: BD (doc/db/) vs código.

Tres ventanas independientes sobre el mismo objeto:

1. PARSE  — regex sobre el .cpp del registry. Captura lo que el archivo
            fuente declara textualmente.
2. INTROSPECT — ejecuta ./build/dump_catalog que llama a nodeRegistry()
            en runtime y emite JSON. Captura lo que el binario, ya
            linkeado, realmente expone.
3. RUNTIME — corre ./build/test_grammar y ./build/test_integration y
            parsea su output. Captura los conteos reales de aserciones y
            pruebas que el código ejerce.

Si las tres estrategias coinciden con la BD, la sincronización es real.
Si discrepan, el desacuerdo dice qué capa está mintiendo.
"""
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DB   = REPO / "doc" / "db"

def color(s, code):
    return f"\033[{code}m{s}\033[0m"

def banner(s):
    print()
    print(color(f"━━━ {s} ━━━", "1;36"))

def ok(s):    return color("✓", "32") + " " + s
def err(s):   return color("✗", "31") + " " + s
def warn(s):  return color("·", "33") + " " + s

# ---------- Strategy 1 ─ Parse ---------------------------------------------

def parse_catalog():
    src = (REPO / "src/core/NodeType.cpp").read_text()
    re_header = re.compile(r'NodeType::(\w+),\s+NodeCategory::(\w+),', re.MULTILINE)
    positions = [(m.start(), m.group(1), m.group(2)) for m in re_header.finditer(src)]
    re_p = re.compile(r'\{\s*"([^"]+)",\s+([-\d.e]+),\s+"([^"]*)"\s*\}')
    out = {}
    for i, (pos, t, cat) in enumerate(positions):
        end = positions[i+1][0] if i+1 < len(positions) else len(src)
        block = src[pos:end]
        # Read label + input/output ports near top
        m_lbl = re.search(r'"([^"]+)"', block[block.find(','):])
        m_ports = re.search(r'(\d+),\s+(\d+),\s*\n\s*\{', block)
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

# ---------- Strategy 2 ─ Introspect binary ---------------------------------

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

# ---------- Strategy 3 ─ Run tests -----------------------------------------

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

# ---------- Comparison -----------------------------------------------------

def diff_catalogs(name_a, a, name_b, b):
    """Return list of (path, a_val, b_val) where they disagree."""
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

def db_catalog():
    db = json.load(open(DB / "node_types.json"))
    return {r['type']: {
        'category': r['category'],
        'label': r['label'],
        'input_ports': r['input_ports'],
        'output_ports': r['output_ports'],
        'params': r['params'],
    } for r in db['rows']}

# ---------- Main -----------------------------------------------------------

def main():
    banner("Strategy 1 — Static parse (regex sobre NodeType.cpp)")
    parse = parse_catalog()
    print(f"  Tipos extraídos: {len(parse)}")

    banner("Strategy 2 — Binary introspection (dump_catalog ejecutado)")
    if not (REPO / "build/dump_catalog").exists():
        print(err("build/dump_catalog no existe. Ejecuta: cmake --build build --target dump_catalog"))
        sys.exit(1)
    intro = introspect_catalog()
    print(f"  Tipos expuestos: {len(intro)}")

    banner("Strategy 3 — Test execution (test_grammar + test_integration)")
    tests = run_tests()
    tg_pass, tg_fail = tests['test_grammar']
    ti_pass, ti_fail = tests['test_integration']
    print(f"  test_grammar:     {tg_pass} passed, {tg_fail} failed")
    print(f"  test_integration: {ti_pass} passed, {ti_fail} failed")

    db = db_catalog()
    print(f"  DB tipos: {len(db)}")

    banner("Triangulación: DB ↔ Parse ↔ Introspect ↔ Tests")

    # DB vs Parse
    d_parse = diff_catalogs("DB", db, "PARSE", parse)
    # DB vs Introspect
    d_intro = diff_catalogs("DB", db, "INTROSPECT", intro)
    # Parse vs Introspect (independent of DB!)
    d_pi    = diff_catalogs("PARSE", parse, "INTROSPECT", intro)
    # Test counts vs DB totals
    db_tests = json.load(open(DB / "test_scenarios.json"))['totals']

    def banner_diff(label, diffs):
        if not diffs:
            print(ok(f"{label}: 0 diffs"))
        else:
            print(err(f"{label}: {len(diffs)} diffs"))
            for path, a, b in diffs[:10]:
                print(f"    {path}: {a!r}  vs  {b!r}")

    banner_diff("DB vs PARSE        ", d_parse)
    banner_diff("DB vs INTROSPECT   ", d_intro)
    banner_diff("PARSE vs INTROSPECT", d_pi)

    print()
    tests_ok = True
    if tg_pass != db_tests['test_grammar_asserts'] or tg_fail != 0:
        print(err(f"test_grammar: BD esperaba {db_tests['test_grammar_asserts']} passed / 0 failed, "
                  f"runtime reportó {tg_pass}/{tg_fail}"))
        tests_ok = False
    else:
        print(ok(f"test_grammar:     DB={db_tests['test_grammar_asserts']} == runtime={tg_pass}"))

    if ti_pass != db_tests['test_integration_asserts'] or ti_fail != 0:
        print(err(f"test_integration: BD esperaba {db_tests['test_integration_asserts']} passed / 0 failed, "
                  f"runtime reportó {ti_pass}/{ti_fail}"))
        tests_ok = False
    else:
        print(ok(f"test_integration: DB={db_tests['test_integration_asserts']} == runtime={ti_pass}"))

    print()
    consistent = (not d_parse and not d_intro and not d_pi and tests_ok)
    if consistent:
        print(color("CONSISTENT — las tres estrategias coinciden con la BD.", "1;32"))
        sys.exit(0)
    else:
        print(color("INCONSISTENT — revisar diffs arriba.", "1;31"))
        sys.exit(1)

if __name__ == '__main__':
    main()
