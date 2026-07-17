"""Regenera data.embed.js a partir de data.json.

Uso: python build.py
Ejecutar cada vez que se edite data.json, para que index.html (abierto con
file://) tenga los datos actualizados sin depender de fetch().
"""
import json
import pathlib

here = pathlib.Path(__file__).parent
data = json.loads((here / "data.json").read_text(encoding="utf-8"))

with open(here / "data.embed.js", "w", encoding="utf-8") as f:
    f.write("// GENERADO AUTOMATICAMENTE desde data.json. No editar a mano: ejecutar build.py despues de editar data.json.\n")
    f.write("const SYNTH_DIAG_DATA = ")
    json.dump(data, f, ensure_ascii=False, indent=2)
    f.write(";\n")

print("data.embed.js actualizado.")
