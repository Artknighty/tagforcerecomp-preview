# Compila os sombreadores GLSL de runtime/shaders/ com glslc e gera gen/gpu_spv.h (arrays uint32_t spv_<nome>_<estagio>).
import subprocess, sys, os, struct, glob
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
out = os.path.join(root, 'gen', 'gpu_spv.h'); os.makedirs(os.path.dirname(out), exist_ok=True)
glslc = 'C:/msys64/ucrt64/bin/glslc.exe'
src = sorted(glob.glob(os.path.join(root, 'runtime', 'shaders', '*.*')))
if os.path.exists(out) and os.path.getmtime(out) >= max(os.path.getmtime(f) for f in src):
    sys.exit(0)
with open(out, 'w') as o:
    o.write('// gerado por tools/build/spv2h.py a partir de runtime/shaders/*\n#pragma once\n#include <stdint.h>\n')
    for f in src:
        name = os.path.basename(f).replace('.', '_')
        r = subprocess.run([glslc, '-O', '-o', '-', f], capture_output=True)
        if r.returncode: sys.stderr.write(r.stderr.decode()); sys.exit(1)
        w = struct.unpack('<%dI' % (len(r.stdout) // 4), r.stdout)
        o.write('static const uint32_t spv_%s[] = {%s};\n' % (name, ','.join('0x%x' % x for x in w)))
print('gpu_spv.h gerado')
