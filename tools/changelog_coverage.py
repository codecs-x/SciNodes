#!/usr/bin/env python3
"""
Audit de cobertura: cada feature del CHANGELOG debe aparecer en la doc
(manual mdBook + tesis LaTeX).

Estrategia: extraer cada bullet `- **X**` del CHANGELOG, normalizar X
como "feature key" (eliminando backticks, paréntesis, etc.), y buscar
si alguna página del manual o algún capítulo de la tesis lo menciona.

Salida: tres categorías por tag:
  ✓ documentado    — al menos una mención en mdBook o tesis
  · solo-mdBook    — está en el manual pero no en la tesis (caso normal
                     para features muy internas)
  · solo-tesis     — está en la tesis pero no en el manual (raro)
  ✗ sin documentar — no aparece en ninguna parte

Limitación: el match es literal (case-insensitive). Una feature
documentada con un nombre distinto al del CHANGELOG aparecerá como
falso negativo. El reporte señala los gaps; el usuario decide cuáles
son reales y cuáles cambio de nombre.
"""
import re
from pathlib import Path

REPO   = Path(__file__).resolve().parent.parent
THESIS = REPO.parent / "SciNodes-thesis"
MDBOOK = REPO / "doc/manual/src"

def color(s, code): return f"\033[{code}m{s}\033[0m"
def ok(s):    return color("✓", "32") + " " + s
def err(s):   return color("✗", "31") + " " + s
def warn(s):  return color("·", "33") + " " + s

# ---------- Parse CHANGELOG -------------------------------------------------

RE_TAG = re.compile(r'^## (v\d+\.\d+\.\d+) — (.+)$', re.MULTILINE)
RE_BULLET = re.compile(r'^- \*\*([^*]+)\*\*', re.MULTILINE)

def parse_changelog():
    """Devuelve dict {tag: [feature_str, ...]} en orden del CHANGELOG."""
    txt = (REPO / "CHANGELOG.md").read_text()
    tags = list(RE_TAG.finditer(txt))
    out = {}
    for i, m in enumerate(tags):
        tag = m.group(1)
        start = m.end()
        end = tags[i+1].start() if i+1 < len(tags) else len(txt)
        section = txt[start:end]
        features = [b.group(1).strip() for b in RE_BULLET.finditer(section)]
        out[tag] = features
    return out

# ---------- Normalización + búsqueda ---------------------------------------

# Extrae "tokens" significativos de un feature string para hacer grep.
# Estrategia: lo que esté entre `backticks` es un identificador técnico
# (clase, tipo, función) — prioridad alta. Las palabras restantes se
# usan como fallback.
RE_BACKTICK = re.compile(r'`([^`]+)`')

def normalize_token(raw):
    """Aplana un token complejo (path, brace expansion, separadores)
    en una lista de tokens atómicos buscables.

    Ejemplos:
      'src/app/Vulkan3DRenderer.{cpp,hpp}' -> ['Vulkan3DRenderer']
      'Canvas/INodeRenderer'                -> ['Canvas', 'INodeRenderer']
      'A + B'                                -> ['A', 'B']
      'I18n::tr(key)'                        -> ['I18n', 'tr']
      'NodeKind'                             -> ['NodeKind']
      'scinodes_units'                       -> ['scinodes_units']
    """
    t = raw.strip()

    # Paths del estilo src/X/Y.{cpp,hpp} -> Y
    m = re.match(r'(?:[\w]+/)+(\w+)\.\{?\w+', t)
    if m:
        return [m.group(1)]
    # Paths del estilo src/X/Y.cpp -> Y
    m = re.match(r'(?:[\w]+/)+(\w+)\.\w+$', t)
    if m:
        return [m.group(1)]

    # Llamada de función Foo::bar(...) -> [Foo, bar]
    m = re.match(r'(\w+)::(\w+)', t)
    if m:
        return [m.group(1), m.group(2)]

    # Compuestos con separadores típicos
    parts = re.split(r'\s*[+/]\s*', t)
    if len(parts) > 1:
        result = []
        for p in parts:
            result.extend(normalize_token(p))
        return result

    # Quitar paréntesis con args: foo(bar) -> foo
    m = re.match(r'(\w+)\(.*\)', t)
    if m:
        return [m.group(1)]

    # Default: si tiene espacios o caracteres raros lo retorna como sale
    return [t] if t else []

def keys_of(feature):
    """Devuelve lista de tokens-grep ordenada por especificidad."""
    bts = RE_BACKTICK.findall(feature)
    out = []
    for raw in bts:
        out.extend(normalize_token(raw))
    return [t for t in out if t]

def all_files(root, exts):
    return [p for p in root.rglob("*") if p.suffix in exts and p.is_file()]

def grep(token, files):
    """¿Aparece literal el token en alguno de los files? Devuelve los paths."""
    if not token:
        return []
    hits = []
    for f in files:
        try:
            txt = f.read_text(errors='ignore')
        except Exception:
            continue
        if token in txt:
            hits.append(f)
    return hits

# ---------- Audit ----------------------------------------------------------

def main():
    print(color("━━━ Auditoría Capa 6: features del CHANGELOG ↔ doc ━━━", "1;36"))
    print()
    changelog = parse_changelog()
    total_features = sum(len(v) for v in changelog.values())
    print(f"  CHANGELOG : {len(changelog)} tags, {total_features} features bulleted")

    md_files = all_files(MDBOOK, {'.md'})
    tex_files = all_files(THESIS / "chapters", {'.tex'}) if THESIS.exists() else []
    print(f"  mdBook    : {len(md_files)} archivos .md")
    print(f"  Tesis     : {len(tex_files)} archivos .tex (en {THESIS})")
    print()

    overall_ok = 0
    overall_mdbook_only = 0
    overall_thesis_only = 0
    overall_gap = 0
    no_tokens_count = 0
    gaps_by_tag = {}

    for tag in sorted(changelog.keys()):
        features = changelog[tag]
        tag_ok = []
        tag_md = []
        tag_th = []
        tag_gap = []
        tag_no_tokens = []
        for feature in features:
            tokens = keys_of(feature)
            if not tokens:
                tag_no_tokens.append(feature)
                no_tokens_count += 1
                continue
            # ¿alguno de los tokens aparece en mdBook?
            md_hit = any(grep(t, md_files) for t in tokens)
            th_hit = any(grep(t, tex_files) for t in tokens)
            if md_hit and th_hit:
                tag_ok.append(feature)
            elif md_hit:
                tag_md.append(feature)
            elif th_hit:
                tag_th.append(feature)
            else:
                tag_gap.append((feature, tokens))

        overall_ok += len(tag_ok)
        overall_mdbook_only += len(tag_md)
        overall_thesis_only += len(tag_th)
        overall_gap += len(tag_gap)
        gaps_by_tag[tag] = tag_gap

        total_t = len(features)
        cov_pct = 100 * (len(tag_ok) + len(tag_md) + len(tag_th)) / total_t if total_t else 0
        status = color("✓", "32") if cov_pct == 100 else (
                 color("·", "33") if cov_pct >= 80 else color("✗", "31"))
        print(f"  {status} {tag}: "
              f"OK={len(tag_ok):2d}  md-only={len(tag_md):2d}  "
              f"th-only={len(tag_th):2d}  GAP={len(tag_gap):2d}  "
              f"{cov_pct:5.1f}% ({total_t} features)")

    print()
    total_doc = total_features - no_tokens_count
    pct = 100 * (overall_ok + overall_mdbook_only + overall_thesis_only) / total_doc if total_doc else 0
    print(color("━━━ Resumen ━━━", "1;36"))
    print(f"  Total con tokens identificables : {total_doc} / {total_features}")
    print(f"  Sin tokens (prose pura, omitidos): {no_tokens_count}")
    print(f"  En mdBook + tesis (ideal)        : {overall_ok}")
    print(f"  Solo mdBook                       : {overall_mdbook_only}")
    print(f"  Solo tesis                        : {overall_thesis_only}")
    print(f"  GAP (sin doc en ningún lado)     : {overall_gap}")
    print(f"  Cobertura                          : {pct:.1f}%")

    if overall_gap:
        print()
        print(color("━━━ Gaps detallados ━━━", "1;36"))
        for tag in sorted(gaps_by_tag):
            gaps = gaps_by_tag[tag]
            if not gaps: continue
            print(f"\n  {color(tag, '1;33')}")
            for feature, tokens in gaps[:15]:
                tk = ', '.join(f"`{t}`" for t in tokens[:3])
                print(f"    ✗ {feature[:70]}")
                print(f"      tokens buscados: {tk}")
            if len(gaps) > 15:
                print(f"    ... ({len(gaps)-15} más)")

    # Falla solo ante GAP real (feature sin doc en ningún lado);
    # "solo mdBook" es tolerado (es el estado deseado tras la dedup).
    if overall_gap:
        raise SystemExit(1)

if __name__ == '__main__':
    main()
