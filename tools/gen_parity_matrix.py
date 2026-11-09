#!/usr/bin/env python3
"""Generates PARITY_MATRIX.md: one row per Kurigram reference function (330 namespaced + 13 wrappers).

Evidence used for each row, all read from this tree:
  - schema:    the generated TL function struct exists in include/cppgram/mtproto/schema/api.h
  - request:   source builds the request (fn::<ns>_<method>) outside comments and the schema directory
  - checklist: status column of CPPGRAM_IMPLEMENTATION_CHECKLIST.md (PARTIAL and BLOCKED are taken from it)
  - tests:     test bodies (TEST / TEST_F blocks) that name the request struct
  - results:   optional gtest XML files passed with --results (produced by running the test binaries)

Categories (checked in this order):
  SCHEMA_BLOCKED                the TL function is not in the layer 233 schema
  NOT_APPLICABLE                checklist status starts with NOT_APPLICABLE (none are assigned at present)
  PARTIAL                       checklist status starts with PARTIAL (none are assigned at present)
  NOT_IMPLEMENTED               no request is constructed in hand-written source
  IMPLEMENTED_WITH_LIMITATIONS  checklist status starts with IMPLEMENTED_WITH_LIMITATIONS; the limits are in the row
  IMPLEMENTED                   request constructed in hand-written source, with no recorded limitation

The evidence column is separate from the category. It says whether a test names the request and whether that test passed
in the supplied results. An IMPLEMENTED row with "source only" evidence has no test behind it.

"Implemented" here means hand-written source constructs the request. It does not mean the behavior was reviewed against
Kurigram; no row in this matrix is a behavioral audit.
Usage: python3 tools/gen_parity_matrix.py [--results DIR] [--write]
"""
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
REF = os.path.join(ROOT, 'tools', 'kurigram_tl_calls.txt')
API = os.path.join(ROOT, 'include', 'cppgram', 'mtproto', 'schema', 'api.h')
CHECK = os.path.join(ROOT, 'CPPGRAM_IMPLEMENTATION_CHECKLIST.md')
OUT = os.path.join(ROOT, 'PARITY_MATRIX.md')


def read(p):
    with open(p, encoding='utf-8', errors='replace') as f:
        return f.read()


def camel(method):
    return method[0].lower() + method[1:]


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def test_bodies(path):
    """Yields (suite, name, body) for each TEST/TEST_F/TEST_P block in a file."""
    text = read(path)
    starts = list(re.finditer(r'^\s*TEST(?:_F|_P)?\(\s*(\w+)\s*,\s*(\w+)\s*\)', text, re.M))
    for i, m in enumerate(starts):
        end = starts[i + 1].start() if i + 1 < len(starts) else len(text)
        yield m.group(1), m.group(2), text[m.start():end]


refs = []
for line in read(REF).splitlines():
    parts = line.split()
    if len(parts) == 2:
        refs.append(parts[1])
namespaced = [r for r in refs if '.' in r]
wrappers = [r for r in refs if '.' not in r]

schema_structs = set(re.findall(r'^struct (\w+) \{', read(API), re.M))

src_blob = []
for base in ['src', os.path.join('include', 'cppgram')]:
    for dp, dn, fn in os.walk(os.path.join(ROOT, base)):
        if os.sep + 'schema' in dp + os.sep:
            continue
        for f in fn:
            if f.endswith(('.cpp', '.h')):
                src_blob.append(strip_comments(read(os.path.join(dp, f))))
src_blob = '\n'.join(src_blob)

test_index = {}  # struct token -> list of "Suite.Name"
for path in glob.glob(os.path.join(ROOT, 'tests', '**', '*.cpp'), recursive=True):
    for suite, name, body in test_bodies(path):
        for token in set(re.findall(r'\b(\w+)::kId\b|\bfn::(\w+)\b', body)):
            tok = token[0] or token[1]
            test_index.setdefault(tok, set()).add(f'{suite}.{name}')

results = {}
if '--results' in sys.argv:
    rdir = sys.argv[sys.argv.index('--results') + 1]
    for xp in glob.glob(os.path.join(rdir, '*.xml')):
        for tc in ET.parse(xp).getroot().iter('testcase'):
            key = f"{tc.get('classname')}.{tc.get('name')}"
            failed = any(child.tag in ('failure', 'error') for child in tc)
            skipped = any(child.tag == 'skipped' for child in tc)
            results[key] = 'skipped' if skipped else ('failed' if failed else 'passed')

checklist = {}
for line in read(CHECK).splitlines():
    if not line.startswith('|'):
        continue
    cells = [c.strip() for c in line.strip().strip('|').split('|')]
    if len(cells) >= 2:
        name = cells[0].strip('`')
        if re.match(r'^[a-z]+\.[A-Za-z]+$', name):
            checklist[name] = cells[-1]  # original text; classification is case-insensitive

rows = []
counts = {}
evidence_counts = {}
for name in namespaced:
    ns, meth = name.split('.', 1)
    token = f'{ns}_{camel(meth)}'
    in_schema = token in schema_structs
    requested = re.search(r'\bfn::' + re.escape(token) + r'\b', src_blob) is not None
    tests = sorted(test_index.get(token, []))
    status = checklist.get(name, '')
    status_up = status.upper()
    if not in_schema:
        cat = 'SCHEMA_BLOCKED'
    elif status_up.startswith('NOT_APPLICABLE'):
        cat = 'NOT_APPLICABLE'
    elif status_up.startswith('PARTIAL'):
        cat = 'PARTIAL'
    elif not requested:
        cat = 'NOT_IMPLEMENTED'
    elif status_up.startswith('IMPLEMENTED_WITH_LIMITATIONS'):
        cat = 'IMPLEMENTED_WITH_LIMITATIONS'
    else:
        cat = 'IMPLEMENTED'
    counts[cat] = counts.get(cat, 0) + 1
    if not results:
        result_text = 'not run' if tests else 'no test'
    elif not tests:
        result_text = 'no test'
    else:
        vals = [results.get(t, 'not run') for t in tests]
        result_text = 'passed' if all(v == 'passed' for v in vals) else ', '.join(sorted(set(vals)))
    if not tests:
        evidence = 'source only: no test names it'
    elif not results:
        evidence = f'{len(tests)} test(s) named; results not supplied'
    elif all(results.get(t) == 'passed' for t in tests):
        evidence = f'{len(tests)} test(s) passed offline'
    else:
        evidence = 'a named test failed or was not run'
    if cat == 'IMPLEMENTED':
        if not tests:
            key = 'source only'
        elif not results:
            key = 'named by a test, results not supplied'
        elif all(results.get(t) == 'passed' for t in tests):
            key = 'a named test passed offline'
        else:
            key = 'a named test failed or was not run'
        evidence_counts[key] = evidence_counts.get(key, 0) + 1
    rows.append((name, 'yes' if in_schema else 'no', 'yes' if requested else 'no',
                 status or '-', str(len(tests)), result_text, evidence, cat))

if '--write' in sys.argv:
    order = ['IMPLEMENTED', 'IMPLEMENTED_WITH_LIMITATIONS', 'PARTIAL', 'NOT_IMPLEMENTED', 'NOT_APPLICABLE',
             'SCHEMA_BLOCKED']
    out = [
        '# CppGram vs Kurigram: parity matrix (source and evidence classification)',
        '',
        'Generated by `tools/gen_parity_matrix.py`. Re-run it after changing the source, the tests or the checklist.',
        '',
        '**This is not a behavioral audit against Kurigram.** Each row records whether the schema has the function, whether',
        'hand-written source builds its request, what the checklist says, and whether a test that names it passed in the',
        'supplied gtest results. No row was checked against Kurigram\'s behavior. A row marked IMPLEMENTED does not mean the',
        'method matches Kurigram in every case.',
        '',
        'Reference list: `tools/kurigram_tl_calls.txt` (343 entries: 330 namespaced methods and 13 wrappers).',
        '',
        '## What a category means',
        '',
        '- **IMPLEMENTED**: hand-written source constructs the request, and the checklist records no limitation. The',
        '  evidence column says whether a passing test names it. Rows marked "source only" have no test.',
        '- **IMPLEMENTED_WITH_LIMITATIONS**: the request is constructed, and the checklist row lists the remaining limits.',
        '- **PARTIAL**: the checklist says the method is partly implemented. No row is in this category at present.',
        '- **NOT_IMPLEMENTED**: no request is constructed in hand-written source.',
        '- **NOT_APPLICABLE**: the method cannot apply to this client. No row is in this category at present.',
        '- **SCHEMA_BLOCKED**: the TL function is not in the layer 233 schema, so it cannot be sent.',
        '',
    ]
    if not results:
        out += ['**Test results:** none supplied. Rows with tests show `not run` until the test binaries are run and their',
                'gtest XML is passed with `--results`.', '']
    out += ['## Summary', '', '| Category | Count |', '|---|---|']
    for k in order:
        out.append(f'| {k} | {counts.get(k, 0)} |')
    out += [f'| **Total namespaced methods** | **{len(namespaced)}** |', '']
    if evidence_counts:
        out += ['Evidence behind the IMPLEMENTED rows:', '', '| Evidence | Count |', '|---|---|']
        for k in sorted(evidence_counts):
            out.append(f'| {k} | {evidence_counts[k]} |')
        out.append('')
    out += [f'Wrapper entries, not counted above ({len(wrappers)}): ' + ', '.join('`' + w + '`' for w in wrappers) + '.',
            '', '## Per-method table', '',
            '| Method | In schema | Request built in source | Checklist status | Tests naming it | Test result | Evidence | Category |',
            '|---|---|---|---|---|---|---|---|']
    for name, s, r, st, n, res, ev, cat in rows:
        out.append(f'| `{name}` | {s} | {r} | {st} | {n} | {res} | {ev} | {cat} |')
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', OUT)
print('namespaced', len(namespaced), 'wrappers', len(wrappers), 'results', len(results))
for k in sorted(counts):
    print(f'{counts[k]:4d}  {k}')
