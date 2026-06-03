#!/usr/bin/env python3
"""
Capa 11 — Audit de cobertura de las reglas de la gramática.

Cierra el punto ciego: ninguna otra capa verificaba que el conjunto de
reglas R* documentado coincida con el que el código realmente emite.

Fuente de verdad: los códigos que aparecen en
  GrammarError{ "R<n>", ... }
dentro de src/core/GrammarParser.cpp (validateEdge, R0–R6) y
src/core/NodeGraph.cpp (tryAddEdge, R7 — consistencia dimensional).

Verifica que ese conjunto coincida con:
  - doc/db/grammar_rules.json   (BD)
  - doc/manual/src/dev/grammar.md  (manual de desarrollador)

Exit 0 si todo coincide; 1 si hay drift (regla en código sin documentar,
o regla documentada que el código ya no emite).
"""
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

def color(s, code): return f"\033[{code}m{s}\033[0m"
def ok(s):  return color("✓", "32") + " " + s
def err(s): return color("✗", "31") + " " + s

# Archivos donde el código construye GrammarError{ "R..", ... }
CODE_FILES = ['src/core/GrammarParser.cpp', 'src/core/NodeGraph.cpp']

def code_rules():
    """Conjunto de códigos R* que el código realmente emite."""
    rules = set()
    for rel in CODE_FILES:
        src = (REPO / rel).read_text()
        # GrammarError{ "Rn" ...  — \s permite el código en varias líneas
        for m in re.finditer(r'GrammarError\s*\{\s*"(R\d+)"', src):
            rules.add(m.group(1))
    return rules

def db_rules():
    db = json.load(open(REPO / 'doc/db/grammar_rules.json'))
    return {row['code'] for row in db['rows']}

def md_rules():
    md = (REPO / 'doc/manual/src/dev/grammar.md').read_text()
    return set(re.findall(r'\b(R\d+)\b', md))

def main():
    print(color("━━━ Auditoría Capa 11: reglas de gramática ↔ código ━━━", "1;36"))
    print()
    code = code_rules()
    db   = db_rules()
    md   = md_rules()

    print(f"  Reglas en código ({', '.join(CODE_FILES)}): "
          f"{', '.join(sorted(code))}")
    print(f"  Reglas en BD (grammar_rules.json)          : {', '.join(sorted(db))}")
    print(f"  Reglas en manual (dev/grammar.md)          : {', '.join(sorted(md))}")
    print()

    bad = False

    # 1) BD debe coincidir exactamente con el código
    code_not_db = sorted(code - db)
    db_not_code = sorted(db - code)
    if code_not_db:
        print(err(f"En código y NO en grammar_rules.json: {', '.join(code_not_db)}"))
        bad = True
    if db_not_code:
        print(err(f"En grammar_rules.json y NO en el código: {', '.join(db_not_code)}"))
        bad = True
    if not code_not_db and not db_not_code:
        print(ok("grammar_rules.json coincide con el código"))

    # 2) El manual debe documentar todas las reglas del código
    code_not_md = sorted(code - md)
    if code_not_md:
        print(err(f"En código y NO mencionadas en dev/grammar.md: {', '.join(code_not_md)}"))
        bad = True
    else:
        print(ok("dev/grammar.md menciona todas las reglas del código"))

    print()
    if bad:
        print(color("INCONSISTENT — actualizar grammar_rules.json y/o dev/grammar.md.", "1;31"))
        sys.exit(1)
    print(color("CONSISTENT — las reglas de la gramática están sincronizadas.", "1;32"))

if __name__ == '__main__':
    main()
