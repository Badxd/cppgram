import os, re, sys, json
root = sys.argv[1]
PATS = {
 'telegram_bot_token': re.compile(rb'\b\d{8,10}:[A-Za-z0-9_-]{35}\b'),
 'telethon_session_string': re.compile(rb'\b1[A-Za-z0-9+/=]{200,}\b'),
 'pem_private_key': re.compile(rb'-----BEGIN [A-Z ]*PRIVATE KEY-----'),
 'github_token': re.compile(rb'\b(ghp_|gho_|ghu_|ghs_|github_pat_)[A-Za-z0-9_]{20,}'),
 'aws_access_key': re.compile(rb'\bAKIA[0-9A-Z]{16}\b'),
 'api_hash_assignment': re.compile(rb'(?i)api[_-]?hash\W{0,6}[0-9a-f]{32}\b'),
 'mtproxy_secret_hex': re.compile(rb'(?i)\b(dd|ee)[0-9a-f]{32}\b'),
 'phone_number_e164': re.compile(rb'\+\d{10,15}\b'),
 'ipv4_public': re.compile(rb'\b(?!10\.|127\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.|0\.|255\.)(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})\b'),
}
SKIP_EXT = {'.o', '.a', '.so', '.png', '.jpg', '.gz', '.zip'}
findings = {}
files_scanned = 0; binaries = []
for d, dirs, files in os.walk(root):
    dirs[:] = [x for x in dirs if x != '.git']
    for f in files:
        p = os.path.join(d, f); rel = os.path.relpath(p, root)
        ext = os.path.splitext(f)[1].lower()
        with open(p, 'rb') as fh: data = fh.read()
        if b'\x00' in data[:8192]: binaries.append(rel)
        files_scanned += 1
        for name, rx in PATS.items():
            for m in rx.finditer(data):
                line = data.count(b'\n', 0, m.start()) + 1
                findings.setdefault(name, []).append(f'{rel}:{line}')
print(json.dumps({'files_scanned': files_scanned, 'binary_files': len(binaries), 'binary_list': binaries}, indent=1))
for k in PATS:
    v = findings.get(k, [])
    print(f'{k}: {len(v)} hit(s)', ('; '.join(v[:12]) + (' ...' if len(v) > 12 else '')) if v else '')
