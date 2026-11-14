#!/usr/bin/env python3
"""
tools/cooker/cook_skybox.py

Generador de CubeMap Atmosférico Nativo (skybox_day.wcube) para Nintendo Wii U (Latte GX2).
Proyecta de forma exacta la textura hemisférica doom_sky11 (utilizada en skydome.wtex y skydome.wmesh)
hacia las 6 caras del CubeMap nativo (256x256 RGBA8 por cara, 1.5MB total).
Garantiza coherencia física y cromática al 100% entre la bóveda visible y las reflexiones Fresnel.
"""

import os
import sys
import math
import struct
import shutil
from PIL import Image

WCUBE_MAGIC = b'WCUB'

# Tabla de calibración geométrica exacta de elevación vs V de skydome.wmesh
ELEV_TABLE = [
    (90.00, 0.000),
    (74.08, 0.125),
    (58.60, 0.250),
    (43.90, 0.375),
    (30.12, 0.500),
    (17.28, 0.625),
    ( 5.30, 0.750),
    (-5.93, 0.875),
    (-16.54, 1.000)
]

def elev_to_v(elev_deg):
    if elev_deg >= ELEV_TABLE[0][0]:
        return ELEV_TABLE[0][1]
    if elev_deg <= ELEV_TABLE[-1][0]:
        return ELEV_TABLE[-1][1]
    for i in range(len(ELEV_TABLE) - 1):
        e0, v0 = ELEV_TABLE[i]
        e1, v1 = ELEV_TABLE[i+1]
        if e0 >= elev_deg >= e1:
            t = (elev_deg - e0) / (e1 - e0)
            return v0 + t * (v1 - v0)
    return 1.0

def sample_bilinear(pix, W, H, u_eq, v_eq):
    """Muestreo bilineal continuo con wrap horizontal sobre la textura panorámica."""
    x = (u_eq % 1.0) * (W - 1)
    y = max(0.0, min(float(H - 1), v_eq * (H - 1)))
    x0 = int(x)
    y0 = int(y)
    x1 = (x0 + 1) % W
    y1 = min(H - 1, y0 + 1)
    fx = x - x0
    fy = y - y0
    c00 = pix[x0, y0]
    c10 = pix[x1, y0]
    c01 = pix[x0, y1]
    c11 = pix[x1, y1]
    r = (c00[0]*(1-fx) + c10[0]*fx)*(1-fy) + (c01[0]*(1-fx) + c11[0]*fx)*fy
    g = (c00[1]*(1-fx) + c10[1]*fx)*(1-fy) + (c01[1]*(1-fx) + c11[1]*fx)*fy
    b = (c00[2]*(1-fx) + c10[2]*fx)*(1-fy) + (c01[2]*(1-fx) + c11[2]*fx)*fy
    return (int(r), int(g), int(b), 255)

def dir_for_face_pixel(face, u, v):
    """Mapeo canónico OpenGL / Latte GX2 de caras de CubeMap a vectores unitarios 3D."""
    if face == 0:   # +X (Right)
        dx, dy, dz = 1.0, -v, -u
    elif face == 1: # -X (Left)
        dx, dy, dz = -1.0, -v, u
    elif face == 2: # +Y (Top / Zenith)
        dx, dy, dz = u, 1.0, v
    elif face == 3: # -Y (Bottom / Ground)
        dx, dy, dz = u, -1.0, -v
    elif face == 4: # +Z (Front)
        dx, dy, dz = u, -v, 1.0
    elif face == 5: # -Z (Back)
        dx, dy, dz = -u, -v, -1.0
    else:
        dx, dy, dz = 0.0, 1.0, 0.0
    l = math.sqrt(dx*dx + dy*dy + dz*dz)
    return dx/l, dy/l, dz/l

def generate_skybox_cubemap(face_size=256,
                            output_path="assets/models/skybox_day.wcube",
                            pano_path="scratch/doom_sky11.png"):
    print(f"[*] Generando Skybox CubeMap ({face_size}x{face_size} por cara, 6 caras) desde '{pano_path}'...")
    
    if not os.path.exists(pano_path):
        print(f"[!] Error: no se encontró la textura fuente del domo '{pano_path}'")
        sys.exit(1)

    im = Image.open(pano_path).convert('RGB')
    W, H = im.size
    pix = im.load()
    print(f"    - Textura fuente cargada: {W}x{H} RGB")

    face_bytes = []
    face_names = ["+X (Right)", "-X (Left)", "+Y (Top / Zenith)", "-Y (Bottom / Ground)", "+Z (Front)", "-Z (Back)"]
    
    for face in range(6):
        print(f"    - Renderizando cara {face}: {face_names[face]}...")
        for y in range(face_size):
            v = 2.0 * ((y + 0.5) / face_size) - 1.0
            for x in range(face_size):
                u = 2.0 * ((x + 0.5) / face_size) - 1.0
                dx, dy, dz = dir_for_face_pixel(face, u, v)

                # Elevación determina si muestreamos el cielo superior o el terreno inferior
                elev_deg = math.degrees(math.asin(max(-1.0, min(1.0, dy))))

                if elev_deg >= 0.0:
                    # Hemisferio superior: Proyección exacta del cielo doom_sky11
                    az = math.atan2(dx, dz)
                    u_eq = ((az - math.pi * 0.5) / (2.0 * math.pi)) % 1.0
                    v_eq = elev_to_v(elev_deg)
                    rgba = sample_bilinear(pix, W, H, u_eq, v_eq)
                elif elev_deg >= -15.0:
                    # Zona de transición suave (0° a -15°):
                    # Blend suave desde el color del horizonte hacia el tono del terreno
                    az = math.atan2(dx, dz)
                    u_eq = ((az - math.pi * 0.5) / (2.0 * math.pi)) % 1.0
                    sky_rgba = sample_bilinear(pix, W, H, u_eq, elev_to_v(0.0))
                    
                    t = -elev_deg / 15.0 # 0.0 en horizonte, 1.0 a -15°
                    # Color del terreno/césped de la escena (ground_grass)
                    ground_r, ground_g, ground_b = 65, 80, 55
                    r = int(sky_rgba[0] * (1.0 - t) + ground_r * t)
                    g = int(sky_rgba[1] * (1.0 - t) + ground_g * t)
                    b = int(sky_rgba[2] * (1.0 - t) + ground_b * t)
                    rgba = (r, g, b, 255)
                else:
                    # Hemisferio inferior completo (< -15° hasta -90° en el Nadir):
                    # Tono de suelo isotrópico continuo sin dependencia azimutal
                    # Erradica al 100% el colapso radial / singularidad polar en la cara -Y (Bottom)
                    t_nadir = (-elev_deg - 15.0) / 75.0 # 0.0 a -15°, 1.0 a -90°
                    # Atenuación suave hacia el nadir (de 65, 80, 55 a 40, 50, 35)
                    r = int(65 * (1.0 - t_nadir) + 40 * t_nadir)
                    g = int(80 * (1.0 - t_nadir) + 50 * t_nadir)
                    b = int(55 * (1.0 - t_nadir) + 35 * t_nadir)
                    rgba = (r, g, b, 255)

                face_bytes.append(struct.pack('4B', *rgba))

    all_data = b''.join(face_bytes)
    total_face_size = face_size * face_size * 4
    total_data_size = total_face_size * 6

    header = struct.pack('>4sHHI20s', WCUBE_MAGIC, 1, face_size, total_data_size, b'\x00'*20)
    
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(all_data)

    print(f"[*] Exito: '{output_path}' generado ({len(header) + len(all_data)} bytes)")

if __name__ == "__main__":
    generate_skybox_cubemap()
