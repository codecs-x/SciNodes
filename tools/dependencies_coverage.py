#!/usr/bin/env python3
"""
Capa 12 — Audit de cobertura de las dependencias externas.

Cierra un punto ciego real: ninguna capa verificaba que `dependencies.json`
coincida con lo que `CMakeLists.txt` realmente declara. Por eso esa tabla
había driftado sin que nadie lo notara (decía C++17 + imnodes cuando el
código ya era C++20, había removido imnodes en v0.0.8 y agregado
glslang/tinygltf).

Fuente de verdad: `CMakeLists.txt`
  - estándar C++   : set(CMAKE_CXX_STANDARD N)
  - deps de build  : FetchContent_Declare(<name> ... GIT_TAG <tag>)
                     + find_package(Vulkan ...)

Verifica contra `doc/db/dependencies.json`:
  - cxx_standard coincide.
  - el conjunto de deps de build coincide (ni falta ni sobra ninguna).
  - la versión declarada en la BD contiene el GIT_TAG de CMake.

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

# Distintas grafías de una misma dependencia (CMake usa el nombre del target,
# la BD un nombre legible) → un token canónico común.
ALIASES = {
    'sdl2': 'sdl2',
    'imgui': 'imgui', 'dearimgui': 'imgui',
    'nlohmannjson': 'json', 'json': 'json',
    'glslang': 'glslang',
    'tinygltf': 'tinygltf',
    'vulkan': 'vulkan',
}

def canon(name):
    n = re.sub(r'[^a-z0-9]', '', name.lower())
    return ALIASES.get(n, n)

def cmake_facts():
    src = (REPO / 'CMakeLists.txt').read_text()

    m = re.search(r'set\(\s*CMAKE_CXX_STANDARD\s+(\d+)\s*\)', src)
    cxx = int(m.group(1)) if m else None

    # FetchContent_Declare(<name> ... GIT_TAG <tag> ...): el name es el primer
    # token tras el paréntesis; el GIT_TAG vive dentro del mismo bloque.
    deps = {}  # canon -> git_tag
    for m in re.finditer(r'FetchContent_Declare\(\s*(\w+)(.*?)\)',
                         src, re.DOTALL):
        name, body = m.group(1), m.group(2)
        tagm = re.search(r'GIT_TAG\s+(\S+)', body)
        deps[canon(name)] = tagm.group(1) if tagm else None

    # Vulkan no se baja: se resuelve con find_package.
    if re.search(r'find_package\(\s*Vulkan\b', src):
        deps[canon('Vulkan')] = None  # sin GIT_TAG; versión no se chequea

    return cxx, deps

def db_facts():
    db = json.load(open(REPO / 'doc/db/dependencies.json'))
    cxx = db.get('cxx_standard')
    rows = {canon(r['name']): r.get('version', '') for r in db['rows']}
    return cxx, rows

def main():
    print(color("━━━ Auditoría Capa 12: dependencias ↔ CMakeLists.txt ━━━", "1;36"))
    print()
    c_cxx, c_deps = cmake_facts()
    d_cxx, d_deps = db_facts()

    print(f"  Deps de build en CMake : {', '.join(sorted(c_deps))}")
    print(f"  Deps de build en BD    : {', '.join(sorted(d_deps))}")
    print(f"  C++ standard  CMake={c_cxx}  BD={d_cxx}")
    print()

    bad = False

    # 1) Estándar C++
    if c_cxx != d_cxx:
        print(err(f"cxx_standard difiere: CMake={c_cxx} vs BD={d_cxx}"))
        bad = True
    else:
        print(ok(f"cxx_standard coincide (C++{c_cxx})"))

    # 2) Conjunto de deps de build
    cmake_not_db = sorted(set(c_deps) - set(d_deps))
    db_not_cmake = sorted(set(d_deps) - set(c_deps))
    if cmake_not_db:
        print(err(f"En CMake y NO en dependencies.json: {', '.join(cmake_not_db)}"))
        bad = True
    if db_not_cmake:
        print(err(f"En dependencies.json y NO en CMake (¿stale?): {', '.join(db_not_cmake)}"))
        bad = True
    if not cmake_not_db and not db_not_cmake:
        print(ok("el conjunto de deps de build coincide"))

    # 3) Versión: el GIT_TAG de CMake debe aparecer en la versión de la BD
    for name in sorted(set(c_deps) & set(d_deps)):
        tag = c_deps[name]
        if tag is None:          # Vulkan (find_package): sin tag que chequear
            continue
        db_ver = d_deps[name]
        if tag not in db_ver:
            print(err(f"{name}: GIT_TAG '{tag}' (CMake) no aparece en la "
                      f"versión de la BD '{db_ver}'"))
            bad = True

    print()
    if bad:
        print(color("INCONSISTENT — actualizar dependencies.json contra CMakeLists.txt.", "1;31"))
        sys.exit(1)
    print(color("CONSISTENT — las dependencias están sincronizadas.", "1;32"))

if __name__ == '__main__':
    main()
