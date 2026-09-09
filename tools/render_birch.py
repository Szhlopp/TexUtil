#!/usr/bin/env python3
"""Optional Blender preview: one birch log using the TexUtil sample's maps.

Run after rendering samples/birch.json:
blender --background --factory-startup --python tools/render_birch.py -- --out out/birch
All scene construction happens in this separate Blender process.
"""
import argparse
from array import array
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def point_at(obj, target):
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()


def texture(nodes, filename, data=False):
    node = nodes.new('ShaderNodeTexImage')
    node.image = bpy.data.images.load(str(filename), check_existing=True)
    if data:
        node.image.colorspace_settings.name = 'Non-Color'
    return node


def material(name, out, prefix, roughness_file=None):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    shader = nodes.get('Principled BSDF')
    shader.inputs['Metallic'].default_value = 0
    shader.inputs['IOR'].default_value = 1.5
    shader.inputs['Roughness'].default_value = 0.74
    color = texture(nodes, out / (prefix + '-color.png'))
    links.new(color.outputs['Color'], shader.inputs['Base Color'])
    if roughness_file:
        roughness = texture(nodes, out / roughness_file, True)
        links.new(roughness.outputs['Color'], shader.inputs['Roughness'])
    normal = texture(nodes, out / (prefix + '-normal.png'), True)
    # TexUtil exports DirectX normals; Blender uses positive-Y tangent normals.
    flip = nodes.new('ShaderNodeVectorMath')
    flip.operation = 'MULTIPLY_ADD'
    flip.inputs[1].default_value = (1, -1, 1)
    flip.inputs[2].default_value = (0, 1, 0)
    links.new(normal.outputs['Color'], flip.inputs[0])
    decode = nodes.new('ShaderNodeNormalMap')
    links.new(flip.outputs['Vector'], decode.inputs['Color'])
    links.new(decode.outputs['Normal'], shader.inputs['Normal'])
    return mat


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('out/birch'))
    parser.add_argument('--hdr', type=Path, help='Optional HDR environment; defaults to Blender studio.exr.')
    parser.add_argument('--samples', type=int, default=64)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    out = args.out.resolve()
    for name in ['birch-color.png', 'birch-normal.png', 'birch-roughness.png', 'birch-profile.png', 'birch-end-color.png', 'birch-end-normal.png']:
        if not (out / name).is_file():
            raise FileNotFoundError(f'Render samples/birch.json first: missing {out / name}')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bark = material('Birch / papery bark', out, 'birch', 'birch-roughness.png')
    end = material('Birch / cut end', out, 'birch-end')

    # A single closed log, about 9 cm across and 78 cm long. U wraps once around
    # the trunk; V follows its length. Caps have their own radial UVs/material.
    segments, rows = 192, 96
    profile = bpy.data.images.load(str(out / 'birch-profile.png'))
    profile.colorspace_settings.name = 'Non-Color'
    pixels = array('f', [0.0]) * len(profile.pixels)
    profile.pixels.foreach_get(pixels)
    width, height = profile.size
    vertices, faces, uvs, material_indices = [], [], [], []
    for row in range(rows + 1):
        v = row / rows
        z = v * 0.78
        center_x = 0.007 * math.sin(v * 4.1) + 0.006 * v
        center_y = 0.003 * math.sin(v * 6)
        for col in range(segments):
            angle = col / segments * math.tau
            radius = 0.045 * (1 - 0.09 * v)
            radius *= 1 + 0.035 * math.sin(angle * 5 + v * 3) + 0.018 * math.sin(angle * 9 - v * 8)
            # Broad healed-scar swelling is separate from the fine normal relief.
            x, y = int(col / segments * (width - 1)), int(v * (height - 1))
            radius += 0.003 * pixels[(y * width + x) * 4]
            vertices.append((center_x + radius * math.cos(angle), center_y + radius * math.sin(angle), z))
    for row in range(rows):
        for col in range(segments):
            nxt = (col + 1) % segments
            faces.append((row * segments + col, row * segments + nxt, (row + 1) * segments + nxt, (row + 1) * segments + col))
            u0, u1, v0, v1 = col / segments, (col + 1) / segments, row / rows, (row + 1) / rows
            uvs.append(((u0, v0), (u1, v0), (u1, v1), (u0, v1)))
            material_indices.append(0)
    for row, reverse in [(0, True), (rows, False)]:
        indices = list(range(row * segments, (row + 1) * segments))
        if reverse:
            indices.reverse()
        faces.append(indices)
        uvs.append([(0.5 + 0.49 * math.cos((i % segments) / segments * math.tau), 0.5 + 0.49 * math.sin((i % segments) / segments * math.tau)) for i in indices])
        material_indices.append(1)
    mesh = bpy.data.meshes.new('Single log / cylindrical UVs')
    mesh.from_pydata(vertices, [], faces)
    mesh.materials.append(bark)
    mesh.materials.append(end)
    mesh.update()
    log = bpy.data.objects.new('Single birch log', mesh)
    bpy.context.collection.objects.link(log)
    layer = mesh.uv_layers.new(name='UVMap')
    for polygon, coords, index in zip(mesh.polygons, uvs, material_indices):
        polygon.material_index = index
        polygon.use_smooth = index == 0
        for loop, uv in zip(polygon.loop_indices, coords):
            layer.data[loop].uv = uv
    bevel = log.modifiers.new('Soft cut rim', 'BEVEL')
    bevel.width = 0.001
    bevel.segments = 3

    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.001))
    ground = bpy.context.object
    ground.name = 'Neutral studio floor'
    mat = bpy.data.materials.new('Warm gray studio')
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (0.17, 0.19, 0.18, 1)
    shader.inputs['Roughness'].default_value = 0.85
    ground.data.materials.append(mat)

    scene = bpy.context.scene
    scene.world = bpy.data.worlds.new('HDR studio environment')
    scene.world.use_nodes = True
    nodes, links = scene.world.node_tree.nodes, scene.world.node_tree.links
    hdr = args.hdr
    if hdr is None:
        hdr = Path(bpy.utils.system_resource('DATAFILES')) / 'studiolights/world/studio.exr'
    if not hdr.is_file():
        raise FileNotFoundError(f'Provide --hdr /path/to/environment.exr; not found: {hdr}')
    environment = nodes.new('ShaderNodeTexEnvironment')
    environment.image = bpy.data.images.load(str(hdr.resolve()))
    links.new(environment.outputs['Color'], nodes.get('Background').inputs['Color'])
    nodes.get('Background').inputs['Strength'].default_value = 0.45
    for name, pos, energy, size in [('Key softbox', (0.0, -1.2, 1.4), 85, 1.0), ('Rim softbox', (-0.8, 0.3, 1.0), 50, 0.7)]:
        data = bpy.data.lights.new(name, 'AREA')
        data.energy, data.shape, data.size = energy, 'DISK', size
        light = bpy.data.objects.new(name, data)
        bpy.context.collection.objects.link(light)
        light.location = pos
        point_at(light, (0, 0, 0.4))
    bpy.ops.object.camera_add(location=(0.90, -1.5, 1.10))
    scene.camera = bpy.context.object
    scene.camera.data.type = 'ORTHO'
    scene.camera.data.ortho_scale = 0.93
    point_at(scene.camera, (0, 0, 0.40))
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = args.samples
    scene.cycles.use_denoising = True
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 8
    scene.render.resolution_x, scene.render.resolution_y = 840, 1200
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.exposure = 0
    scene.render.image_settings.file_format = 'PNG'
    scene.render.filepath = str(out / 'birch-log.png')
    bpy.ops.wm.save_as_mainfile(filepath=str(out / 'birch-log.blend'))
    bpy.ops.render.render(write_still=True)
    print(f'Rendered one birch log with HDR {hdr}: {out / "birch-log.png"}')


if __name__ == '__main__':
    main()
