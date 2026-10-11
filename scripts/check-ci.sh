#!/usr/bin/env bash
# pre-push gate for workflow files: YAML parses + build-step order correct.
# Usage: scripts/check-ci.sh (run before any push that touches .github/)
set -euo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

python3 - <<'EOF'
import yaml, sys, glob

files = sorted(glob.glob('.github/workflows/*.yml'))
if not files:
    print("check-ci: no workflows found"); sys.exit(1)

for f in files:
    d = yaml.safe_load(open(f))
    jobs = d.get('jobs', {})
    print(f"check-ci: {f} — parses OK, jobs: {list(jobs)}")

# Build job step order (the ordered-list class of bug hit 3x this project).
ci = yaml.safe_load(open('.github/workflows/ci.yml'))
steps = [s.get('name') for s in ci['jobs']['build']['steps']]
required = [
    'Install Qt',          # starts with matrix var; matched by prefix below
    'Vendor header-only deps',
    'Configure',
    'Build',
    'Test (ctest)',
]
names = [s or '' for s in steps]
pos = {name: i for i, name in enumerate(names)}
def idx(prefix):
    for i, n in enumerate(names):
        if n.startswith(prefix):
            return i
    return -1
order = [idx('Install Qt'), pos['Vendor header-only deps'], pos['Configure'], pos['Build'], pos['Test (ctest)']]
if -1 in order or order != sorted(order) or len(set(order)) != len(order):
    print(f"check-ci: FAIL — build step order broken: {steps}")
    sys.exit(1)

# Protected job names must exist byte-identically (branch-protection coupling).
assert ci['jobs']['license-audit']['name'] == 'license audit', "protected name drifted: license audit"
assert ci['jobs']['secrets-scan']['name'] == 'secrets scan', "protected name drifted: secrets scan"
print(f"check-ci: step order OK ({steps})")
print("check-ci: protected job names intact")

# deps/dependencies.json parses and its name set matches LICENSES.md
# (the ranged-PUT-clobbers-entry bug class: a dropped dep line would
# otherwise pass silently).
import json, re
manifest = json.load(open('deps/dependencies.json'))
names = {d['name'] for d in manifest['dependencies']}
lic_text = open('LICENSES.md').read()
runtime = re.search(r'## Runtime dependencies.*?##', lic_text, re.S).group(0)
buildtest = re.search(r'## Build / test dependencies.*?##', lic_text, re.S).group(0)
table_names = set(re.findall(r'^\| ([^|]+?) \|', runtime + buildtest, re.M))
table_names.discard('Dependency')  # header row
if names != table_names:
    print(f"check-ci: FAIL — manifest vs LICENSES.md mismatch:")
    print(f"  manifest only: {sorted(names - table_names)}")
    print(f"  LICENSES.md only: {sorted(table_names - names)}")
    sys.exit(1)
print(f"check-ci: manifest ({len(names)} deps) matches LICENSES.md")
EOF
echo "check-ci: PASS"
