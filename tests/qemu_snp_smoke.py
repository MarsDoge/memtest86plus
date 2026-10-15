#!/usr/bin/env python3
"""Local LOONGARCH64 A/B smoke; no real NIC or physical DMA claim."""
import concurrent.futures
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import os

ART = pathlib.Path('/home/qdy/artifacts/memtest-snp-ab-20261015')
SCRATCH = pathlib.Path('/home/qdy/.hermes/cache/scratch/memtest-snp-qemu')
FW = pathlib.Path('/home/qdy/Desktop/github/qemu/build/pc-bios/edk2-loongarch64-code.fd')
SHELL = pathlib.Path('/home/qdy/Desktop/github/Build/LoongArchVirtQemu/DEBUG_GCC/LOONGARCH64/Shell.efi')

def run(label):
    esp = SCRATCH / label / 'esp'
    boot = esp / 'EFI' / 'BOOT'
    boot.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(SHELL, boot / 'BOOTLOONGARCH64.EFI')
    binary = ART / ('memtest-A-baseline.efi' if label == 'A' else 'memtest-B-snp-stop.efi')
    shutil.copyfile(binary, esp / 'startup.nsh.efi')
    (esp / 'startup.nsh').write_bytes(b'@echo -off\r\n%0.efi log=mmio,0x1fe001e0 nosmp\r\n')
    cmd = ['qemu-system-loongarch64', '-machine', 'virt', '-accel', 'tcg', '-cpu', 'la464',
           '-m', '512', '-smp', '1', '-nodefaults', '-display', 'none', '-monitor', 'none',
           '-serial', 'stdio', '-bios', str(FW), '-no-reboot',
           '-drive', f'file=fat:rw:{esp},format=raw,if=none,id=disk',
           '-device', 'virtio-blk-pci,drive=disk',
           '-object', 'rng-random,id=rng0,filename=/dev/urandom', '-device', 'virtio-rng-pci,rng=rng0',
           '-netdev', 'user,id=net0,restrict=on', '-device', 'virtio-net-pci,netdev=net0,romfile=']
    (ART / f'qemu-{label}-command.json').write_text(json.dumps(cmd, indent=2))
    with (ART / f'qemu-{label}.log').open('wb') as out:
        proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            rc = proc.wait(timeout=75)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait()
            rc = '75s smoke timeout (expected for running tester)'
    text = (ART / f'qemu-{label}.log').read_text(errors='replace')
    clean = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text).replace('\r', '')
    (ART / f'qemu-{label}-clean.log').write_text(clean)
    lines = [line for line in clean.splitlines() if 'SNP-AB' in line or 'Memtest' in line or 'PASS' in line or 'TEST' in line or 'ERROR' in line]
    return {'variant': label, 'exit': rc, 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'selected_lines': lines[-40:]}

if __name__ == '__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(run, ['A', 'B']))
    (ART / 'qemu-results.json').write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))
