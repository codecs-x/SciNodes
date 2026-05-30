# Examples

Fixtures de prueba y demos del proyecto. Los referencian:

- `doc/test_manual.md` para los casos de prueba manuales.
- `doc/authoring-guide.md` para la guía de autoría de assets 3D.

## Estructura

```
examples/
├── dc_motor/                  # asset 3D del motor DC
│   ├── dc_motor.gltf          # versión autoreada en Blender, con
│   │                          # `extras.scinodes` por nodo (camino legacy).
│   ├── dc_motor.bin           # buffer binario referenciado.
│   ├── dc_motor_raw.gltf      # misma geometría SIN metadata scinodes
│   │                          # (camino in-app: el sidecar JSON se
│   │                          # autorea desde el panel de SciNodes).
│   └── dc_motor_raw.bin       # buffer binario referenciado.
├── custom_nodes/
│   └── quadratic.json         # descriptor de un nodo personalizado
│                              # (y = a·u² + b·u + c), referencia del
│                              # Caso F del test manual.
└── graphs/
    └── walkthrough_pid.scn    # grafo de demo: lazo PID + DC motor.
```

## Sidecars de mapping

Cuando ejecutas el flujo de **autoreado in-app** sobre
`dc_motor_raw.gltf`, SciNodes escribe un sidecar
`dc_motor_raw.mapping.json` al lado del archivo. Esos archivos se
generan, no se versionan — `.gitignore` los excluye. Si quieres
empezar limpio antes de un test, simplemente bórralos:

```bash
rm -f examples/dc_motor/*.mapping.json
```

## Regenerar `dc_motor_raw.gltf` desde `dc_motor.gltf`

Si por alguna razón se desincronizan (por ejemplo, modificas el
`dc_motor.gltf` desde Blender), el raw se reconstruye con un
script Python que copia la geometría y descarta las propiedades
`scinodes.*`:

```bash
cd examples/dc_motor
python3 << 'EOF'
import json, shutil, os
with open('dc_motor.gltf') as f: d = json.load(f)
for n in d.get('nodes', []):
    if 'extras' in n:
        new = {k:v for k,v in n['extras'].items() if not k.startswith('scinodes')}
        if new: n['extras'] = new
        else: del n['extras']
shutil.copy2('dc_motor.bin', 'dc_motor_raw.bin')
for b in d.get('buffers', []):
    if b.get('uri', '').endswith('.bin'): b['uri'] = 'dc_motor_raw.bin'
with open('dc_motor_raw.gltf', 'w') as f: json.dump(d, f, indent=2)
print('dc_motor_raw.gltf regenerado.')
EOF
```

## Regenerar `dc_motor.gltf` desde cero

El asset autoreado en Blender se genera con
`tools/blender/setup_dc_motor.py`:

1. Abre Blender (cualquier versión 3.0+).
2. Ve al workspace **Scripting**.
3. Pega el contenido del script en el Text Editor.
4. **Alt+P** para ejecutar (limpia la escena y crea los cinco
   objetos).
5. **File → Export → glTF 2.0**, marca **Include → Custom
   Properties**, formato glTF Separate.
6. Guarda como `examples/dc_motor/dc_motor.gltf`.

Detalles en `doc/authoring-guide.md`.
