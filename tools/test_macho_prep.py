#!/usr/bin/env python3
"""Regresión del rebase de __text (tools/macho_prep.py) con casos vistos en consola.

Uso: python3 -I tools/test_macho_prep.py [Executable]   (por defecto game_app/BladesOfFury)
"""
import os, struct, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'game_app', 'BladesOfFury')

CASES = {
    0x2480: True,    # corrida 5: ldr r3,=__bss absoluto (primer constructor, ARM)
    0x145f8: False,  # corrida 7: offset PIC Thumb (ldr r3,[pc] + add r3, pc)
    0x23718: True,   # tabla de switch non-PIC (ldrls pc, [pc, r0, lsl #2]): direcciones de case
    0x39be80: False, # corrida 8: zlib _order.3008 {8, 7} = 0x00070008 parece un inicio de función
    0x39a908: False, # zlib _static_ltree, mismo valor
    0x3a7124: False, # zlib _configuration_table {8, 16} = 0x00100008 (ush, no puntero)
    0x3a70f0: True,  # zlib _configuration_table[0].func (deflate_stored)
    0x39c760: False, # _k_country (texto, no alineado): "\0SE\0" = 0x00455300
    0x58928: False,  # corrida 13: CTrailSceneNode::update offset 0x4804 != static_initialization_0
}

with tempfile.TemporaryDirectory() as d:
    out = os.path.join(d, 'rebase.bin')
    subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'macho_prep.py'), BIN, out],
                   check=True, stdout=subprocess.DEVNULL)
    b = open(out, 'rb').read()
    rebase = set(struct.unpack_from(f'<{struct.unpack_from("<I", b, 4)[0]}I', b, 8))
bad = [f'{a:#x} esperado {"en" if want else "fuera de"} rebase.bin' for a, want in CASES.items() if (a in rebase) != want]
print('\n'.join(bad) or f'macho_prep ok ({len(CASES)} casos)')
sys.exit(1 if bad else 0)
