#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ZenCrop architecture measurement harness (read-only).

Reproduces every structural number quoted in
.plan/refactor/zencrop-cxx23-architecture-plan.md. The ratcheting gate lives in
scripts/check_architecture.ps1; this tool is for exploration and for re-measuring
as refactor stages land (module weights, coupling matrix, large files/functions).

Includes are written as bare basenames in this repository because every src
subdirectory is on the compiler include path, so the resolver matches basenames.

Usage:
    python scripts/python/measure_architecture.py
    python scripts/python/measure_architecture.py --json build/artifacts/diagnostics/architecture-measurement.json
"""
import os
import re
import sys
import json
import collections

EXT = ('.cpp', '.h', '.hpp', '.inl')
INC_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
ASSET_EXCLUDE = 'webview_assets'

LAYER = {
    'src/core': 0, 'src/image': 0,
    'src/net': 1, 'src/window': 1, 'src/detect': 1,
    'src/ocr': 2, 'src/ocr/engine': 2, 'src/ocr/layout': 2, 'src/ocr/batch': 2,
    'src/ocr/document': 2, 'src/ocr/model_download': 2,
    'src/screenshot': 3, 'src/screenshot/annotation': 3, 'src/screenshot/render': 3,
    'src/screenshot/editor': 3, 'src/screenshot/overlay': 3, 'src/screenshot/longshot': 3,
    'src/translation': 3, 'src/selection': 3,
    'src/ocr/ui': 4, 'src/ocr/ui/dashboard': 4,
    'src': 5,
}

FORBIDDEN = [
    ('settings_to_screenshot', 'src/core/Settings.cpp', False, 'src/screenshot/', False),
    ('settings_to_ocr_ui', 'src/core/Settings.cpp', False, 'src/ocr/ui/', False),
    ('annotation_to_overlay', 'src/screenshot/annotation/', True, 'src/window/OverlayWindow.h', True),
    ('screenshot_to_ocr_ui', 'src/screenshot/', True, 'src/ocr/ui/', False),
    ('ocr_ui_to_screenshot', 'src/ocr/ui/', True, 'src/screenshot/', False),
    ('net_to_ocr_engine', 'src/net/', True, 'src/ocr/engine/', False),
    ('ocr_engine_to_net', 'src/ocr/engine/', True, 'src/net/', False),
    ('batch_to_document', 'src/ocr/batch/', True, 'src/ocr/document/', False),
    ('document_to_batch', 'src/ocr/document/', True, 'src/ocr/batch/', False),
]

HUB_HEADER = 'src/core/WideStringUtils.h'

IDIOMS = {
    'throw': r'\bthrow\s+\w',
    'try/catch': r'\bcatch\s*\(',
    'return false': r'\breturn\s+false\s*;',
    'std::wstring': r'\bstd::wstring\b',
    'std::span': r'\bstd::span\b',
    'std::expected': r'\bstd::expected\b',
    'std::format': r'\bstd::format\b',
    'std::jthread': r'\bstd::jthread\b',
    'std::string_view': r'\bstd::string_view\b',
    'std::optional': r'\bstd::optional\b',
    'std::unique_ptr': r'\bstd::unique_ptr\b',
    'std::shared_ptr': r'\bstd::shared_ptr\b',
    'std::thread': r'\bstd::thread\b',
    'CreateThread family': r'\b(?:CreateThread|_beginthreadex|_beginthread)\b',
    'std::mutex/lock_guard': r'\bstd::(?:mutex|lock_guard|unique_lock|scoped_lock)\b',
    'std::atomic': r'\bstd::atomic\b',
    'GDI manual release': r'\b(?:DeleteObject|DeleteDC|ReleaseDC|DestroyWindow|DestroyIcon)\b',
    'COM manual Release': r'->Release\s*\(\s*\)',
    'CComPtr/ComPtr': r'\b(?:CComPtr|Microsoft::WRL::ComPtr|winrt::com_ptr)\b',
    'delete': r'\bdelete\s*(\[\s*\])?',
    'raw new T(': r'\bnew\s+[A-Za-z_][\w:<>]*\s*\(',
    'extern declaration': r'^\s*extern\s+[^;]+;',
    'printf family': r'\b(?:printf|fprintf|sprintf|snprintf|swprintf|_snwprintf|wsprintf|vswprintf|StringCchPrintf|StringCbPrintf)\w*\s*\(',
    'WM_MOUSEMOVE': r'\bWM_MOUSEMOVE\b',
    'WM_TIMER': r'\bWM_TIMER\b',
}

FUNC_SIG = re.compile(r'^[A-Za-z_][\w:<>,&*\s\[\]]*\s[\w:~]+\s*\([^;{]*\)\s*(?:const)?\s*\{?\s*$')


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def rel(root, p):
    return os.path.relpath(p, root).replace('\\', '/')


def read(p):
    try:
        with open(p, 'r', encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def count_lines(p):
    try:
        with open(p, 'rb') as f:
            data = f.read()
    except OSError:
        return 0
    if not data:
        return 0
    return data.count(b'\n') + (0 if data.endswith(b'\n') else 1)


def source_files(root):
    out = []
    src = os.path.join(root, 'src')
    for dp, dn, fns in os.walk(src):
        for fn in fns:
            if not fn.lower().endswith(EXT):
                continue
            full = os.path.join(dp, fn)
            r = rel(root, full)
            if ASSET_EXCLUDE in r:
                continue
            out.append(r)
    return sorted(out)


def dir_of(p):
    parts = p.split('/')
    if p.startswith('src/') and len(parts) > 3:
        return '/'.join(parts[:3])
    return '/'.join(parts[:-1])


def build_graph(root, files):
    by_base = collections.defaultdict(list)
    for f in files:
        by_base[os.path.basename(f)].append(f)
    graph = {}
    for f in files:
        targets = []
        for inc in INC_RE.findall(read(os.path.join(root, f.replace('/', os.sep)))):
            cands = by_base.get(os.path.basename(inc.replace('\\', '/')))
            if cands and len(cands) == 1:
                targets.append(cands[0])
        graph[f] = targets
    return graph


def find_cycles(graph):
    colour = {}
    back = []
    for start in graph:
        if colour.get(start):
            continue
        stack = [(start, 0)]
        colour[start] = 1
        path = [start]
        while stack:
            node, idx = stack[-1]
            children = graph.get(node, [])
            if idx >= len(children):
                colour[node] = 2
                stack.pop()
                path.pop()
                continue
            stack[-1] = (node, idx + 1)
            child = children[idx]
            state = colour.get(child)
            if state == 1:
                back.append(list(path) + [child])
            elif state is None:
                colour[child] = 1
                stack.append((child, 0))
                path.append(child)
    return back


def large_functions(root, files, threshold=200):
    found = []
    for f in files:
        if not f.endswith(('.cpp', '.h', '.hpp')):
            continue
        lines = read(os.path.join(root, f.replace('/', os.sep))).splitlines()
        i = 0
        while i < len(lines):
            line = lines[i]
            if len(line) > 400 or ';' in line:
                i += 1
                continue
            if FUNC_SIG.match(line.strip()) and not line.strip().startswith(('if', 'for', 'while', 'switch', 'else', 'return')):
                depth = 0
                started = False
                end = i
                for j in range(i, min(len(lines), i + 4000)):
                    depth += lines[j].count('{') - lines[j].count('}')
                    if '{' in lines[j]:
                        started = True
                    if started and depth <= 0:
                        end = j
                        break
                span = end - i + 1
                if started and span >= threshold:
                    found.append({'file': f, 'line': i + 1, 'lines': span,
                                  'signature': line.strip()[:110]})
                i = end + 1
                continue
            i += 1
    return sorted(found, key=lambda x: -x['lines'])


def main():
    root = repo_root()
    files = source_files(root)
    graph = build_graph(root, files)

    by_ext = collections.defaultdict(lambda: {'files': 0, 'lines': 0})
    inventory = []
    for f in files:
        n = count_lines(os.path.join(root, f.replace('/', os.sep)))
        by_ext[os.path.splitext(f)[1].lower()]['files'] += 1
        by_ext[os.path.splitext(f)[1].lower()]['lines'] += n
        inventory.append({'path': f, 'lines': n})

    module_lines = collections.defaultdict(int)
    module_files = collections.Counter()
    for it in inventory:
        module_lines[dir_of(it['path'])] += it['lines']
        module_files[dir_of(it['path'])] += 1

    matrix = collections.Counter()
    for src, targets in graph.items():
        ds = dir_of(src)
        for t in targets:
            dt = dir_of(t)
            if ds != dt:
                matrix[(ds, dt)] += 1

    inversions = []
    for (a, b), n in matrix.items():
        la, lb = LAYER.get(a), LAYER.get(b)
        if la is not None and lb is not None and la < lb:
            inversions.append({'from': a, 'fromLayer': la, 'to': b, 'toLayer': lb, 'edges': n})
    inversions.sort(key=lambda x: -x['edges'])

    mutual = []
    for (a, b) in list(matrix):
        if (b, a) in matrix and a < b:
            mutual.append({'a': a, 'b': b, 'forward': matrix[(a, b)], 'reverse': matrix[(b, a)]})
    mutual.sort(key=lambda x: -(x['forward'] + x['reverse']))

    forbidden = []
    for src, targets in graph.items():
        for (rid, sglob, s_is_dir, tglob, t_is_file) in FORBIDDEN:
            if not (src.startswith(sglob) if s_is_dir else src == sglob):
                continue
            for t in targets:
                if (t == tglob) if t_is_file else t.startswith(tglob):
                    forbidden.append({'rule': rid, 'from': src, 'to': t})

    included_by = collections.defaultdict(set)
    for src, targets in graph.items():
        for t in targets:
            included_by[t].add(src)

    def blast(header):
        seen, stack = set(), [header]
        while stack:
            cur = stack.pop()
            for r in included_by.get(cur, ()):
                if r not in seen:
                    seen.add(r)
                    stack.append(r)
        return len(seen)

    headers = [f for f in files if f.endswith(('.h', '.hpp', '.inl'))]
    hub = [{'header': h, 'direct': len(included_by.get(h, ())), 'transitive': blast(h)} for h in headers]
    hub.sort(key=lambda x: -x['transitive'])

    idioms = collections.Counter()
    idiom_files = collections.defaultdict(set)
    externs = []
    for f in files:
        text = read(os.path.join(root, f.replace('/', os.sep)))
        for name, pat in IDIOMS.items():
            hits = re.findall(pat, text, re.M)
            if hits:
                idioms[name] += len(hits)
                idiom_files[name].add(f)
                if name == 'extern declaration':
                    for m in re.finditer(pat, text, re.M):
                        externs.append({'file': f, 'decl': m.group(0).strip()[:100]})

    big = large_functions(root, files)

    report = {
        'fileCount': len(files),
        'lineCount': sum(i['lines'] for i in inventory),
        'byExtension': {k: v for k, v in sorted(by_ext.items())},
        'moduleWeight': [{'dir': d, 'files': module_files[d], 'lines': module_lines[d]}
                         for d in sorted(module_lines, key=lambda d: -module_lines[d])],
        'largestFiles': sorted(inventory, key=lambda x: -x['lines'])[:25],
        'includeCycles': len(find_cycles(graph)),
        'forbiddenEdges': forbidden,
        'moduleInversionEdges': sum(i['edges'] for i in inversions),
        'moduleInversions': inversions,
        'moduleMutualPairs': len(mutual),
        'moduleMutual': mutual,
        'hubHeaders': hub[:20],
        'idioms': {k: {'hits': idioms[k], 'files': len(idiom_files[k])} for k in idioms},
        'externDeclarations': externs,
        'largeFunctions': big[:40],
    }

    out = None
    if '--json' in sys.argv:
        idx = sys.argv.index('--json')
        if idx + 1 < len(sys.argv):
            out = sys.argv[idx + 1]
    if out is None:
        out = os.path.join(root, 'build', 'artifacts', 'diagnostics', 'architecture-measurement.json')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=2, ensure_ascii=False)

    print('=' * 74)
    print('ZENCROP ARCHITECTURE MEASUREMENT')
    print('=' * 74)
    print(f"first-party : {report['fileCount']} files / {report['lineCount']:,} lines")
    for e, v in report['byExtension'].items():
        print(f"   {e:6s} {v['files']:4d} files {v['lines']:8,d} lines")
    print()
    print(f"include cycles            : {report['includeCycles']}")
    print(f"forbidden include edges   : {len(forbidden)}")
    print(f"module inversion edges    : {report['moduleInversionEdges']}")
    print(f"module mutual pairs       : {report['moduleMutualPairs']}")
    print(f"extern declarations       : {len(externs)}")
    print()
    print('--- top module weights ---')
    for m in report['moduleWeight'][:10]:
        print(f"  {m['lines']:7,d}  {m['files']:4d} files  {m['dir']}/")
    print()
    print('--- layering inversions ---')
    for i in inversions[:12]:
        print(f"  {i['edges']:4d}  L{i['fromLayer']} {i['from']}  ->  L{i['toLayer']} {i['to']}")
    print()
    print('--- hub headers (transitive includers) ---')
    for h in hub[:8]:
        print(f"  {h['transitive']:4d} transitive {h['direct']:4d} direct  {h['header']}")
    print()
    print('--- largest files ---')
    for it in report['largestFiles'][:12]:
        print(f"  {it['lines']:6,d}  {it['path']}")
    print()
    print('--- largest functions >= 200 lines ---')
    for fn in big[:15]:
        print(f"  {fn['lines']:5,d}  {fn['file']}:{fn['line']}  {fn['signature'][:78]}")
    print()
    print('--- idiom census (hits / files) ---')
    for k in sorted(idioms, key=lambda x: -idioms[x]):
        print(f"  {k:24s} {idioms[k]:6d} / {len(idiom_files[k]):4d}")
    print()
    print('JSON written:', out)


if __name__ == '__main__':
    main()
