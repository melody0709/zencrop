#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Create a safety mirror of the repository at <repo>/.bak.

Invokes robocopy through subprocess with an explicit argument list. Passing the
paths through a shell mangles them (MSYS rewrites D:\\... and /XD stops
matching), which silently disabled the exclusions on the first attempt.

Excluded: ocr/ (re-downloadable models), build/ (generated), .research/ (a
junction to a separate private repository), and .bak/ itself.
"""
import os
import shutil
import subprocess
import sys

REPO = r'D:\GITHUB_melody0709\zencrop_ocr_pxipin'
DEST = os.path.join(REPO, '.bak')
EXCLUDE = [os.path.join(REPO, 'ocr'), os.path.join(REPO, 'build'),
           os.path.join(REPO, '.research'), DEST]

if not os.path.basename(DEST) == '.bak':
    raise SystemExit('refusing to run: destination must end with .bak')
if not os.path.isdir(REPO):
    raise SystemExit('repository root is missing: ' + REPO)

if os.path.isdir(DEST):
    print('removing previous mirror:', DEST)
    shutil.rmtree(DEST, ignore_errors=False)
os.makedirs(DEST, exist_ok=True)

cmd = ['robocopy', REPO, DEST, '/E', '/MIR', '/XJ', '/R:1', '/W:1', '/MT:16',
       '/NFL', '/NDL', '/NP', '/NJH', '/NJS'] + ['/XD'] + EXCLUDE
print('running:', ' '.join(cmd[:4]), '... /XD', ' '.join(os.path.basename(p) for p in EXCLUDE))
proc = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
print('robocopy exit code:', proc.returncode)
tail = [l for l in (proc.stdout or '').splitlines() if l.strip()]
for line in tail[-8:]:
    print('   ', line.strip()[:150])
if proc.returncode > 7:
    print('robocopy reported a failure (exit > 7)', file=sys.stderr)
    sys.exit(1)


def measure(root, skip_names):
    files = 0
    total = 0
    for dp, dns, fns in os.walk(root):
        rel = os.path.relpath(dp, root)
        top = rel.split(os.sep)[0]
        if top in skip_names:
            dns[:] = []
            continue
        for fn in fns:
            files += 1
            try:
                total += os.path.getsize(os.path.join(dp, fn))
            except OSError:
                pass
    return files, total


src_files, src_bytes = measure(REPO, {'ocr', 'build', '.research', '.bak'})
dst_files, dst_bytes = measure(DEST, set())

print()
print(f'source (excluded) : {src_files:6d} files  {src_bytes:,} bytes')
print(f'mirror            : {dst_files:6d} files  {dst_bytes:,} bytes')
print('file delta        :', dst_files - src_files)
print('byte delta        :', dst_bytes - src_bytes)
ok = (dst_files == src_files) and (dst_bytes == src_bytes)
print()
print('MIRROR MATCHES SOURCE:', ok)
sys.exit(0 if ok else 1)
