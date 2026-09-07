#!/usr/bin/env python3
"""Optional independent SDK validation; TexUtil itself does not need Python/MaterialX."""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

import MaterialX as mx
from MaterialX import PyMaterialXGenGlsl as gl
from MaterialX import PyMaterialXGenShader as gs


def validate(path, library):
    # Resolve filenames from the document's directory, as a moved material bundle will.
    tree = ET.parse(path)
    for value in tree.findall(".//input[@type='filename']"):
        relative = Path(value.attrib["value"])
        if relative.is_absolute() or not (path.parent / relative).is_file():
            raise RuntimeError(f"{path}: missing or nonrelative texture {relative}")
    document = mx.createDocument()
    mx.readFromXmlFile(document, str(path))
    document.importLibrary(library)
    valid, message = document.validate()
    if not valid:
        raise RuntimeError(f"{path}: {message}")
    generator = gl.GlslShaderGenerator.create()
    cms = gs.DefaultColorManagementSystem.create(generator.getTarget())
    cms.loadLibrary(library)
    generator.setColorManagementSystem(cms)
    context = gs.GenContext(generator)
    context.registerSourceCodeSearchPath(mx.getDefaultDataSearchPath())
    materials = document.getMaterialNodes()
    if not materials:
        raise RuntimeError(f"{path}: no materials")
    for material in materials:
        shader = generator.generate("ValidationShader", material, context)
        if not shader.getSourceCode(gs.PIXEL_STAGE):
            raise RuntimeError(f"{path}: empty generated shader")
    print(f"PASS {path}: SDK validation, texture paths, GLSL generation")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path, help=".mtlx files or directories to search recursively")
    args = parser.parse_args()
    library = mx.createDocument()
    mx.loadLibraries(mx.getDefaultDataLibraryFolders(), mx.getDefaultDataSearchPath(), library)
    files = sorted({f for p in args.paths for f in (p.rglob("*.mtlx") if p.is_dir() else [p])})
    if not files:
        parser.error("no MaterialX documents found")
    print(f"MaterialX SDK {mx.__version__}")
    for path in files:
        validate(path, library)


if __name__ == "__main__":
    main()
