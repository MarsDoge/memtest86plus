#!/usr/bin/env python3
"""Rebuild, validate and package local-only SNP A/B artifacts."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tarfile

SRC = Path(__file__).resolve().parents[1]
ART = Path('/home/qdy/artifacts/memtest-snp-ab-20261015')
SCRATCH = Path('/home/qdy/.hermes/cache/scratch')
BASE = '32a5fe7ecbc40b4be3749b42b758ee05d2d7e206'

def run(cmd, cwd=SRC, **kwargs):
    return subprocess.run(cmd, cwd=cwd, check=True, **kwargs)

result = {'base': BASE, 'source_worktree': str(SRC), 'artifacts': {}}
for variant, flag, name in [('A', '0', 'memtest-A-baseline.efi'), ('B', '1', 'memtest-B-snp-stop.efi')]:
    with (ART / f'build-{variant}.log').open('w') as log:
        run(['make', '-B', '-j8', 'CC=loongarch64-linux-gnu-gcc', 'LD=loongarch64-linux-gnu-ld',
             'OBJCOPY=loongarch64-linux-gnu-objcopy', f'SNP_AB_TEST={flag}'],
            cwd=SRC / 'build/loongarch64', stdout=log, stderr=subprocess.STDOUT)
    new = (SRC / 'build/loongarch64/mt86plus').read_bytes()
    old = (ART / name).read_bytes()
    assert new == old, f'{variant} changed since QEMU run; rerun smoke before publishing report'
    pe = struct.unpack_from('<I', new, 0x3c)[0]
    assert new[:2] == b'MZ' and new[pe:pe+4] == b'PE\0\0'
    machine = struct.unpack_from('<H', new, pe+4)[0]
    subsystem = struct.unpack_from('<H', new, pe+24+68)[0]
    assert machine == 0x6264 and subsystem == 10
    result['artifacts'][name] = {'bytes': len(new), 'sha256': hashlib.sha256(new).hexdigest(),
                                  'machine': hex(machine), 'subsystem': subsystem,
                                  'same_bytes_as_qemu_smoke': True}
run(['gcc', '-std=gnu11', '-O0', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
     '-Iboot', '-Isystem', 'tests/efi_snp_host.c', '-o', str(SCRATCH/'efi-snp-host')])
with (ART/'host-tests.log').open('w') as log:
    run([str(SCRATCH/'efi-snp-host')], stdout=log, stderr=subprocess.STDOUT)
result['host_passes'] = sum(line.startswith('PASS ') for line in (ART/'host-tests.log').read_text().splitlines())
assert result['host_passes'] == 24
edk2 = Path('/home/qdy/edk2')
for arch, cc in [('LoongArch64', 'loongarch64-linux-gnu-gcc'), ('X64', 'gcc')]:
    run([cc, '-std=gnu11', '-fshort-wchar', '-Wall', '-Werror', '-I'+str(edk2/'MdePkg/Include'),
         '-I'+str(edk2/'MdePkg/Include'/arch), '-c', 'tests/efi_snp_edk2_abi.c',
         '-o', str(SCRATCH/f'efi-snp-edk2-{arch}-abi.o')])
result['edk2_abi'] = 'PASS X64 and LOONGARCH64'
result['edk2_commit'] = run(['git', 'rev-parse', 'HEAD'], cwd=edk2, capture_output=True, text=True).stdout.strip()
invalid = subprocess.run(['make','-C','build/loongarch64','SNP_AB_TEST=2','-n'], cwd=SRC, capture_output=True)
assert invalid.returncode != 0 and b'SNP_AB_TEST must be 0 or 1' in invalid.stderr
result['invalid_build_flag'] = 'PASS rejected'
run(['git', 'diff', '--check'])
result['diff_check'] = 'PASS'
patch = run(['git', 'diff', '--binary'], capture_output=True).stdout
newfiles = ['boot/efisnp.h', 'tests/efi_snp_host.c', 'tests/efi_snp_edk2_abi.c',
            'tests/qemu_snp_smoke.py', 'tests/package_snp_ab.py']
for name in newfiles:
    diff = subprocess.run(['git','diff','--no-index','--binary','/dev/null',name], cwd=SRC, capture_output=True)
    assert diff.returncode == 1
    patch += diff.stdout
(ART/'snp-ab.patch').write_bytes(patch)
# Check against an isolated index; leave source/index in the user's worktree untouched.
index = SCRATCH/'memtest-snp-apply-index'
if index.exists(): index.unlink()
env = dict(os.environ, GIT_INDEX_FILE=str(index))
run(['git','read-tree',BASE], env=env)
run(['git','apply','--cached','--check',str(ART/'snp-ab.patch')], env=env)
result['patch_apply_check'] = 'PASS against exact base using isolated index'
index.unlink()
tracked = run(['git','ls-files','-z'],capture_output=True).stdout.decode().split('\0')
with tarfile.open(ART/'source.tar.gz','w:gz') as tar:
    for name in sorted(set([name for name in tracked if name] + newfiles)):
        tar.add(SRC/name, arcname='memtest-snp-ab/'+name, recursive=False)
shutil.copyfile(ART/'README.zh-CN.md', ART/'README.md')
result['qemu'] = json.loads((ART/'qemu-results.json').read_text())
result['hardware'] = 'NOT RUN: target RTL8168 and physical memory test pending'
(ART/'validation.json').write_text(json.dumps(result, indent=2)+'\n')
files = ['memtest-A-baseline.efi','memtest-B-snp-stop.efi','snp-ab.patch','source.tar.gz',
         'README.zh-CN.md','validation.json','host-tests.log']
(ART/'SHA256SUMS').write_text(''.join(hashlib.sha256((ART/name).read_bytes()).hexdigest()+'  '+name+'\n' for name in files))
print(json.dumps(result, indent=2))
