#!/usr/bin/env python3
"""
tools/cooker/cook_reflective_textures.py

Genera y cocina las texturas con máscara de reflectividad en el canal Alpha
para la Casa Holandesa (ventanas) y el Pájaro Cantor (ojos) en formato BC3 (DXT5)
para Nintendo Wii U (AMD Latte GX2).
"""

import os
import sys
import struct
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from mesh_cooker import cook_texture

def cook_dutch_house_reflective():
    print("=== [1/2] Procesando Textura Reflectiva de Casa Holandesa ===")
    diff_path = "models/dutch-house/textures/soloport_house_2.0.png"
    rough_path = "models/dutch-house/textures/soloport_house_roughness.png"
    temp_rgba_path = "temp_house_reflective_rgba.png"
    out_wtex_path = "assets/models/dutch_house.wtex"

    diff_img = Image.open(diff_path).convert("RGB")
    rough_img = Image.open(rough_path).convert("L")
    w, h = diff_img.size

    # Inversión de Roughness:
    # Cristales de ventanas (Roughness = 0) -> Alpha = 255 (Reflectividad 1.0)
    # Paredes, vigas y tejas (Roughness = 255) -> Alpha = 0 (Reflectividad 0.0)
    alpha_data = bytearray(w * h)
    rough_bytes = rough_img.tobytes()

    for i in range(w * h):
        r_val = rough_bytes[i]
        # Inversión con umbral limpio: valores < 50 son ventanas de cristal
        if r_val < 50:
            alpha_data[i] = 255
        else:
            alpha_data[i] = 0

    alpha_img = Image.frombytes("L", (w, h), bytes(alpha_data))
    r, g, b = diff_img.split()
    rgba_img = Image.merge("RGBA", (r, g, b, alpha_img))
    rgba_img.save(temp_rgba_path)
    print(f"Imagen combinada temporal guardada: {temp_rgba_path} ({w}x{h})")

    # Cocinar a .wtex en formato BC3 (DXT5)
    cook_texture(temp_rgba_path, out_wtex_path, target_size=2048, tex_format="bc3")
    print(f"Textura cocinada con éxito: {out_wtex_path} (BC3)")

    if os.path.exists(temp_rgba_path):
        os.remove(temp_rgba_path)

def cook_bird_reflective():
    print("=== [2/2] Procesando Textura Reflectiva de Pájaro Cantor ===")
    diff_path = "models/bird-orange/textures/BirdOrange_BaseColor.png"
    temp_rgba_path = "temp_bird_reflective_rgba.png"
    out_wtex_path = "assets/models/bird_orange.wtex"

    diff_img = Image.open(diff_path).convert("RGB")
    w, h = diff_img.size

    # Canal Alpha inicial en 0 (todo el plumaje y cuerpo es mate sin reflejo especular de CubeMap)
    alpha_data = bytearray(w * h)

    # Identificar la región UV exacta de los ojos de los 122 vértices de Eye_L y Eye_R
    # Del análisis de vértices en bird_orange.wmesh:
    # U: 0.4552 a 0.7524 -> X: 932 a 1540
    # V: 0.1788 a 0.4215 -> Y: 366 a 863
    # Dentro de esta región, los texeles oscuros (< 45 en RGB) corresponden al iris/pupila/córnea
    for y in range(360, 870):
        for x in range(930, 1550):
            idx = y * w + x
            pr, pg, pb = diff_img.getpixel((x, y))
            # Ojos y córnea del pájaro
            if pr < 50 and pg < 50 and pb < 50:
                alpha_data[idx] = 255
            elif pr < 70 and pg < 60 and pb < 40 and (y > 400 and y < 800 and x > 980 and x < 1500):
                alpha_data[idx] = 220

    alpha_img = Image.frombytes("L", (w, h), bytes(alpha_data))
    r, g, b = diff_img.split()
    rgba_img = Image.merge("RGBA", (r, g, b, alpha_img))
    rgba_img.save(temp_rgba_path)
    print(f"Imagen combinada temporal guardada: {temp_rgba_path} ({w}x{h})")

    # Cocinar a .wtex en formato BC3 (DXT5)
    cook_texture(temp_rgba_path, out_wtex_path, target_size=2048, tex_format="bc3")
    print(f"Textura cocinada con éxito: {out_wtex_path} (BC3)")

    if os.path.exists(temp_rgba_path):
        os.remove(temp_rgba_path)

    # Sincronizar en build/assets y build/content
    for fname in ["dutch_house.wtex", "bird_orange.wtex"]:
        src = os.path.join("assets/models", fname)
        for dst_dir in ["build/assets/models", "build/content/models"]:
            dst = os.path.join(dst_dir, fname)
            with open(src, "rb") as sf, open(dst, "wb") as df:
                df.write(sf.read())
    print("Sincronización de texturas BC3 en build/ completada.")

if __name__ == "__main__":
    cook_dutch_house_reflective()
    cook_bird_reflective()
    print("=== PROCESAMIENTO DE ASSETS COMPLETADO CON ÉXITO ===")
