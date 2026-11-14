#!/usr/bin/env python3
"""
tools/cooker/cook_bird.py

Cooker para ingerir BirdOrange.fbx y BirdOrange_BaseColor.png
hacia los formatos binarios nativos de Nintendo Wii U:
1. assets/models/bird_orange.wmesh (Malla esquelética con stride de 80 bytes)
2. assets/models/bird_orange.wskel (Jerarquía de 21 articulaciones con ordenamiento topológico)
3. assets/models/bird_orange.wanim (Clips de animación con selector: Salto, Piar, Mirar, Completa)
4. assets/models/bird_orange.wtex (Textura optimizada RGBA8 con pirámide de mipmaps)
"""

import os
import sys
import math
import struct
from typing import List, Tuple, Dict, Any
from PIL import Image

# Importar utilidades locales
sys.path.insert(0, os.path.dirname(__file__))
from fbx_parser import read_fbx_binary_nodes
from mesh_cooker import align_up, compute_aabb, compute_bounding_sphere
from generate_skinned_character import (
    mat4_identity, mat4_mul, mat4_invert_affine,
    quat_identity, quat_from_axis_angle,
    WMESH_MAGIC, WSKL_MAGIC, WANM_MAGIC,
    MESH_FLAG_SKINNED, MESH_FLAG_HAS_COLOR, MESH_FLAG_HAS_TEXCOORD,
    MESH_FLAG_HAS_NORMAL, MESH_FLAG_BIG_ENDIAN, SKINNED_VERTEX_STRIDE
)

FBX_TIME_SECOND = 46186158000.0

# -----------------------------------------------------------------------------
# Jerarquía Topológica Canónica de 21 Articulaciones (<= 24 para registros KC2)
# -----------------------------------------------------------------------------
CANONICAL_BONES = [
    ("Root_M",         -1),
    ("Spine1_M",        0),
    ("Chest_M",         1),
    ("Neck_M",          2),
    ("Head_M",          3),
    ("Jaw_M",           4),
    ("Eye_L",           4),
    ("Eye_R",           4),
    ("Hip_L",           0),
    ("Knee_L",          8),
    ("Ankle_L",         9),
    ("Hip_R",           0),
    ("Knee_R",         11),
    ("Ankle_R",        12),
    ("Tail1_M",         0),
    ("Tail2_M",        14),
    ("Tail3_M",        15),
    ("Tail3ASide_L",   15),
    ("Tail3ASide_R",   15),
    ("Tail3ASide1_L",  15),
    ("Tail3ASide1_R",  15),
]

# Mapeo de reducción de sub-articulaciones y extremos hacia los 21 huesos canónicos
BONE_REMAP = {
    # Pata izquierda
    "ToesA_L": "Ankle_L", "ToesAEnd_L": "Ankle_L",
    "ToesB_L": "Ankle_L", "ToesEnd_L": "Ankle_L",
    "ToesC_L": "Ankle_L", "ToesCEnd_L": "Ankle_L",
    # Pata derecha
    "ToesA_R": "Ankle_R", "ToesAEnd_R": "Ankle_R",
    "ToesB_R": "Ankle_R", "ToesEnd_R": "Ankle_R",
    "ToesC_R": "Ankle_R", "ToesCEnd_R": "Ankle_R",
    # Cabeza y cara
    "HeadEnd_M": "Head_M", "JawEnd_M": "Jaw_M",
    "EyeEnd_L": "Eye_L", "EyeEnd_R": "Eye_R",
    # Cola
    "Tail3End_M": "Tail3_M",
    "Tail3SideEnd_L": "Tail3ASide_L", "Tail3SideEnd_R": "Tail3ASide_R",
    "Tail3SideEnd1_L": "Tail3ASide1_L", "Tail3SideEnd1_R": "Tail3ASide1_R",
}

def quat_mul(q1, q2):
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return (
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2
    )

def euler_to_quat(rx_deg, ry_deg, rz_deg, order=0):
    rx = math.radians(rx_deg)
    ry = math.radians(ry_deg)
    rz = math.radians(rz_deg)
    qx = quat_from_axis_angle((1.0, 0.0, 0.0), rx)
    qy = quat_from_axis_angle((0.0, 1.0, 0.0), ry)
    qz = quat_from_axis_angle((0.0, 0.0, 1.0), rz)

    # FBX RotationOrder enums:
    if order == 0:   # eEulerXYZ
        return quat_mul(qz, quat_mul(qy, qx))
    elif order == 1: # eEulerXZY
        return quat_mul(qy, quat_mul(qz, qx))
    elif order == 2: # eEulerYZX
        return quat_mul(qx, quat_mul(qz, qy))
    elif order == 3: # eEulerYXZ
        return quat_mul(qz, quat_mul(qx, qy))
    elif order == 4: # eEulerZXY
        return quat_mul(qy, quat_mul(qx, qz))
    elif order == 5: # eEulerZYX
        return quat_mul(qx, quat_mul(qy, qz))
    else:
        return quat_mul(qz, quat_mul(qy, qx))

def mat3_to_quat(m):
    # m es matriz 4x4 column-major
    m00, m10, m20 = m[0], m[1], m[2]
    m01, m11, m21 = m[4], m[5], m[6]
    m02, m12, m22 = m[8], m[9], m[10]

    tr = m00 + m11 + m22
    if tr > 0.0:
        s = math.sqrt(tr + 1.0) * 2.0
        qw = 0.25 * s
        qx = (m21 - m12) / s
        qy = (m02 - m20) / s
        qz = (m10 - m01) / s
    elif (m00 > m11) and (m00 > m22):
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2.0
        qw = (m21 - m12) / s
        qx = 0.25 * s
        qy = (m01 + m10) / s
        qz = (m02 + m20) / s
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2.0
        qw = (m02 - m20) / s
        qx = (m01 + m10) / s
        qy = 0.25 * s
        qz = (m12 + m21) / s
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2.0
        qw = (m10 - m01) / s
        qx = (m02 + m20) / s
        qy = (m12 + m21) / s
        qz = 0.25 * s

    l = math.sqrt(qx*qx + qy*qy + qz*qz + qw*qw)
    if l < 1e-6:
        return (0.0, 0.0, 0.0, 1.0)
    return (qx/l, qy/l, qz/l, qw/l)

def cook_bird():
    fbx_path = "models/bird-orange/source/BirdOrange.fbx"
    tex_path = "models/bird-orange/textures/BirdOrange_BaseColor.png"

    print("=== INICIANDO COOKER DE BIRD-ORANGE (FBX -> WII U) ===")
    nodes = read_fbx_binary_nodes(fbx_path)
    objects = [n for n in nodes if n.name == 'Objects'][0]
    connections = [n for n in nodes if n.name == 'Connections'][0]

    # Cargar textura para muestreo de color de vértices
    tex_img = Image.open(tex_path).convert('RGB')
    tex_w, tex_h = tex_img.size
    tex_pixels = tex_img.load()
    print(f"Textura cargada: {tex_w}x{tex_h}")

    # 1. Mapear Modelos, Deformers y Animaciones
    models: Dict[int, Tuple[str, Any]] = {}
    for obj in objects.children:
        if obj.name == 'Model':
            models[obj.props[0]] = (obj.props[1].split('\x00')[0], obj)

    clusters: Dict[int, Any] = {}
    for obj in objects.children:
        if obj.name == 'Deformer' and len(obj.props) > 2 and obj.props[2] == 'Cluster':
            clusters[obj.props[0]] = obj

    curves = {obj.props[0]: obj for obj in objects.children if obj.name == 'AnimationCurve'}
    curve_nodes = {obj.props[0]: obj for obj in objects.children if obj.name == 'AnimationCurveNode'}

    # Conexiones
    cluster_to_bone: Dict[int, str] = {}
    bone_to_cluster: Dict[str, int] = {}
    curve_to_node: Dict[Tuple[int, str], int] = {}
    node_to_model: Dict[Tuple[int, str], int] = {}

    for c in connections.children:
        if c.name == 'C' and len(c.props) >= 3 and c.props[0] == 'OO':
            src, dst = c.props[1], c.props[2]
            if dst in clusters and src in models:
                bname = models[src][0]
                cluster_to_bone[dst] = bname
                bone_to_cluster[bname] = dst
        elif c.name == 'C' and len(c.props) >= 4:
            src, dst, prop = c.props[1], c.props[2], c.props[3]
            if src in curves and dst in curve_nodes:
                curve_to_node[(dst, prop)] = src
            elif src in curve_nodes and dst in models:
                node_to_model[(dst, prop)] = src

    # 2. Extraer Geometría de Malla
    mesh_geo = None
    for obj in objects.children:
        if obj.name == 'Geometry' and len(obj.props) > 2 and obj.props[2] == 'Mesh':
            mesh_geo = obj
            break

    if not mesh_geo:
        raise RuntimeError("No se encontró Geometry 'Mesh' en el archivo FBX.")

    raw_verts = mesh_geo.find_child('Vertices').props[0]
    pvi = mesh_geo.find_child('PolygonVertexIndex').props[0]

    raw_normals = None
    norm_node = mesh_geo.find_child('LayerElementNormal')
    if norm_node:
        norm_arr = norm_node.find_child('Normals')
        if norm_arr: raw_normals = norm_arr.props[0]

    raw_uvs = None
    uv_indices = None
    uv_node = mesh_geo.find_child('LayerElementUV')
    if uv_node:
        uv_arr = uv_node.find_child('UV')
        if uv_arr: raw_uvs = uv_arr.props[0]
        uvi_arr = uv_node.find_child('UVIndex')
        if uvi_arr: uv_indices = uvi_arr.props[0]

    num_control_points = len(raw_verts) // 3
    print(f"Puntos de control de malla: {num_control_points}")

    # 3. Recolectar Pesos por Vértice de Control
    cp_weights: List[List[Tuple[str, float]]] = [[] for _ in range(num_control_points)]
    for cid, cl in clusters.items():
        bone_name = cluster_to_bone.get(cid)
        if not bone_name: continue
        target_bone = BONE_REMAP.get(bone_name, bone_name)

        idxs = cl.find_child('Indexes')
        wts = cl.find_child('Weights')
        if not idxs or not wts or not idxs.props or not wts.props:
            continue

        c_idxs = idxs.props[0]
        c_wts = wts.props[0]
        for v_i, w in zip(c_idxs, c_wts):
            if w > 0.001 and v_i < num_control_points:
                cp_weights[v_i].append((target_bone, float(w)))

    bone_name_to_idx = {name: i for i, (name, _) in enumerate(CANONICAL_BONES)}

    cp_bone_indices: List[List[int]] = []
    cp_bone_weights: List[List[float]] = []

    for v_i, w_list in enumerate(cp_weights):
        consolidated: Dict[int, float] = {}
        for bname, weight in w_list:
            b_idx = bone_name_to_idx.get(bname)
            if b_idx is not None:
                consolidated[b_idx] = consolidated.get(b_idx, 0.0) + weight

        if not consolidated:
            consolidated[0] = 1.0

        sorted_weights = sorted(consolidated.items(), key=lambda x: -x[1])[:4]
        total_w = sum(w for _, w in sorted_weights)
        if total_w < 1e-6:
            sorted_weights = [(0, 1.0)]
            total_w = 1.0

        b_idxs = [b for b, _ in sorted_weights]
        b_wts = [w / total_w for _, w in sorted_weights]

        while len(b_idxs) < 4:
            b_idxs.append(0)
            b_wts.append(0.0)

        cp_bone_indices.append(b_idxs)
        cp_bone_weights.append(b_wts)

    # 4. Triangular Malla y Deduplicar Vértices
    class ProcessedVertex:
        __slots__ = ('pos', 'norm', 'uv', 'col', 'bones', 'weights')
        def __init__(self, pos, norm, uv, col, bones, weights):
            self.pos = pos
            self.norm = norm
            self.uv = uv
            self.col = col
            self.bones = bones
            self.weights = weights

    processed_verts: List[ProcessedVertex] = []
    indices: List[int] = []
    vert_map: Dict[Tuple, int] = {}

    curr_poly = []
    for k, idx in enumerate(pvi):
        is_end = (idx < 0)
        cp_idx = (~idx) if is_end else idx

        px = raw_verts[cp_idx * 3 + 0]
        py = raw_verts[cp_idx * 3 + 1]
        pz = raw_verts[cp_idx * 3 + 2]

        if raw_normals:
            nx = raw_normals[k * 3 + 0]
            ny = raw_normals[k * 3 + 1]
            nz = raw_normals[k * 3 + 2]
        else:
            nx, ny, nz = 0.0, 1.0, 0.0

        if raw_uvs:
            uv_i = uv_indices[k] if uv_indices else k
            u = raw_uvs[uv_i * 2 + 0]
            v = 1.0 - raw_uvs[uv_i * 2 + 1]
        else:
            u, v = 0.0, 0.0

        tx = max(0, min(tex_w - 1, int(u * tex_w)))
        ty = max(0, min(tex_h - 1, int(v * tex_h)))
        cr, cg, cb = tex_pixels[tx, ty]
        col = (cr / 255.0, cg / 255.0, cb / 255.0, 1.0)

        bones = cp_bone_indices[cp_idx]
        weights = cp_bone_weights[cp_idx]

        curr_poly.append((
            (px, py, pz),
            (nx, ny, nz),
            (u, v),
            col,
            tuple(bones),
            tuple(weights)
        ))

        if is_end:
            for i in range(1, len(curr_poly) - 1):
                for p_vert in (curr_poly[0], curr_poly[i], curr_poly[i + 1]):
                    pos_k = tuple(round(coord, 4) for coord in p_vert[0])
                    norm_k = tuple(round(coord, 3) for coord in p_vert[1])
                    uv_k = tuple(round(coord, 4) for coord in p_vert[2])
                    key = (pos_k, norm_k, uv_k, p_vert[4])

                    if key in vert_map:
                        indices.append(vert_map[key])
                    else:
                        new_idx = len(processed_verts)
                        vert_map[key] = new_idx
                        indices.append(new_idx)
                        processed_verts.append(ProcessedVertex(
                            p_vert[0], p_vert[1], p_vert[2], p_vert[3], p_vert[4], p_vert[5]
                        ))
            curr_poly = []

    vertex_count = len(processed_verts)
    index_count = len(indices)
    print(f"Malla procesada: {vertex_count} vértices, {index_count} índices ({index_count // 3} triángulos)")

    # 5. Calcular Matrices de Reposo (Skeleton & Bind Poses)
    skeleton_data = []
    bone_world_mats: Dict[str, List[float]] = {}
    bone_rot_orders: Dict[str, int] = {}

    for name, _ in CANONICAL_BONES:
        cid = bone_to_cluster.get(name)
        if cid and cid in clusters:
            tl = clusters[cid].find_child('TransformLink')
            if tl and tl.props:
                bone_world_mats[name] = list(tl.props[0])

        mid = [m for m, (n, _) in models.items() if n == name]
        if mid:
            p70 = models[mid[0]][1].find_child('Properties70')
            if p70:
                for p in p70.children:
                    if p.props[0] == 'RotationOrder':
                        bone_rot_orders[name] = p.props[4]

    for name, p_idx in CANONICAL_BONES:
        if name not in bone_world_mats:
            if p_idx >= 0:
                p_name = CANONICAL_BONES[p_idx][0]
                bone_world_mats[name] = list(bone_world_mats[p_name])
            else:
                bone_world_mats[name] = mat4_identity()

    for idx, (name, parent_idx) in enumerate(CANONICAL_BONES):
        m_world = bone_world_mats[name]
        inv_bind = mat4_invert_affine(m_world)

        if parent_idx < 0:
            local_bind = list(m_world)
        else:
            parent_name = CANONICAL_BONES[parent_idx][0]
            m_parent_world = bone_world_mats[parent_name]
            inv_parent = mat4_invert_affine(m_parent_world)
            local_bind = mat4_mul(inv_parent, m_world)

        # Extraer cuaternión y traslación exacta de reposo para preservar la forma anatómica canónica
        q_bind = mat3_to_quat(local_bind)
        t_bind = (local_bind[12], local_bind[13], local_bind[14])

        skeleton_data.append({
            'name': name,
            'parent': parent_idx,
            'world_bind': m_world,
            'inv_bind': inv_bind,
            'local_bind': local_bind,
            'q_bind': q_bind,
            't_bind': t_bind,
            'rot_order': bone_rot_orders.get(name, 0)
        })

    # 6. Muestrear Curvas de Animación en Múltiples Clips Selectables
    def sample_curve_val(cid, t_sec):
        if not cid or cid not in curves:
            return 0.0
        c = curves[cid]
        times = c.find_child('KeyTime').props[0]
        vals = c.find_child('KeyValueFloat').props[0]
        t_fbx = int(t_sec * FBX_TIME_SECOND)
        if t_fbx <= times[0]: return vals[0]
        if t_fbx >= times[-1]: return vals[-1]
        for i in range(1, len(times)):
            if times[i] >= t_fbx:
                dt = times[i] - times[i-1]
                factor = (t_fbx - times[i-1]) / dt if dt > 0 else 0.0
                return vals[i-1] + (vals[i] - vals[i-1]) * factor
        return vals[-1]

    # Clips canónicos (<= 64 keyframes para respetar MAX_KEYFRAMES_PER_TRACK en Wii U)
    CLIPS_DEF = [
        # (nombre_clip, start_sec, end_sec, num_kfs, is_looping)
        ("Salto",     0.00, 0.65, 21, True),   # 0.65s @ ~30fps: Salto limpio, flexión de rodillas y salto
        ("Piar",      0.65, 2.65, 41, True),   # 2.00s @ 20fps: Movimiento rítmico de pico y graznido
        ("Mirar",     2.65, 5.00, 48, True),   # 2.35s @ 20fps: Giro de cabeza y guiño de ojos
        ("Completa",  0.00, 5.00, 61, True),   # 5.00s @ 12fps: Secuencia FBX íntegra de 5 segundos
    ]

    cooked_clips = []

    for clip_name, start_t, end_t, num_kfs, is_looping in CLIPS_DEF:
        clip_duration = end_t - start_t
        anim_tracks = []

        for bone_info in skeleton_data:
            name = bone_info['name']
            mid = [m for m, (n, _) in models.items() if n == name]
            r_node_id = node_to_model.get((mid[0], 'Lcl Rotation')) if mid else None
            t_node_id = node_to_model.get((mid[0], 'Lcl Translation')) if mid else None

            q_bind = bone_info['q_bind']
            t_bind = bone_info['t_bind']
            rot_order = bone_info['rot_order']

            kfs = []
            for f in range(num_kfs):
                factor = f / (num_kfs - 1) if num_kfs > 1 else 0.0
                t_sample = start_t + factor * clip_duration
                t_clip = factor * clip_duration

                # Traslación: Root_M contiene la curva de salto vertical (hop) en el FBX
                if t_node_id and name == "Root_M":
                    tx = sample_curve_val(curve_to_node.get((t_node_id, 'd|X')), t_sample)
                    ty = sample_curve_val(curve_to_node.get((t_node_id, 'd|Y')), t_sample)
                    tz = sample_curve_val(curve_to_node.get((t_node_id, 'd|Z')), t_sample)
                    trans = (tx, ty, tz)
                else:
                    trans = t_bind

                # Rotación: Combinar orientación de reposo con delta de animación
                if r_node_id:
                    rx = sample_curve_val(curve_to_node.get((r_node_id, 'd|X')), t_sample)
                    ry = sample_curve_val(curve_to_node.get((r_node_id, 'd|Y')), t_sample)
                    rz = sample_curve_val(curve_to_node.get((r_node_id, 'd|Z')), t_sample)
                    q_delta = euler_to_quat(rx, ry, rz, rot_order)
                    rot = quat_mul(q_bind, q_delta)
                else:
                    rot = q_bind

                scale = (1.0, 1.0, 1.0)
                kfs.append({'time': t_clip, 'trans': trans, 'rot': rot, 'scale': scale})

            anim_tracks.append(kfs)

        cooked_clips.append({
            'name': clip_name,
            'duration': clip_duration,
            'fps': (num_kfs - 1) / clip_duration,
            'looping': is_looping,
            'tracks': anim_tracks
        })

    # 7. EXPORTAR ARCHIVOS BINARIOS
    os.makedirs("assets/models", exist_ok=True)
    os.makedirs("build/assets/models", exist_ok=True)
    os.makedirs("build/content/models", exist_ok=True)

    # 7.1 assets/models/bird_orange.wmesh
    wmesh_path = "assets/models/bird_orange.wmesh"
    vbo_size = vertex_count * SKINNED_VERTEX_STRIDE
    vbo_offset = 96
    vbo_offset_aligned = align_up(vbo_offset, 256)
    pad_pre_vbo = vbo_offset_aligned - 96

    ibo_offset = vbo_offset_aligned + vbo_size
    ibo_offset_aligned = align_up(ibo_offset, 32)
    pad_pre_ibo = ibo_offset_aligned - ibo_offset
    ibo_size = index_count * 2

    aabb_min, aabb_max = compute_aabb(processed_verts)
    cx, cy, cz, radius = compute_bounding_sphere(processed_verts, aabb_min, aabb_max)

    flags = MESH_FLAG_SKINNED | MESH_FLAG_HAS_COLOR | MESH_FLAG_HAS_TEXCOORD | MESH_FLAG_HAS_NORMAL | MESH_FLAG_BIG_ENDIAN
    header = struct.pack(
        '>4sHHIIIIIIII4f3f3fI3I',
        WMESH_MAGIC,
        1,
        flags,
        vertex_count,
        index_count,
        SKINNED_VERTEX_STRIDE,
        0, # Index format 0 = U16
        vbo_offset_aligned,
        vbo_size,
        ibo_offset_aligned,
        ibo_size,
        cx, cy, cz, radius,
        aabb_min[0], aabb_min[1], aabb_min[2],
        aabb_max[0], aabb_max[1], aabb_max[2],
        1, # Submesh count
        0, 0, 0
    )

    with open(wmesh_path, 'wb') as f:
        f.write(header)
        f.write(b'\x00' * pad_pre_vbo)
        for v in processed_verts:
            f.write(struct.pack(
                '>3f3f2f4f4B4f3f',
                v.pos[0], v.pos[1], v.pos[2],
                v.norm[0], v.norm[1], v.norm[2],
                v.uv[0], v.uv[1],
                v.col[0], v.col[1], v.col[2], v.col[3],
                v.bones[0], v.bones[1], v.bones[2], v.bones[3],
                v.weights[0], v.weights[1], v.weights[2], v.weights[3],
                0.0, 0.0, 0.0
            ))
        f.write(b'\x00' * pad_pre_ibo)
        for idx in indices:
            f.write(struct.pack('>H', idx))

    print(f"Exportado '{wmesh_path}': {vertex_count} verts ({vbo_size}B), {index_count} indices ({ibo_size}B)")

    # 7.2 assets/models/bird_orange.wskel (Header canónico 32 bytes)
    wskel_path = "assets/models/bird_orange.wskel"
    joint_count = len(skeleton_data)
    with open(wskel_path, 'wb') as f:
        f.write(struct.pack('>4sHHI20s', WSKL_MAGIC, 1, joint_count, 0, b'\x00'*20))
        for j in skeleton_data:
            name_bytes = j['name'].encode('ascii')[:31].ljust(32, b'\x00')
            f.write(struct.pack(
                '>32shh16f16f',
                name_bytes, j['parent'], 0,
                *j['inv_bind'], *j['local_bind']
            ))
    print(f"Exportado '{wskel_path}': {joint_count} articulaciones")

    # 7.3 assets/models/bird_orange.wanim (Header canónico 32 bytes con soporte multiclip)
    wanim_path = "assets/models/bird_orange.wanim"
    clip_count = len(cooked_clips)
    with open(wanim_path, 'wb') as f:
        f.write(struct.pack('>4sHHI20s', WANM_MAGIC, 1, clip_count, 0, b'\x00'*20))
        for clip in cooked_clips:
            c_name_bytes = clip['name'].encode('ascii')[:31].ljust(32, b'\x00')
            c_loop_val = 1 if clip['looping'] else 0
            f.write(struct.pack('>32sffII', c_name_bytes, clip['duration'], clip['fps'], c_loop_val, joint_count))
            for track in clip['tracks']:
                f.write(struct.pack('>I', len(track)))
                for kf in track:
                    f.write(struct.pack(
                        '>f3f4f3f',
                        kf['time'],
                        kf['trans'][0], kf['trans'][1], kf['trans'][2],
                        kf['rot'][0], kf['rot'][1], kf['rot'][2], kf['rot'][3],
                        kf['scale'][0], kf['scale'][1], kf['scale'][2]
                    ))
    print(f"Exportado '{wanim_path}': {clip_count} clips de animación:")
    for c in cooked_clips:
        print(f"  - Clip '{c['name']}': {c['duration']:.2f}s ({len(c['tracks'][0])} kfs, loop={c['looping']})")

    # Sincronizar en build/assets y build/content
    for fname in ["bird_orange.wmesh", "bird_orange.wskel", "bird_orange.wanim", "bird_orange.wtex"]:
        src = os.path.join("assets/models", fname)
        if os.path.exists(src):
            for dst_dir in ["build/assets/models", "build/content/models"]:
                dst = os.path.join(dst_dir, fname)
                with open(src, 'rb') as sf, open(dst, 'wb') as df:
                    df.write(sf.read())
    print("Sincronización de assets de BirdOrange completada con éxito.")

if __name__ == '__main__':
    cook_bird()
