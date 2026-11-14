#!/usr/bin/env python3
"""
tools/cooker/cook_model.py

Universal 3D Model & Animation Asset Cooker for Nintendo Wii U Engine.
Complies with Jason Gregory's Game Engine Architecture (3rd Ed., Chapters 11-14):
- Auto-detects Static Mesh (stride 48B) vs Skinned Mesh (stride 80B).
- Auto-discovers textures and generates .wtex with mipmaps and Big-Endian packaging.
- Supports Autodesk FBX 7.x binary and Wavefront OBJ formats.
- Enforces AMD Latte GX2 silicon limits (<= 24 joints for KC2 uniform registers).
- Employs exact rest-pose mathematics (Shepperd/Shoemake SO(3) quaternion extraction)
  and preserves canonical bind poses without geometric distortion.
- Supports multi-clip animation extraction (.wanim) with automatic or JSON-guided slicing.
- Synchronizes output assets to assets/models/, build/assets/models/, and build/content/models/.
"""

import os
import sys
import math
import struct
import json
import shutil
import argparse
from typing import List, Tuple, Dict, Any, Optional
from PIL import Image

try:
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
except Exception:
    pass

# Import local cooker utilities
sys.path.insert(0, os.path.dirname(__file__))
from fbx_parser import read_fbx_binary_nodes, extract_fbx_meshes
from mesh_cooker import (
    align_up, compute_aabb, compute_bounding_sphere,
    reorder_triangles_ptvc, cook_texture, cook_model as cook_static_model,
    Vertex as StaticVertex, WMESH_MAGIC, WMESH_VERSION,
    MESH_FLAG_STATIC, MESH_FLAG_SKINNED, MESH_FLAG_HAS_COLOR,
    MESH_FLAG_HAS_TEXCOORD, MESH_FLAG_HAS_NORMAL, MESH_FLAG_BIG_ENDIAN
)

# Constants
WSKL_MAGIC = b'WSKL'
WANM_MAGIC = b'WANM'
SKINNED_VERTEX_STRIDE = 80
MAX_LATTE_JOINTS = 24
MAX_KEYFRAMES_PER_TRACK = 64
FBX_TIME_SECOND = 46186158000.0

# -----------------------------------------------------------------------------
# Linear Algebra & Dual Quaternions / SO(3) Math
# -----------------------------------------------------------------------------
def mat4_identity() -> List[float]:
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    ]

def mat4_mul(a: List[float], b: List[float]) -> List[float]:
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            out[c*4 + r] = (
                a[0*4 + r] * b[c*4 + 0] +
                a[1*4 + r] * b[c*4 + 1] +
                a[2*4 + r] * b[c*4 + 2] +
                a[3*4 + r] * b[c*4 + 3]
            )
    return out

def mat4_invert_affine(m: List[float]) -> List[float]:
    inv = mat4_identity()
    # Transpose upper 3x3
    inv[0] = m[0]; inv[1] = m[4]; inv[2] = m[8]
    inv[4] = m[1]; inv[5] = m[5]; inv[6] = m[9]
    inv[8] = m[2]; inv[9] = m[6]; inv[10] = m[10]
    # -R^T * T
    inv[12] = -(inv[0]*m[12] + inv[4]*m[13] + inv[8]*m[14])
    inv[13] = -(inv[1]*m[12] + inv[5]*m[13] + inv[9]*m[14])
    inv[14] = -(inv[2]*m[12] + inv[6]*m[13] + inv[10]*m[14])
    return inv

def quat_identity() -> Tuple[float, float, float, float]:
    return (0.0, 0.0, 0.0, 1.0)

def quat_from_axis_angle(axis: Tuple[float, float, float], angle_rad: float) -> Tuple[float, float, float, float]:
    half = angle_rad * 0.5
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))

def quat_mul(q1: Tuple[float, float, float, float], q2: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return (
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2,
        w1*w2 - x1*x2 - y1*y2 - z1*z2
    )

def euler_to_quat(rx_deg: float, ry_deg: float, rz_deg: float, order: int = 0) -> Tuple[float, float, float, float]:
    rx = math.radians(rx_deg)
    ry = math.radians(ry_deg)
    rz = math.radians(rz_deg)
    qx = quat_from_axis_angle((1.0, 0.0, 0.0), rx)
    qy = quat_from_axis_angle((0.0, 1.0, 0.0), ry)
    qz = quat_from_axis_angle((0.0, 0.0, 1.0), rz)

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

def mat3_to_quat(m: List[float]) -> Tuple[float, float, float, float]:
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

# -----------------------------------------------------------------------------
# Skinned Vertex Class
# -----------------------------------------------------------------------------
class SkinnedVertex:
    __slots__ = ('pos', 'norm', 'uv', 'col', 'bones', 'weights')
    def __init__(self, pos, norm, uv, col, bones, weights):
        self.pos = list(pos)
        self.norm = list(norm)
        self.uv = list(uv)
        self.col = list(col)
        self.bones = list(bones)
        self.weights = list(weights)

# -----------------------------------------------------------------------------
# Universal Model Cooker Class
# -----------------------------------------------------------------------------
class UniversalModelCooker:
    def __init__(self, input_path: str, output_dir: str = "assets/models",
                 custom_name: Optional[str] = None, config_path: Optional[str] = None,
                 scale: float = 1.0, custom_tex: Optional[str] = None, sync: bool = True):
        self.input_path = os.path.abspath(input_path)
        self.output_dir = os.path.abspath(output_dir)
        self.scale = scale
        self.custom_tex = custom_tex
        self.sync = sync

        if not os.path.exists(self.input_path):
            raise FileNotFoundError(f"Archivo de modelo no encontrado: {self.input_path}")

        # Determinar nombre base del activo
        if custom_name:
            self.asset_name = custom_name
        else:
            base = os.path.splitext(os.path.basename(self.input_path))[0]
            # Convertir CamelCase o PascalCase a snake_case
            s1 = ''.join(['_' + c.lower() if c.isupper() else c.lower() for c in base]).lstrip('_')
            self.asset_name = s1.replace('-', '_')

        # Buscar archivo de configuración JSON acompañante
        self.config = {}
        if config_path and os.path.exists(config_path):
            with open(config_path, 'r', encoding='utf-8') as cf:
                self.config = json.load(cf)
                print(f"[COOKER] Configuración cargada desde: {config_path}")
        else:
            # Búsqueda automática en la misma carpeta o padre
            dir_name = os.path.dirname(self.input_path)
            candidates = [
                os.path.join(dir_name, f"{self.asset_name}.json"),
                os.path.join(dir_name, "model_config.json"),
                os.path.join(os.path.dirname(dir_name), f"{self.asset_name}.json"),
                os.path.join(os.path.dirname(dir_name), "model_config.json")
            ]
            for cand in candidates:
                if os.path.exists(cand):
                    with open(cand, 'r', encoding='utf-8') as cf:
                        self.config = json.load(cf)
                        print(f"[COOKER] Configuración auto-descubierta en: {cand}")
                    break

        if 'name' in self.config and not custom_name:
            self.asset_name = self.config['name']

        # Descubrimiento de textura
        self.tex_path = self.discover_texture()

    def discover_texture(self) -> Optional[str]:
        if self.custom_tex and os.path.exists(self.custom_tex):
            return self.custom_tex
        if 'texture' in self.config:
            t = self.config['texture']
            if not os.path.isabs(t):
                # Probar relativo al dir de entrada o al root del proyecto
                if os.path.exists(t): return os.path.abspath(t)
                cand = os.path.join(os.path.dirname(self.input_path), t)
                if os.path.exists(cand): return os.path.abspath(cand)
            elif os.path.exists(t):
                return t

        # Auto-búsqueda en subcarpeta 'textures/' o hermana
        model_dir = os.path.dirname(self.input_path)
        search_dirs = [
            model_dir,
            os.path.join(model_dir, "textures"),
            os.path.join(os.path.dirname(model_dir), "textures")
        ]
        valid_exts = ('.png', '.tga', '.jpg', '.jpeg')
        for sdir in search_dirs:
            if os.path.exists(sdir):
                # Preferir nombres con BaseColor, Diffuse, Albedo o el nombre del activo
                files = os.listdir(sdir)
                for f in files:
                    if f.lower().endswith(valid_exts) and any(k in f.lower() for k in ['basecolor', 'diffuse', 'albedo', self.asset_name]):
                        return os.path.abspath(os.path.join(sdir, f))
                for f in files:
                    if f.lower().endswith(valid_exts):
                        return os.path.abspath(os.path.join(sdir, f))
        return None

    def cook(self):
        print(f"\n=======================================================")
        print(f"[*] INICIANDO COOKER UNIVERSAL DE MODELOS WII U: '{self.asset_name}'")
        print(f"=======================================================")
        print(f"Entrada: {self.input_path}")
        print(f"Directorio de Salida: {self.output_dir}")
        if self.tex_path:
            print(f"Textura Detectada: {self.tex_path}")

        ext = os.path.splitext(self.input_path)[1].lower()
        if ext == '.obj':
            self.cook_obj()
        elif ext == '.fbx':
            self.cook_fbx()
        else:
            raise ValueError(f"Formato no soportado: '{ext}'. Use .fbx o .obj.")

        # Sincronización automática de activos
        if self.sync:
            self.sync_outputs()

    def sync_outputs(self):
        print(f"[COOKER] Sincronizando activos hacia build/assets y build/content...")
        os.makedirs("assets/models", exist_ok=True)
        os.makedirs("build/assets/models", exist_ok=True)
        os.makedirs("build/content/models", exist_ok=True)

        suffixes = [".wmesh", ".wskel", ".wanim", ".wtex"]
        for s in suffixes:
            src = os.path.join(self.output_dir, f"{self.asset_name}{s}")
            if os.path.exists(src):
                for dst_dir in ["build/assets/models", "build/content/models"]:
                    dst = os.path.join(dst_dir, os.path.basename(src))
                    shutil.copy2(src, dst)
                    print(f"  -> Sincronizado: {dst}")

    # -------------------------------------------------------------------------
    # Pipeline de Mallas Estáticas (.obj)
    # -------------------------------------------------------------------------
    def cook_obj(self):
        out_wmesh = os.path.join(self.output_dir, f"{self.asset_name}.wmesh")
        os.makedirs(self.output_dir, exist_ok=True)
        from mesh_cooker import parse_obj
        verts, indices = parse_obj(self.input_path)
        if self.scale != 1.0:
            for v in verts:
                v.pos[0] *= self.scale; v.pos[1] *= self.scale; v.pos[2] *= self.scale

        cook_static_model(verts, indices, out_wmesh, optimize_cache=True)
        if self.tex_path:
            out_wtex = os.path.join(self.output_dir, f"{self.asset_name}.wtex")
            cook_texture(self.tex_path, out_wtex)

    # -------------------------------------------------------------------------
    # Pipeline FBX: Detección Automática Skinned vs Static
    # -------------------------------------------------------------------------
    def cook_fbx(self):
        nodes = read_fbx_binary_nodes(self.input_path)
        objects = [n for n in nodes if n.name == 'Objects']
        if not objects:
            raise RuntimeError("FBX inválido: No se encontró sección 'Objects'.")
        objects_node = objects[0]

        # Comprobar si contiene Deformers de tipo Cluster (skinning)
        has_skinning = False
        for obj in objects_node.children:
            if obj.name == 'Deformer' and len(obj.props) > 2 and obj.props[2] == 'Cluster':
                has_skinning = True
                break

        if has_skinning:
            print("[COOKER] [SKEL] Malla Esquelética Articulada detectada (Clusters de Skinning presentes).")
            self.cook_fbx_skinned(nodes)
        else:
            print("[COOKER] [STATIC] Malla Estática detectada (Sin clusters de deformación esquelética).")
            self.cook_fbx_static()

    def cook_fbx_static(self):
        os.makedirs(self.output_dir, exist_ok=True)
        fbx_meshes = extract_fbx_meshes(self.input_path)
        if not fbx_meshes:
            raise RuntimeError("No se encontraron geometrías válidas en el archivo FBX.")

        if len(fbx_meshes) > 1 and 'house' in fbx_meshes:
            base_name = self.asset_name.replace('_props', '').replace('_alpha', '')
            for k, m in fbx_meshes.items():
                target_path = os.path.join(self.output_dir, f"{base_name}.wmesh" if k == 'house' else f"{base_name}_{k}.wmesh")
                v_list = [StaticVertex(v['pos'], v['norm'], v['uv'], v['col']) for v in m['vertices']]
                if self.scale != 1.0:
                    for v in v_list:
                        v.pos[0] *= self.scale; v.pos[1] *= self.scale; v.pos[2] *= self.scale
                cook_static_model(v_list, m['indices'], target_path, optimize_cache=True)

        out_wmesh = os.path.join(self.output_dir, f"{self.asset_name}_complete.wmesh" if (len(fbx_meshes) > 1 and 'house' in fbx_meshes) else f"{self.asset_name}.wmesh")
        all_verts = []
        all_indices = []
        for k, m in fbx_meshes.items():
            base_idx = len(all_verts)
            v_list = [StaticVertex(v['pos'], v['norm'], v['uv'], v['col']) for v in m['vertices']]
            all_verts.extend(v_list)
            all_indices.extend([i + base_idx for i in m['indices']])

        if self.scale != 1.0:
            for v in all_verts:
                v.pos[0] *= self.scale; v.pos[1] *= self.scale; v.pos[2] *= self.scale

        cook_static_model(all_verts, all_indices, out_wmesh, optimize_cache=True)
        if self.tex_path:
            out_wtex = os.path.join(self.output_dir, f"{self.asset_name}.wtex")
            cook_texture(self.tex_path, out_wtex, tex_format='bc1')

    # -------------------------------------------------------------------------
    # Pipeline FBX Skinned (Articulado con Huesos y Animación)
    # -------------------------------------------------------------------------
    def cook_fbx_skinned(self, nodes):
        objects = [n for n in nodes if n.name == 'Objects'][0]
        connections = [n for n in nodes if n.name == 'Connections'][0]

        # 1. Cargar Textura si existe para muestrear color de vértices
        tex_pixels = None
        tex_w, tex_h = 0, 0
        if self.tex_path and os.path.exists(self.tex_path):
            tex_img = Image.open(self.tex_path).convert('RGB')
            tex_w, tex_h = tex_img.size
            tex_pixels = tex_img.load()
            print(f"[COOKER] Textura cargada para muestreo de color: {tex_w}x{tex_h}")
            out_wtex = os.path.join(self.output_dir, f"{self.asset_name}.wtex")
            os.makedirs(self.output_dir, exist_ok=True)
            cook_texture(self.tex_path, out_wtex)

        # 2. Mapear Modelos, Clusters y Conexiones
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

        cluster_to_bone: Dict[int, str] = {}
        bone_to_cluster: Dict[str, int] = {}
        curve_to_node: Dict[Tuple[int, str], int] = {}
        node_to_model: Dict[Tuple[int, str], int] = {}
        model_hierarchy: Dict[str, str] = {} # bone -> parent_bone

        for c in connections.children:
            if c.name == 'C' and len(c.props) >= 3 and c.props[0] == 'OO':
                src, dst = c.props[1], c.props[2]
                if dst in clusters and src in models:
                    bname = models[src][0]
                    cluster_to_bone[dst] = bname
                    bone_to_cluster[bname] = dst
                elif src in models and dst in models:
                    model_hierarchy[models[src][0]] = models[dst][0]
            elif c.name == 'C' and len(c.props) >= 4:
                src, dst, prop = c.props[1], c.props[2], c.props[3]
                if src in curves and dst in curve_nodes:
                    curve_to_node[(dst, prop)] = src
                elif src in curve_nodes and dst in models:
                    node_to_model[(dst, prop)] = src

        # 3. Extraer Geometría
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
        print(f"[COOKER] Puntos de control de malla: {num_control_points}")

        # 4. Reducción y Construcción de Jerarquía Canónica de Huesos
        bone_remap = self.config.get('skeleton', {}).get('remap', {})
        max_joints = self.config.get('skeleton', {}).get('max_joints', MAX_LATTE_JOINTS)

        # Recopilar todos los huesos asociados a clusters
        active_bones = list(bone_to_cluster.keys())

        # Si no hay remap en config y se superan los huesos de Latte, podar automáticamente
        if len(active_bones) > max_joints and not bone_remap:
            print(f"[COOKER] [WARN] Huesos activos ({len(active_bones)}) exceden el presupuesto ({max_joints}). Aplicando poda heurística automática...")
            bone_remap = self.auto_cull_skeleton(active_bones, clusters, cluster_to_bone, model_hierarchy, max_joints)

        # Filtrar y ordenar topológicamente (padre antes que hijo)
        canonical_bones_set = set()
        for b in active_bones:
            target = bone_remap.get(b, b)
            canonical_bones_set.add(target)

        # Construir lista topológica
        ordered_bones = self.build_topological_hierarchy(canonical_bones_set, model_hierarchy, bone_remap)
        print(f"[COOKER] Jerarquía Canónica construida: {len(ordered_bones)} articulaciones (<= {max_joints})")
        for idx, (bname, pidx) in enumerate(ordered_bones):
            pname = ordered_bones[pidx][0] if pidx >= 0 else "None (Root)"
            print(f"  [{idx:02d}] {bname:20s} -> Padre: {pname}")

        bone_name_to_idx = {name: i for i, (name, _) in enumerate(ordered_bones)}

        # 5. Recolectar y Consolidar Pesos de Skinning
        cp_weights: List[List[Tuple[str, float]]] = [[] for _ in range(num_control_points)]
        for cid, cl in clusters.items():
            bone_name = cluster_to_bone.get(cid)
            if not bone_name: continue
            target_bone = bone_remap.get(bone_name, bone_name)

            idxs = cl.find_child('Indexes')
            wts = cl.find_child('Weights')
            if not idxs or not wts or not idxs.props or not wts.props:
                continue

            c_idxs = idxs.props[0]
            c_wts = wts.props[0]
            for v_i, w in zip(c_idxs, c_wts):
                if w > 0.001 and v_i < num_control_points:
                    cp_weights[v_i].append((target_bone, float(w)))

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

        # 6. Triangular Malla y Deduplicar Vértices
        processed_verts: List[SkinnedVertex] = []
        indices: List[int] = []
        vert_map: Dict[Tuple, int] = {}

        curr_poly = []
        for k, idx in enumerate(pvi):
            is_end = (idx < 0)
            cp_idx = (~idx) if is_end else idx

            px = raw_verts[cp_idx * 3 + 0] * self.scale
            py = raw_verts[cp_idx * 3 + 1] * self.scale
            pz = raw_verts[cp_idx * 3 + 2] * self.scale

            if raw_normals:
                nx = raw_normals[k * 3 + 0]
                ny = raw_normals[k * 3 + 1]
                nz = raw_normals[k * 3 + 2]
            else:
                nx, ny, nz = 0.0, 1.0, 0.0

            if raw_uvs:
                uv_i = uv_indices[k] if uv_indices else k
                u = raw_uvs[uv_i * 2 + 0]
                v = 1.0 - raw_uvs[uv_i * 2 + 1] # Flip V para OpenGL / GX2
            else:
                u, v = 0.0, 0.0

            if tex_pixels and tex_w > 0:
                tx = max(0, min(tex_w - 1, int(u * tex_w)))
                ty = max(0, min(tex_h - 1, int(v * tex_h)))
                cr, cg, cb = tex_pixels[tx, ty]
                col = (cr / 255.0, cg / 255.0, cb / 255.0, 1.0)
            else:
                col = (1.0, 1.0, 1.0, 1.0)

            bones = cp_bone_indices[cp_idx]
            weights = cp_bone_weights[cp_idx]

            curr_poly.append((
                (px, py, pz), (nx, ny, nz), (u, v), col, tuple(bones), tuple(weights)
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
                            processed_verts.append(SkinnedVertex(
                                p_vert[0], p_vert[1], p_vert[2], p_vert[3], p_vert[4], p_vert[5]
                            ))
                curr_poly = []

        vertex_count = len(processed_verts)
        index_count = len(indices)
        print(f"[COOKER] Malla procesada: {vertex_count} vértices, {index_count} índices ({index_count // 3} triángulos)")

        # 7. Matrices de Reposo (Skeleton & Bind Poses)
        skeleton_data = []
        bone_world_mats: Dict[str, List[float]] = {}
        bone_rot_orders: Dict[str, int] = {}

        for name, _ in ordered_bones:
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

        for name, p_idx in ordered_bones:
            if name not in bone_world_mats:
                if p_idx >= 0:
                    p_name = ordered_bones[p_idx][0]
                    bone_world_mats[name] = list(bone_world_mats[p_name])
                else:
                    bone_world_mats[name] = mat4_identity()

        for idx, (name, parent_idx) in enumerate(ordered_bones):
            m_world = bone_world_mats[name]
            inv_bind = mat4_invert_affine(m_world)

            if parent_idx < 0:
                local_bind = list(m_world)
            else:
                parent_name = ordered_bones[parent_idx][0]
                m_parent_world = bone_world_mats[parent_name]
                inv_parent = mat4_invert_affine(m_parent_world)
                local_bind = mat4_mul(inv_parent, m_world)

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

        # 8. Muestreo de Clips de Animación
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

        # Definición de clips: desde JSON o detectar rango total
        clips_config = self.config.get('clips', [])
        if not clips_config:
            # Detectar duración máxima en las curvas
            max_t = 1.0
            for c in curves.values():
                kt = c.find_child('KeyTime')
                if kt and kt.props:
                    last_t = kt.props[0][-1] / FBX_TIME_SECOND
                    if last_t > max_t: max_t = last_t
            clips_config = [{"name": "Default", "start": 0.0, "end": max_t, "kfs": min(61, MAX_KEYFRAMES_PER_TRACK), "loop": True}]

        cooked_clips = []
        for clip in clips_config:
            c_name = clip.get('name', 'Clip')
            start_t = float(clip.get('start', 0.0))
            end_t = float(clip.get('end', 1.0))
            num_kfs = int(clip.get('kfs', 61))
            num_kfs = min(num_kfs, MAX_KEYFRAMES_PER_TRACK)
            is_looping = bool(clip.get('loop', True))
            duration = end_t - start_t
            fps = (num_kfs - 1) / duration if duration > 0 else 30.0

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
                    t_sample = start_t + factor * duration
                    t_clip = factor * duration

                    # Traslación solo para huesos raíz (parent < 0) para preservar longitudes
                    if t_node_id and bone_info['parent'] < 0:
                        tx = sample_curve_val(curve_to_node.get((t_node_id, 'd|X')), t_sample) * self.scale
                        ty = sample_curve_val(curve_to_node.get((t_node_id, 'd|Y')), t_sample) * self.scale
                        tz = sample_curve_val(curve_to_node.get((t_node_id, 'd|Z')), t_sample) * self.scale
                        trans = (tx, ty, tz)
                    else:
                        trans = t_bind

                    if r_node_id:
                        rx = sample_curve_val(curve_to_node.get((r_node_id, 'd|X')), t_sample)
                        ry = sample_curve_val(curve_to_node.get((r_node_id, 'd|Y')), t_sample)
                        rz = sample_curve_val(curve_to_node.get((r_node_id, 'd|Z')), t_sample)
                        q_delta = euler_to_quat(rx, ry, rz, rot_order)
                        rot = quat_mul(q_bind, q_delta)
                    else:
                        rot = q_bind

                    kfs.append({'time': t_clip, 'trans': trans, 'rot': rot, 'scale': (1.0, 1.0, 1.0)})
                anim_tracks.append(kfs)

            cooked_clips.append({
                'name': c_name,
                'duration': duration,
                'fps': fps,
                'looping': is_looping,
                'tracks': anim_tracks
            })

        # 9. Exportar Archivos Binarios
        os.makedirs(self.output_dir, exist_ok=True)
        self.export_wmesh_skinned(processed_verts, indices)
        self.export_wskel(skeleton_data)
        self.export_wanim(cooked_clips, len(skeleton_data))

    def export_wmesh_skinned(self, verts: List[SkinnedVertex], indices: List[int]):
        wmesh_path = os.path.join(self.output_dir, f"{self.asset_name}.wmesh")
        vertex_count = len(verts)
        index_count = len(indices)
        vbo_size = vertex_count * SKINNED_VERTEX_STRIDE
        vbo_offset = 96
        vbo_offset_aligned = align_up(vbo_offset, 256)
        pad_pre_vbo = vbo_offset_aligned - 96

        ibo_offset = vbo_offset_aligned + vbo_size
        ibo_offset_aligned = align_up(ibo_offset, 32)
        pad_pre_ibo = ibo_offset_aligned - ibo_offset
        ibo_size = index_count * 2

        aabb_min, aabb_max = compute_aabb(verts)
        cx, cy, cz, radius = compute_bounding_sphere(verts, aabb_min, aabb_max)

        flags = MESH_FLAG_SKINNED | MESH_FLAG_HAS_COLOR | MESH_FLAG_HAS_TEXCOORD | MESH_FLAG_HAS_NORMAL | MESH_FLAG_BIG_ENDIAN
        header = struct.pack(
            '>4sHHIIIIIIII4f3f3fI3I',
            WMESH_MAGIC, 1, flags,
            vertex_count, index_count, SKINNED_VERTEX_STRIDE,
            0, # U16 indices
            vbo_offset_aligned, vbo_size,
            ibo_offset_aligned, ibo_size,
            cx, cy, cz, radius,
            aabb_min[0], aabb_min[1], aabb_min[2],
            aabb_max[0], aabb_max[1], aabb_max[2],
            1, 0, 0, 0
        )

        with open(wmesh_path, 'wb') as f:
            f.write(header)
            f.write(b'\x00' * pad_pre_vbo)
            for v in verts:
                f.write(struct.pack(
                    '>3f3f2f4f4B4f3f',
                    v.pos[0], v.pos[1], v.pos[2],
                    v.norm[0], v.norm[1], v.norm[2],
                    v.uv[0], v.uv[1],
                    v.col[0], v.col[1], v.col[2], v.col[3],
                    v.bones[0], v.bones[1], v.bones[2], v.bones[3],
                    v.weights[0], v.weights[1], v.weights[2], v.weights[3],
                    0.0, 0.0, 0.0 # 12B padding
                ))
            f.write(b'\x00' * pad_pre_ibo)
            for idx in indices:
                f.write(struct.pack('>H', idx))

        print(f"[COOKER] Exportado '{wmesh_path}': {vertex_count} vértices ({vbo_size}B), {index_count} índices ({ibo_size}B)")

    def export_wskel(self, skeleton_data: List[Dict]):
        wskel_path = os.path.join(self.output_dir, f"{self.asset_name}.wskel")
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
        print(f"[COOKER] Exportado '{wskel_path}': {joint_count} articulaciones")

    def export_wanim(self, cooked_clips: List[Dict], joint_count: int):
        wanim_path = os.path.join(self.output_dir, f"{self.asset_name}.wanim")
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
        print(f"[COOKER] Exportado '{wanim_path}': {clip_count} clips de animación multiclip")

    # -------------------------------------------------------------------------
    # Algoritmo de Poda Heurística y Topología Canónica
    # -------------------------------------------------------------------------
    def auto_cull_skeleton(self, active_bones: List[str], clusters: Dict,
                           cluster_to_bone: Dict, model_hierarchy: Dict,
                           max_joints: int) -> Dict[str, str]:
        remap = {}
        # Identificar hojas (nodos que no son padre de ningún otro hueso activo)
        remaining = set(active_bones)
        while len(remaining) > max_joints:
            parents_of_remaining = set(model_hierarchy.get(b) for b in remaining if model_hierarchy.get(b))
            leaves = [b for b in remaining if b not in parents_of_remaining]
            if not leaves:
                # Si todos tienen hijos (ciclo o jerarquía profunda), tomar cualquiera
                leaves = list(remaining)

            # Ordenar hojas: preferir colapsar las que terminen en End, Tip o tengan menos peso
            leaves.sort(key=lambda b: (0 if 'end' in b.lower() or 'tip' in b.lower() else 1, b))
            to_remove = leaves[0]
            parent = model_hierarchy.get(to_remove)
            if parent:
                # Remapear hacia su padre
                remap[to_remove] = remap.get(parent, parent)
            remaining.remove(to_remove)

        return remap

    def build_topological_hierarchy(self, canonical_bones: set, model_hierarchy: Dict,
                                    bone_remap: Dict) -> List[Tuple[str, int]]:
        # Encontrar ancestros y armar árbol limpio
        # Mapear cada hueso canónico a su padre canónico
        parent_map: Dict[str, Optional[str]] = {}
        for b in canonical_bones:
            curr = model_hierarchy.get(b)
            while curr and curr not in canonical_bones:
                # Si el padre fue remapeado, usar el remapeo
                if curr in bone_remap:
                    curr = bone_remap[curr]
                    break
                curr = model_hierarchy.get(curr)
            parent_map[b] = curr if curr in canonical_bones else None

        # Ordenar topológicamente: raíces primero, luego hijos
        ordered: List[Tuple[str, int]] = []
        name_to_idx: Dict[str, int] = {}
        remaining = set(canonical_bones)

        while remaining:
            # Buscar nodos cuyos padres ya están en ordered o son None
            ready = [b for b in remaining if parent_map[b] is None or parent_map[b] in name_to_idx]
            if not ready:
                # Si hay referencias circulares o raíz no encontrada, forzar una raíz
                ready = [list(remaining)[0]]
                parent_map[ready[0]] = None

            ready.sort() # Determinismo
            for b in ready:
                pname = parent_map[b]
                pidx = name_to_idx[pname] if pname in name_to_idx else -1
                idx = len(ordered)
                ordered.append((b, pidx))
                name_to_idx[b] = idx
                remaining.remove(b)

        return ordered

# -----------------------------------------------------------------------------
# CLI Entrypoint
# -----------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Cooker Universal de Modelos 3D y Animación para Nintendo Wii U")
    parser.add_argument("input", help="Ruta al archivo fuente (.fbx o .obj)")
    parser.add_argument("--name", help="Sobrescribir nombre base del activo (ej: bird_orange)")
    parser.add_argument("--output-dir", default="assets/models", help="Directorio de salida (default: assets/models)")
    parser.add_argument("--config", help="Ruta al archivo opcional de configuración .json")
    parser.add_argument("--scale", type=float, default=1.0, help="Multiplicador de escala del modelo (default: 1.0)")
    parser.add_argument("--tex", help="Ruta opcional a la textura fuente")
    parser.add_argument("--no-sync", action="store_true", help="Omitir copia automática a build/assets y build/content")

    args = parser.parse_args()

    cooker = UniversalModelCooker(
        input_path=args.input,
        output_dir=args.output_dir,
        custom_name=args.name,
        config_path=args.config,
        scale=args.scale,
        custom_tex=args.tex,
        sync=(not args.no_sync)
    )
    cooker.cook()

if __name__ == '__main__':
    main()
