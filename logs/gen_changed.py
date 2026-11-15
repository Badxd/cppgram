import os, hashlib, sys
base, work = sys.argv[1], sys.argv[2]
EXCL_DIRS = {'.git', 'build', 'dist'}
SELF = {'changed-files.txt'}
def walk(root):
    out = {}
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x not in EXCL_DIRS]
        for f in files:
            rel = os.path.relpath(os.path.join(d, f), root)
            out[rel] = os.path.join(d, f)
    return out
def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b''): h.update(chunk)
    return h.hexdigest()
b, w = walk(base), walk(work)
rows = []
for rel in sorted(set(b) | set(w)):
    if rel in SELF: continue
    hv = (lambda r: '(see SHA256SUMS.txt: it cannot record its own hash)') if rel == 'SHA256SUMS.txt' else (lambda r: sha(w[r]))
    if rel not in b: rows.append(('A', rel, hv(rel)))
    elif rel not in w: rows.append(('D', rel, '-'))
    elif sha(b[rel]) != sha(w[rel]): rows.append(('M', rel, hv(rel)))
print('# Changed files: work tree vs baseline 1cd09d0 (excludes build/, dist/, .git; changed-files.txt excludes itself)')
print('# status  path  sha256 of the work-tree file (A/M) or - (D)')
for s, rel, h in rows: print(f'{s}  {rel}  {h}')
print(f'# totals: added={sum(1 for r in rows if r[0]=="A")} modified={sum(1 for r in rows if r[0]=="M")} deleted={sum(1 for r in rows if r[0]=="D")}')
