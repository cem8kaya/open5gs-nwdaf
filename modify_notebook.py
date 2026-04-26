#!/usr/bin/env python3
"""
Script to modify 5g_ai_lab_daily_ops_v2.ipynb:
1. Fix section numbering in existing cells
2. Add 7 groups of new cells
"""

import json
import re

# ── Load cell content from the analysis file ─────────────────────────────────
# We read the variables directly from the .py file by executing it
import importlib.util, sys, os

# Execute the analysis file to get the cell content variables
analysis_file = '/home/user/open5gs-nwdaf/5g_daily_ops_analysis_and_new_cells.py'
spec = importlib.util.spec_from_file_location("analysis", analysis_file)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

CELL_A      = mod.CELL_A
CELL_A_FIX  = mod.CELL_A_FIX
CELL_B      = mod.CELL_B
CELL_C      = mod.CELL_C
CELL_C_PLOT = mod.CELL_C_PLOT
CELL_D      = mod.CELL_D
CELL_E      = mod.CELL_E
CELL_F      = mod.CELL_F
CELL_G      = mod.CELL_G
CELL_G_EDA  = mod.CELL_G_EDA

# ── Helper: strip surrounding r""" / """ delimiters and leading/trailing whitespace
def strip_delimiters(s):
    """Strip r\"\"\" or \"\"\" delimiters and surrounding whitespace/newlines."""
    s = s.strip()
    # Remove leading r""" or """
    if s.startswith('r"""'):
        s = s[4:]
    elif s.startswith('"""'):
        s = s[3:]
    # Remove trailing """
    if s.endswith('"""'):
        s = s[:-3]
    # Strip surrounding whitespace/newlines
    s = s.strip('\n')
    return s

# ── Helper: convert a string of code/text to notebook source list ──────────
def to_source(text):
    """Split text into list of lines. Each line (except last) ends with \\n."""
    lines = text.split('\n')
    result = []
    for i, line in enumerate(lines):
        if i < len(lines) - 1:
            result.append(line + '\n')
        else:
            # Last line: only add if non-empty (or it's the only line)
            if line or len(lines) == 1:
                result.append(line)
    return result

# ── Helper: create a markdown cell ──────────────────────────────────────────
def markdown_cell(text):
    return {
        "cell_type": "markdown",
        "metadata": {},
        "source": to_source(text)
    }

# ── Helper: create a code cell ───────────────────────────────────────────────
def code_cell(text):
    return {
        "cell_type": "code",
        "metadata": {},
        "source": to_source(text),
        "outputs": [],
        "execution_count": None
    }

# ── Load notebook ─────────────────────────────────────────────────────────────
nb_path = '/home/user/open5gs-nwdaf/5g_ai_lab_daily_ops_v2.ipynb'
with open(nb_path) as f:
    nb = json.load(f)

cells = nb['cells']
print(f"Loaded notebook with {len(cells)} cells")

# ═════════════════════════════════════════════════════════════════════════════
# PART 1 — Fix section numbering (patch existing cells)
# ═════════════════════════════════════════════════════════════════════════════

def get_source_str(cell):
    """Return cell source as a single string."""
    src = cell.get('source', [])
    if isinstance(src, list):
        return ''.join(src)
    return src

def set_source(cell, text):
    """Set cell source from a string."""
    cell['source'] = to_source(text)

# Cell 90: # 15. Quick Reference Card → # 14. Quick Reference Card
src90 = get_source_str(cells[90])
print(f"\nCell 90 before: {repr(src90[:60])}")
src90_new = src90.replace('# 15. Quick Reference Card', '# 14. Quick Reference Card')
set_source(cells[90], src90_new)
print(f"Cell 90 after:  {repr(get_source_str(cells[90])[:60])}")

# Cell 93: ## 13. NWDAF C++ Verification → # 15. NWDAF C++ Verification
src93 = get_source_str(cells[93])
print(f"\nCell 93 before: {repr(src93[:60])}")
src93_new = src93.replace('## 13. NWDAF C++ Verification', '# 15. NWDAF C++ Verification')
set_source(cells[93], src93_new)
print(f"Cell 93 after:  {repr(get_source_str(cells[93])[:60])}")

# Cell 96: ## 14. Web UI Integration & Comparison → # 16. Web UI Integration & Comparison
src96 = get_source_str(cells[96])
print(f"\nCell 96 before: {repr(src96[:60])}")
src96_new = src96.replace('## 14. Web UI Integration & Comparison', '# 16. Web UI Integration & Comparison')
set_source(cells[96], src96_new)
print(f"Cell 96 after:  {repr(get_source_str(cells[96])[:60])}")

print(f"\nPart 1 (numbering fixes) done. Cell count still: {len(cells)}")

# ═════════════════════════════════════════════════════════════════════════════
# PART 2 — Add new cells
# Process from BOTTOM to TOP to keep original indices stable
# ═════════════════════════════════════════════════════════════════════════════

# ─── Insertion Group 4 — Insert new markdown cell BEFORE original cell 99 ────
# "# 17. NRF & Integration Validation\n" before cell 99
new_cell_99 = markdown_cell('# 17. NRF & Integration Validation\n')
cells.insert(99, new_cell_99)
print(f"\nInserted §17 header before original cell 99. Cell count: {len(cells)}")

# ─── Insertion Group 3 — after original cell 70 ──────────────────────────────
# (10.3 through 10.6b — 12 cells)
# Insert AFTER cell 70, so we insert at index 71 (and each successive one after that)

group3_cells = [
    # 1
    markdown_cell(
        '## 10.3 — Scenario D: SLA Violation via Real 5G Congestion\n'
        '\n'
        '**[BUG-04 FIX]** The original Scenario D used `iperf3` on the host\'s default route, bypassing the 5G core. This fixed version routes a ping flood through `uesimtun0` via `nr-binder`, so NWDAF\'s ogstun counters actually register the congestion.\n'
    ),
    # 2
    code_cell(strip_delimiters(CELL_C)),
    # 3
    markdown_cell('### 10.3b — Scenario D Results Plot\n'),
    # 4
    code_cell(strip_delimiters(CELL_C_PLOT)),
    # 5
    markdown_cell(
        '## 10.4 — Jitter & Latency Analysis (Real RTT)\n'
        '\n'
        '**[BUG-05 FIX]** Replaces the mock `np.random.normal` placeholder with actual RTT values parsed from `ping` output through `uesimtun0`. Includes 3GPP TS 22.261 URLLC/eMBB latency classification.\n'
    ),
    # 6
    code_cell(strip_delimiters(CELL_D)),
    # 7
    markdown_cell(
        '## 10.5 — Multi-UE Load Test\n'
        '\n'
        '**[OBS-05]** Activates the second subscriber (IMSI `999700000000002`) that was provisioned but never used. Runs concurrent DL (UE1) and UL (UE2) traffic to stress SMF/UPF and increase ML dataset diversity.\n'
    ),
    # 8
    code_cell(strip_delimiters(CELL_F)),
    # 9
    markdown_cell(
        '## 10.6 — PCAP Dataset Export for ML Pipeline\n'
        '\n'
        'Captures traffic on `ogstun` (the UPF-visible interface, bypassing the gtp5g limitation on uesimtun0 RX) and exports a labeled PCAP + CSV for the ML pipeline.\n'
    ),
    # 10
    code_cell(strip_delimiters(CELL_G)),
    # 11
    markdown_cell('### 10.6b — Dataset EDA & Label Balance\n'),
    # 12
    code_cell(strip_delimiters(CELL_G_EDA)),
]

insert_pos = 71  # after original cell 70
for i, cell in enumerate(group3_cells):
    cells.insert(insert_pos + i, cell)

print(f"Inserted Group 3 (12 cells after original cell 70). Cell count: {len(cells)}")

# ─── Insertion Group 2 — after original cell 64 ──────────────────────────────
# (9.3 Anomaly Baseline Health Check — 2 cells)
group2_cells = [
    # 1
    markdown_cell(
        '## 9.3 — Anomaly Baseline Health Check\n'
        '\n'
        '**[OBS-01]** Guards against the BASELINE_TOO_LOW condition where the Isolation Forest was trained on idle/zero-traffic data, causing 97%+ false-positive anomaly rates. Run after retraining to verify model health.\n'
    ),
    # 2
    code_cell(strip_delimiters(CELL_E)),
]

insert_pos = 65  # after original cell 64
for i, cell in enumerate(group2_cells):
    cells.insert(insert_pos + i, cell)

print(f"Inserted Group 2 (2 cells after original cell 64). Cell count: {len(cells)}")

# ─── Insertion Group 1 — after original cell 57 ──────────────────────────────
# (8.5 UUID Reconciliation, fix block, 8.6 NRF Audit — 6 cells)
group1_cells = [
    # 1
    markdown_cell(
        '## 8.5 — NWDAF UUID Reconciliation & Heartbeat Fix\n'
        '\n'
        '**[BUG-01]** Diagnoses the UUID mismatch between `nwdaf.yaml` and the running C++ binary. A mismatched UUID causes the NRF to reject PATCH heartbeats with HTTP 400, creating a re-registration storm.\n'
        '\n'
        'Run the diagnostic cell first, then uncomment the fix block if a mismatch is confirmed.\n'
    ),
    # 2
    code_cell(strip_delimiters(CELL_A)),
    # 3
    markdown_cell('### 8.5b — UUID Fix Block (run after confirming mismatch)\n'),
    # 4
    code_cell(strip_delimiters(CELL_A_FIX)),
    # 5
    markdown_cell(
        '## 8.6 — NRF Registration Audit\n'
        '\n'
        '**[BUG-02 / BUG-03]** Audits all NRF-registered NF instances, detects stale/orphaned registrations, and shows the heartbeat error rate. Run this when nrfd shows elevated error rates.\n'
    ),
    # 6
    code_cell(strip_delimiters(CELL_B)),
]

insert_pos = 58  # after original cell 57
for i, cell in enumerate(group1_cells):
    cells.insert(insert_pos + i, cell)

print(f"Inserted Group 1 (6 cells after original cell 57). Cell count: {len(cells)}")

# ═════════════════════════════════════════════════════════════════════════════
# SAVE
# ═════════════════════════════════════════════════════════════════════════════
nb['cells'] = cells
with open(nb_path, 'w') as f:
    json.dump(nb, f, indent=1, ensure_ascii=False)

print(f"\n✅ Saved. Final cell count: {len(cells)}")

# ═════════════════════════════════════════════════════════════════════════════
# VERIFY — Print section headers
# ═════════════════════════════════════════════════════════════════════════════
print("\n" + "="*70)
print("SECTION HEADERS (markdown cells starting with #)")
print("="*70)
for i, cell in enumerate(nb['cells']):
    if cell['cell_type'] == 'markdown':
        src = ''.join(cell.get('source', []))
        first_line = src.split('\n')[0].strip()
        if first_line.startswith('#'):
            print(f"  [{i:3d}] {first_line}")
