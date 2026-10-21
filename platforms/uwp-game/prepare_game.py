# SPDX-License-Identifier: MPL-2.0
# Runs only inside the packaged interpreter; generated game code stays on USB.
import os, sys, json, hashlib, builtins
sys.path[:0] = [os.path.join(wwhd_tools, 'recomp'), wwhd_tools]
for key in ['WWHD_RECOMP_CRLIVE','WWHD_RECOMP_LEAF','WWHD_RECOMP_NONLEAF','WWHD_RECOMP_GQR']:
    os.environ[key] = '1'
for key in ['WWHD_RECOMP_PLAIN','WWHD_RECOMP_CR_CHECK','WWHD_RECOMP_LEAF_POISON']:
    os.environ[key] = '0'
with open(wwhd_rpx, 'rb') as f:
    digest = hashlib.file_digest(f, 'sha256').hexdigest()
if digest != 'c4f0ab300542e0bfc462696850534e71db2ad02288a7eb55e5a4cd4062f16153':
    raise ValueError('Unsupported game. Use the original USA release of Wind Waker HD.')
import recomp
compiler = recomp.Recompiler(wwhd_rpx, mod_hooks=True)
original_open = builtins.open
def progress_open(path, *args, **kwargs):
    mode = args[0] if args else kwargs.get('mode', 'r')
    if isinstance(path, str) and 'w' in mode and os.path.basename(path).startswith('code_'):
        print('GENERATING ' + os.path.basename(path))
    return original_open(path, *args, **kwargs)
builtins.open = progress_open
try:
    compiler.run(wwhd_work, 30000)
finally:
    builtins.open = original_open
units = [name for name in os.listdir(wwhd_work) if name.startswith('code_') and name.endswith('.c')]
if compiler.build.name != 'USA' or len(compiler.sorted_entries) != 39713 or len(units) != 78:
    raise ValueError('Translation does not match the supported runtime profile.')
print('PASS translation: USA, 39713 functions, 78 source units')
