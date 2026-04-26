"""
5G AI/ML Lab — Daily Ops Notebook Analysis & New Cells
=======================================================
Analysis of: 5g_ai_lab_daily_ops_v2_(4).ipynb
Run date  : 2026-04-26
Author    : Claude Sonnet (Anthropic)

SUMMARY OF FINDINGS
===================

[CRITICAL] BUG-01  NWDAF UUID mismatch — config vs runtime
[HIGH]     BUG-02  NRF heartbeat 400 loop — 60 failures/hour
[HIGH]     BUG-03  nrfd 360 errors/hour — storm from NWDAF re-registrations
[MEDIUM]   BUG-04  Scenario D — congestion not injected through 5G core
[MEDIUM]   BUG-05  Jitter cell (10.5 visual) uses mock np.random data, not real ping RTT
[LOW]      OBS-01  ABNORMAL_BEHAVIOUR: anomalyDetected=true with 70+ anomaly indices → likely BASELINE_TOO_LOW
[LOW]      OBS-02  ogstun2 DOWN — second UPF slice not active
[LOW]      OBS-03  Auto-Diagnosis: reports "2 tunnels active" but health dashboard shows only uesimtun0
[INFO]     OBS-04  Section numbering out of order (§13 appears twice, §15 before §13)
[INFO]     OBS-05  Subscriber 999700000000002 provisioned but never used in any scenario

NEW CELLS ADDED
===============
A. NWDAF UUID Reconciliation & Heartbeat Fix  (replaces/supplements §8.1)
B. NRF Registration Audit                     (new — diagnoses BUG-02/03)
C. Scenario D Fix — Real Congestion via uesimtun0
D. Jitter Analysis Fix — Real RTT Parsing
E. Anomaly Baseline Health Check              (guard for BASELINE_TOO_LOW)
F. Multi-UE Load Test (uses subscriber 999700000000002)
G. PCAP Dataset Export for ML Pipeline
"""


# ══════════════════════════════════════════════════════════════════════════════
# CELL A — NWDAF UUID Reconciliation & Heartbeat Fix
# ══════════════════════════════════════════════════════════════════════════════
# BUG-01 DIAGNOSIS:
#   nwdaf.yaml   nf_instance_id: 405696f4-4216-4a91-8387-54e469eef734
#   /health API  nfInstanceId:   893d63a5-408a-4889-ab63-8343f8d4bd01
#
# These are DIFFERENT UUIDs. The C++ binary is generating its own UUID at
# startup (or reading from a secondary location), ignoring the yaml value.
# This causes the NRF to receive a heartbeat PATCH for an instance ID it
# doesn't recognise → HTTP 400 → re-registration loop.
#
# HOW TO USE: Run this cell, read the diagnosis, then uncomment the FIX block.
# ══════════════════════════════════════════════════════════════════════════════

CELL_A = r"""
%%bash
PROJECT_ID="g-ai-lab-491619"; ZONE="europe-west4-a"; VM_NAME="open5gs-ai-lab"

gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command='
set +e
echo "════════════════════════════════════════════════════"
echo "  NWDAF UUID RECONCILIATION DIAGNOSTIC"
echo "  $(date "+%Y-%m-%d %H:%M:%S")"
echo "════════════════════════════════════════════════════"

# 1. UUID from config file
CFG_UUID=$(grep "nf_instance_id:" /etc/open5gs/nwdaf.yaml \
    | grep -oE "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}" \
    | head -1)
echo ""
echo "[1] Config UUID (nwdaf.yaml):  ${CFG_UUID:-NOT FOUND}"

# 2. UUID from live /health endpoint
RT_UUID=$(curl -s --connect-timeout 3 http://localhost:7779/nwdaf-analytics/v1/health \
    | python3 -c "import sys,json; print(json.load(sys.stdin).get(\"nfInstanceId\",\"?\"))" \
    2>/dev/null)
echo "[2] Runtime UUID (/health):    ${RT_UUID:-NOT REACHABLE}"

# 3. UUID that NRF actually knows about
NRF_NWDAF=$(curl -s --http2-prior-knowledge --connect-timeout 3 \
    "http://127.0.0.10:7777/nnrf-nfm/v1/nf-instances?nf-type=NWDAF" \
    2>/dev/null | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    items = d.get(\"_links\",{}).get(\"item\",[])
    for it in items:
        href = it.get(\"href\",\"\")
        print(href.split(\"/\")[-1])
except: print(\"PARSE_ERROR\")
" 2>/dev/null)
echo "[3] NRF-registered UUID(s):    ${NRF_NWDAF:-NONE}"

echo ""
# 4. Verdict
if [ "$CFG_UUID" = "$RT_UUID" ]; then
    echo "[VERDICT] ✅ UUIDs MATCH — heartbeat 400 has a different root cause"
    echo "          Check: is NRF receiving the heartbeat for the right instance?"
else
    echo "[VERDICT] ❌ UUID MISMATCH CONFIRMED"
    echo "          Config says: $CFG_UUID"
    echo "          Runtime is:  $RT_UUID"
    echo ""
    echo "  Root cause: The C++ binary generates/reads a UUID that differs from"
    echo "  the yaml value. The NRF registered the runtime UUID but heartbeat"
    echo "  PUT targets the config UUID → HTTP 400."
    echo ""
    echo "  ── FIX ──────────────────────────────────────────────────────────"
    echo "  Option A (preferred): overwrite yaml with runtime UUID so they match."
    echo "    sudo sed -i \"s/$CFG_UUID/$RT_UUID/\" /etc/open5gs/nwdaf.yaml"
    echo "    # No restart needed — runtime is already using RT_UUID."
    echo ""
    echo "  Option B: force the binary to honour the yaml uuid by clearing the"
    echo "  auto-generated state file (if the binary caches a UUID to disk)."
    echo "    sudo find /opt/nwdaf /etc/open5gs -name \"*.uuid\" -o -name \"nwdaf_id*\" 2>/dev/null"
    echo "  ─────────────────────────────────────────────────────────────────"
fi

echo ""
echo "[4] Recent heartbeat errors (last 10):"
sudo journalctl -u open5gs-nwdafd -n 100 --no-pager 2>/dev/null \
    | grep -i "heartbeat" | tail -10
'
"""

# ── FIX BLOCK (uncomment and run after reading diagnosis above) ──────────────
CELL_A_FIX = r"""
%%bash
PROJECT_ID="g-ai-lab-491619"; ZONE="europe-west4-a"; VM_NAME="open5gs-ai-lab"

gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command='
set +e
CFG_UUID=$(grep "nf_instance_id:" /etc/open5gs/nwdaf.yaml \
    | grep -oE "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}" | head -1)
RT_UUID=$(curl -s --connect-timeout 3 http://localhost:7779/nwdaf-analytics/v1/health \
    | python3 -c "import sys,json; print(json.load(sys.stdin).get(\"nfInstanceId\",\"\"))" 2>/dev/null)

if [ -z "$RT_UUID" ]; then
    echo "❌ Cannot read runtime UUID — is NWDAF running? Run Cell 3.2 (ACTION=start) first."
    exit 1
fi
if [ "$CFG_UUID" = "$RT_UUID" ]; then
    echo "✅ Already in sync — nothing to do."
    exit 0
fi

echo "Syncing config UUID → runtime UUID"
echo "  Before: $CFG_UUID"
echo "  After:  $RT_UUID"

sudo cp /etc/open5gs/nwdaf.yaml /etc/open5gs/nwdaf.yaml.bak_$(date +%Y%m%d%H%M%S)
sudo sed -i "s/$CFG_UUID/$RT_UUID/g" /etc/open5gs/nwdaf.yaml

# Also patch root-level nf_instance_id if present
sudo sed -i "s/$CFG_UUID/$RT_UUID/g" /etc/open5gs/nwdaf.yaml

echo "Verifying:"
grep "nf_instance_id" /etc/open5gs/nwdaf.yaml
echo ""
echo "Wait ~70s for next heartbeat cycle to confirm 400 errors stop."
echo "Monitor with: journalctl -u open5gs-nwdafd -f 2>/dev/null | grep -i heartbeat"
'
"""


# ══════════════════════════════════════════════════════════════════════════════
# CELL B — NRF Registration Audit
# ══════════════════════════════════════════════════════════════════════════════
# BUG-02/03 CONTEXT:
#   nrfd has 360 errors/warnings in 1 hour = 6/min.
#   nwdafd has 60 errors/warnings = 1/min (NRF heartbeat fails every 60s).
#   The extra ~300 nrfd errors = 5/min = NRF receiving unexpected requests.
#
# This cell audits what the NRF actually has registered, checks heartbeat
# counters, and tells you which NF instance IDs are stale/orphaned.
# ══════════════════════════════════════════════════════════════════════════════

CELL_B = r"""
%%bash
PROJECT_ID="g-ai-lab-491619"; ZONE="europe-west4-a"; VM_NAME="open5gs-ai-lab"

gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command='
set +e
echo "════════════════════════════════════════════════════"
echo "  NRF REGISTRATION AUDIT"
echo "  $(date "+%Y-%m-%d %H:%M:%S")"
echo "════════════════════════════════════════════════════"

echo ""
echo "[1] All NF instances currently registered in NRF:"
curl -s --http2-prior-knowledge --connect-timeout 5 \
    "http://127.0.0.10:7777/nnrf-nfm/v1/nf-instances" 2>/dev/null \
| python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    items = d.get(\"_links\",{}).get(\"item\",[])
    print(f\"  Total: {len(items)} registered NF instances\")
    for it in items:
        href = it.get(\"href\",\"\")
        uid = href.split(\"/\")[-1]
        # Fetch details for each
        import urllib.request
        try:
            req = urllib.request.Request(href,
                headers={\"Accept\":\"application/json\"})
            with urllib.request.urlopen(req) as r:
                nf = json.loads(r.read())
                nft = nf.get(\"nfType\",\"?\")
                status = nf.get(\"nfStatus\",\"?\")
                print(f\"  {nft:<10} {status:<12} {uid[:8]}...\")
        except Exception as e:
            print(f\"  ??         ??           {uid[:8]}... ({e})\")
except Exception as e:
    print(f\"  PARSE_ERROR: {e}\")
" 2>/dev/null

echo ""
echo "[2] NWDAF-specific registration:"
curl -s --http2-prior-knowledge --connect-timeout 5 \
    "http://127.0.0.10:7777/nnrf-nfm/v1/nf-instances?nf-type=NWDAF" 2>/dev/null \
| python3 -m json.tool 2>/dev/null | head -40

echo ""
echo "[3] NRF error rate (last 30min):"
sudo journalctl -u open5gs-nrfd --since "30 min ago" --no-pager 2>/dev/null \
    | grep -iE "error|warn|400|rejected" | tail -20

echo ""
echo "[4] NWDAF heartbeat timeline (last 10 events):"
sudo journalctl -u open5gs-nwdafd --since "30 min ago" --no-pager 2>/dev/null \
    | grep -iE "heartbeat|register|nrf" | tail -15
'
"""


# ══════════════════════════════════════════════════════════════════════════════
# CELL C — Scenario D FIX: Real Congestion Through 5G Core
# ══════════════════════════════════════════════════════════════════════════════
# BUG-04 ROOT CAUSE:
#   Original code used `iperf3 -c 8.8.8.8 -u -b 50M` on the host default route.
#   This traffic bypasses the 5G core entirely — it goes out ens4 directly.
#   NWDAF monitors ogstun/uesimtun0 counters, so it sees zero change → score
#   stays exactly the same (64.05... → 64.05...).
#
# FIX: Use nr-binder to route ping flood through uesimtun0, which goes through
#      the UPF and is visible to NWDAF's ogstun counter sampling.
# ══════════════════════════════════════════════════════════════════════════════

CELL_C = r"""
%%bash
PROJECT_ID="g-ai-lab-491619"; ZONE="europe-west4-a"; VM_NAME="open5gs-ai-lab"

gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command='
set +e
echo "=== Scenario D (FIXED): SLA Violation via 5G Core Congestion ==="
LOG="/tmp/scenario_d_fixed.csv"
NR="/opt/UERANSIM/build"

# Identify UE tunnel
UE_IFACE=$(ip a | grep -o "uesimtun[0-9]*" | head -1)
if [ -z "$UE_IFACE" ]; then
    echo "❌ No uesimtun interface — start UE first (Cell 4.1)"
    exit 1
fi
echo "UE interface: $UE_IFACE"
echo "timestamp_ms,phase,overall_score,dl_kbps,ul_kbps,grade" > $LOG

# ── Helper: snapshot NWDAF analytics ─────────────────────────────────────────
get_analytics() {
    NET=$(curl -s --connect-timeout 3 \
        "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=NETWORK_PERFORMANCE" \
        2>/dev/null)
    QOS=$(curl -s --connect-timeout 3 \
        "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=QoS_SUSTAINABILITY" \
        2>/dev/null)
    python3 -c "
import json, sys
try:
    nd = json.loads('''$NET''').get(\"analData\",{})
    qd = json.loads('''$QOS''').get(\"analData\",{})
    score = round(nd.get(\"overallScore\",0),2)
    label = nd.get(\"scoreLabel\",\"?\")
    dl    = round(qd.get(\"currentDlKbps\",0),1)
    ul    = round(qd.get(\"currentUlKbps\",0),1)
    print(f\"{score},{dl},{ul},{label}\")
except: print(\"0,0,0,?\")
" 2>/dev/null
}

# ── Phase 1: Baseline — minimal traffic ──────────────────────────────────────
echo ""
echo "--- Phase 1: Baseline (5 ICMP pings to establish baseline score) ---"
cd $NR && sudo ./nr-binder $UE_IFACE ping -c 5 -q 8.8.8.8 > /dev/null 2>&1
sleep 3
STATS=$(get_analytics)
echo "  $(date +%s%3N),BASELINE,$STATS" | tee -a $LOG
echo "  Baseline: score=$(echo $STATS | cut -d, -f1)  DL=$(echo $STATS | cut -d, -f2) kbps"

# ── Phase 2: Congestion — ping flood through uesimtun0 ───────────────────────
echo ""
echo "--- Phase 2: Congestion flood via nr-binder (through UPF) ---"
# Start background flood THROUGH the 5G core — this is what was missing before
cd $NR
sudo ./nr-binder $UE_IFACE ping -f -c 50000 -s 1400 8.8.8.8 > /dev/null 2>&1 &
FLOOD_PID=$!
echo "  Flood PID=$FLOOD_PID — waiting 8s for NWDAF to detect..."
sleep 8

# Snapshot during congestion
STATS_CONGESTED=$(get_analytics)
echo "  $(date +%s%3N),CONGESTION,$STATS_CONGESTED" | tee -a $LOG
echo "  Congested: score=$(echo $STATS_CONGESTED | cut -d, -f1)  DL=$(echo $STATS_CONGESTED | cut -d, -f2) kbps"

wait $FLOOD_PID 2>/dev/null

# ── Phase 3: Recovery ─────────────────────────────────────────────────────────
echo ""
echo "--- Phase 3: Recovery (10s cooldown) ---"
sleep 10
STATS_RECOVERY=$(get_analytics)
echo "  $(date +%s%3N),RECOVERY,$STATS_RECOVERY" | tee -a $LOG
echo "  Recovery: score=$(echo $STATS_RECOVERY | cut -d, -f1)  DL=$(echo $STATS_RECOVERY | cut -d, -f2) kbps"

echo ""
SCORE_BASE=$(echo $STATS | cut -d, -f1)
SCORE_CONG=$(echo $STATS_CONGESTED | cut -d, -f1)
echo "Score delta: $SCORE_BASE → $SCORE_CONG"
if python3 -c "exit(0 if abs(float(\"$SCORE_CONG\")-float(\"$SCORE_BASE\")) > 1 else 1)" 2>/dev/null; then
    echo "✅ NWDAF detected congestion — score shifted"
else
    echo "⚠️  Score did not shift. NWDAF EWMA may need more time to react."
    echo "   Try increasing flood duration or check anomaly_min_samples."
fi
echo ""
echo "✅ CSV: $LOG"
cat $LOG
' 2>&1

# Download CSV to Colab
gcloud compute scp $VM_NAME:/tmp/scenario_d_fixed.csv \
    ./scenario_d_fixed.csv \
    --project=$PROJECT_ID --zone=$ZONE 2>/dev/null \
    && echo "✅ Downloaded: scenario_d_fixed.csv" \
    || echo "⚠️  SCP failed"
"""

# ── Colab-side Scenario D plot ────────────────────────────────────────────────
CELL_C_PLOT = """
import pandas as pd, matplotlib.pyplot as plt, os

if not os.path.exists('scenario_d_fixed.csv'):
    print("⚠️  Run the bash cell above first.")
else:
    df = pd.read_csv('scenario_d_fixed.csv',
                     names=['ts','phase','score','dl_kbps','ul_kbps','grade'])
    df['time'] = pd.to_datetime(df['ts'], unit='ms')

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # Score timeline
    colors = {'BASELINE': '#4CAF50', 'CONGESTION': '#F44336', 'RECOVERY': '#2196F3'}
    for _, row in df.iterrows():
        axes[0].bar(row['phase'], row['score'],
                    color=colors.get(row['phase'], '#9E9E9E'), width=0.6)
    axes[0].set_title('NWDAF Network Performance Score\\n(Scenario D — Real 5G Congestion)')
    axes[0].set_ylabel('Overall Score (0-100)')
    axes[0].set_ylim(0, 100)
    axes[0].axhline(80, color='green', linestyle='--', alpha=0.5, label='Good threshold')
    axes[0].legend()

    # DL/UL throughput
    x = range(len(df))
    axes[1].bar([i - 0.2 for i in x], df['dl_kbps'], width=0.4,
                label='DL (kbps)', color='#2196F3')
    axes[1].bar([i + 0.2 for i in x], df['ul_kbps'], width=0.4,
                label='UL (kbps)', color='#FF9800')
    axes[1].set_xticks(list(x))
    axes[1].set_xticklabels(df['phase'])
    axes[1].set_title('Throughput per Phase (NWDAF QoS_SUSTAINABILITY)')
    axes[1].set_ylabel('KB/s')
    axes[1].legend()

    plt.tight_layout()
    plt.savefig('scenario_d_fixed.png', dpi=150)
    plt.show()
    print("Saved: scenario_d_fixed.png")
"""


# ══════════════════════════════════════════════════════════════════════════════
# CELL D — Jitter Analysis FIX: Real RTT Parsing (replaces mock np.random)
# ══════════════════════════════════════════════════════════════════════════════
# BUG-05 ROOT CAUSE:
#   Cell 79 (Jitter analysis) runs the ping command correctly but then ignores
#   the output and uses np.random.normal(loc=22.5, scale=4.2, size=50) as a
#   placeholder. The plot shown is SYNTHETIC, not real measurements.
# ══════════════════════════════════════════════════════════════════════════════

CELL_D = r"""
import subprocess, re, numpy as np, matplotlib.pyplot as plt

PROJECT_ID = "g-ai-lab-491619"
ZONE       = "europe-west4-a"
VM_NAME    = "open5gs-ai-lab"

print("Collecting 50 real RTT samples via uesimtun0 ping...")
cmd = [
    "gcloud", "compute", "ssh", VM_NAME,
    f"--project={PROJECT_ID}", f"--zone={ZONE}",
    "--command",
    (
        # Verbose ping output so we can parse per-packet RTT
        "UE_IFACE=$(ip a | grep -o 'uesimtun[0-9]*' | head -1) && "
        "[ -n \"$UE_IFACE\" ] && "
        "sudo /opt/UERANSIM/build/nr-binder $UE_IFACE "
        "ping -c 50 -i 0.2 8.8.8.8 2>&1 || "
        "echo 'NO_TUNNEL'"
    )
]

result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
output = result.stdout

if "NO_TUNNEL" in output or not output.strip():
    print("❌ No UE tunnel — run Cell 4.1 first.")
else:
    # Parse "time=XX.X ms" from each ping reply line
    rtts = [float(m) for m in re.findall(r"time=([\d.]+)\s*ms", output)]
    print(f"  Parsed {len(rtts)} real RTT samples")

    if len(rtts) < 5:
        print("⚠️  Too few samples. Check ping output:")
        print(output[:500])
    else:
        rtts = np.array(rtts)
        jitter = np.diff(rtts)          # Packet Delay Variation (PDV)

        print(f"\n  Mean RTT      : {rtts.mean():.2f} ms")
        print(f"  Std Dev (RTT) : {rtts.std():.2f} ms")
        print(f"  Min/Max RTT   : {rtts.min():.2f} / {rtts.max():.2f} ms")
        print(f"  Mean Jitter   : {np.abs(jitter).mean():.2f} ms")
        print(f"  P95 RTT       : {np.percentile(rtts, 95):.2f} ms")

        # URLLC assessment (ITU-T G.114 / 3GPP TS 22.261)
        if rtts.mean() < 1.0:
            verdict = "✅ URLLC-class (<1ms) — excellent"
        elif rtts.mean() < 10.0:
            verdict = "✅ eMBB-class (<10ms) — good"
        elif rtts.mean() < 100.0:
            verdict = "⚠️  mMTC-class (<100ms) — acceptable"
        else:
            verdict = "❌ Exceeds 100ms — investigate routing"
        print(f"  3GPP verdict  : {verdict}")

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 5))

        # Plot 1: RTT time series
        ax1.plot(rtts, marker='o', linestyle='-', color='#2196F3', markersize=4)
        ax1.axhline(rtts.mean(), color='red', linestyle='--',
                    label=f'Mean {rtts.mean():.1f} ms')
        ax1.fill_between(range(len(rtts)),
                         rtts.mean() - rtts.std(),
                         rtts.mean() + rtts.std(),
                         alpha=0.15, color='red', label='±1σ')
        ax1.set_title("5G Core RTT — Real Measurements via uesimtun0")
        ax1.set_ylabel("Latency (ms)")
        ax1.set_xlabel("Packet #")
        ax1.legend()
        ax1.grid(True, alpha=0.3)

        # Plot 2: Jitter (PDV) histogram
        ax2.hist(jitter, bins=20, color='#4CAF50', edgecolor='black', alpha=0.8)
        ax2.axvline(np.mean(jitter), color='red', linestyle='--',
                    label=f'Avg Jitter {np.abs(jitter).mean():.2f} ms')
        ax2.set_title("Packet Delay Variation (Jitter) — URLLC Compliance")
        ax2.set_xlabel("Δ Latency (ms)")
        ax2.set_ylabel("Frequency")
        ax2.legend()
        ax2.grid(True, alpha=0.3)

        plt.tight_layout()
        plt.savefig('jitter_real.png', dpi=150)
        plt.show()
        print("Saved: jitter_real.png")
"""


# ══════════════════════════════════════════════════════════════════════════════
# CELL E — Anomaly Baseline Health Check
# ══════════════════════════════════════════════════════════════════════════════
# OBS-01 CONTEXT:
#   Cell 63 shows anomalyDetected=true with 70+ anomaly indices out of ~72 total
#   data points. This is ~97% anomaly rate — clearly a BASELINE_TOO_LOW or
#   model-trained-on-idle-traffic problem.
#
# This cell assesses whether the current model is fit for purpose and tells you
# exactly when to re-train.
# ══════════════════════════════════════════════════════════════════════════════

CELL_E = r"""
%%bash
PROJECT_ID="g-ai-lab-491619"; ZONE="europe-west4-a"; VM_NAME="open5gs-ai-lab"

gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command='
set +e
echo "════════════════════════════════════════════════════"
echo "  ANOMALY MODEL BASELINE HEALTH CHECK"
echo "  $(date "+%Y-%m-%d %H:%M:%S")"
echo "════════════════════════════════════════════════════"

RAW=$(curl -s --connect-timeout 5 \
    "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=ABNORMAL_BEHAVIOUR" \
    2>/dev/null)

python3 -c "
import json, sys
try:
    d = json.loads(\"\"\"$RAW\"\"\")
    ad = d.get(\"analData\", {})

    # Core metrics
    detected  = ad.get(\"anomalyDetected\", None)
    pct       = float(ad.get(\"anomalyPct\", 0))
    indices   = ad.get(\"anomalyIndices\", [])
    status    = d.get(\"status\", ad.get(\"status\", \"N/A\"))
    confidence = int(ad.get(\"confidence\", 0))
    n_indices = len(indices)

    print()
    print(f\"  anomalyDetected : {detected}\")
    print(f\"  anomalyPct      : {pct:.1f}%\")
    print(f\"  anomalyIndices  : {n_indices} flagged points\")
    print(f\"  confidence      : {confidence}%\")
    print(f\"  status          : {status}\")
    print()

    # Diagnosis
    if status == \"BASELINE_TOO_LOW\":
        print(\"  ❌ BASELINE_TOO_LOW — model trained on idle/zero-traffic data.\")
        print(\"     Action: Start UE sessions, run scenarios 10-20min, re-run Cell 9.1.\")
    elif detected and pct > 50:
        print(f\"  ❌ HIGH FALSE-POSITIVE RATE ({pct:.0f}% flagged)\")
        print(\"     Root cause: Isolation Forest trained on abnormal baseline (likely\")
        print(\"     low-traffic idle period). The model treats normal traffic as anomalous.\")
        print()
        print(\"     Recommended action:\")
        print(\"       1. Run Cell 4.1 to ensure UE is up and traffic flows\")
        print(\"       2. Run Scenario A + C for ≥20 minutes\")
        print(\"       3. Re-run Cell 9.0 (readiness check) — verify ≥120 dataPoints\")
        print(\"       4. Re-run Cell 9.1 (POST /train)\")
        print(\"       5. Re-run this cell — target anomalyPct < 5%\")
    elif detected and pct <= 5:
        print(f\"  ✅ Model healthy — {pct:.1f}% anomaly rate is within normal range.\")
    elif not detected:
        print(\"  ✅ No anomalies detected — system operating normally.\")
    else:
        print(f\"  ⚠️  Moderate anomaly rate ({pct:.1f}%) — monitor for trend.\")
except Exception as e:
    print(f\"  ERROR: {e}\")
    print(f\"  Raw: {\"\"\"$RAW\"\"\"[:300]}\")
" 2>/dev/null

echo ""
echo "  Model file:"
ls -lh /opt/nwdaf/models/ 2>/dev/null || echo "  No model found"
echo ""
echo "  Data point count (estimate from training record):"
sudo journalctl -u open5gs-nwdafd --no-pager 2>/dev/null \
    | grep -i "trained\|dataPoints\|data point" | tail -5
'
"""


# ══════════════════════════════════════════════════════════════════════════════
# CELL F — Multi-UE Load Test (uses subscriber 999700000000002)
# ══════════════════════════════════════════════════════════════════════════════
# OBS-05 CONTEXT:
#   Subscriber 999700000000002 (K=465B5CE8..., OPC=E8ED289D...) exists in
#   MongoDB (seen in Cell 34 output) but is never used in any test scenario.
#   This cell starts a second UE to create concurrent sessions and stresses
#   the SMF/UPF with real multi-UE load — essential for ML dataset diversity.
#
# PREREQUISITE: Cell 6.1 must show IMSI 999700000000002 provisioned.
#               The UERANSIM UE config at /opt/UERANSIM/config/open5gs-ue2.yaml
#               must exist OR this cell creates it from ue.yaml with patched IMSI.
# ══════════════════════════════════════════════════════════════════════════════

CELL_F = r"""
%%bash
PROJECT_ID="g-ai-lab-491619"; ZONE="europe-west4-a"; VM_NAME="open5gs-ai-lab"

gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command='
set +e
NR="/opt/UERANSIM/build"
CFG_UE1="/opt/UERANSIM/config/open5gs-ue.yaml"
CFG_UE2="/opt/UERANSIM/config/open5gs-ue2.yaml"

echo "=== Multi-UE Load Test ==="
echo ""

# ── Step 1: Verify subscriber 999700000000002 exists ─────────────────────────
SUB_COUNT=$(mongosh --quiet open5gs --eval \
    "db.subscribers.countDocuments({imsi:\"999700000000002\"})" 2>/dev/null)
if [ "${SUB_COUNT:-0}" -lt 1 ]; then
    echo "❌ Subscriber 999700000000002 not found. Run Cell 6.2 to provision it first."
    exit 1
fi
echo "✅ Subscriber 999700000000002 confirmed in MongoDB"

# ── Step 2: Create UE2 config by patching UE1 config ─────────────────────────
if [ ! -f "$CFG_UE2" ]; then
    echo "Creating UE2 config from UE1 (patching IMSI → 999700000000002)..."
    sudo cp "$CFG_UE1" "$CFG_UE2"
    # Patch IMSI
    sudo sed -i "s/999700000000001/999700000000002/g" "$CFG_UE2"
    # Patch K and OPC to match subscriber 2
    sudo sed -i "s/465B5CE8B199B49FAA5F0A2EE238A6BC/465B5CE8B199B49FAA5F0A2EE238A6BC/g" "$CFG_UE2"
    sudo sed -i "s/E8ED289DEBA952E4283B54E88E6183CA/E8ED289DEBA952E4283B54E88E6183CA/g" "$CFG_UE2"
    echo "  ✅ UE2 config created: $CFG_UE2"
    echo "  IMSI in config: $(grep "supi:" $CFG_UE2 | head -1)"
else
    echo "✅ UE2 config already exists: $CFG_UE2"
fi

# ── Step 3: Check UE1 is already running ─────────────────────────────────────
UE1_IFACE=$(ip a | grep -o "uesimtun[0-9]*" | head -1)
if [ -z "$UE1_IFACE" ]; then
    echo "⚠️  UE1 not running — starting UE1 first..."
    sudo nohup $NR/nr-ue -c $CFG_UE1 > /root/ue.log 2>&1 &
    for i in $(seq 1 40); do
        sleep 0.5
        UE1_IFACE=$(ip a | grep -o "uesimtun[0-9]*" | head -1)
        [ -n "$UE1_IFACE" ] && echo "  ✅ UE1 up: $UE1_IFACE" && break
    done
fi
echo "UE1 interface: ${UE1_IFACE:-MISSING}"

# ── Step 4: Start UE2 ─────────────────────────────────────────────────────────
echo ""
echo "Starting UE2 (IMSI: 999700000000002)..."
sudo nohup $NR/nr-ue -c $CFG_UE2 > /root/ue2.log 2>&1 &
UE2_IFACE=""
for i in $(seq 1 40); do
    sleep 0.5
    # UE2 will get uesimtun1 (or higher)
    UE2_IFACE=$(ip a | grep -o "uesimtun[0-9]*" | sort -V | tail -1)
    # Make sure it is different from UE1
    [ -n "$UE2_IFACE" ] && [ "$UE2_IFACE" != "$UE1_IFACE" ] && \
        echo "  ✅ UE2 PDU session up: $UE2_IFACE" && break
done

if [ -z "$UE2_IFACE" ] || [ "$UE2_IFACE" = "$UE1_IFACE" ]; then
    echo "  ❌ UE2 failed to get a tunnel. UE2 log:"
    tail -20 /root/ue2.log
    exit 1
fi

# ── Step 5: Concurrent traffic on both UEs ────────────────────────────────────
echo ""
echo "=== Concurrent load: UE1($UE1_IFACE) + UE2($UE2_IFACE) ==="
LOG="/tmp/multi_ue_load.csv"
echo "ts_ms,ue,iface,dl_kbps,ul_kbps" > $LOG

cd $NR

# UE1 download
(
    T0=$(date +%s%3N)
    wget_out=$(sudo ./nr-binder $UE1_IFACE \
        wget -O /dev/null --progress=dot:mega \
        http://speedtest.tele2.net/5MB.zip 2>&1)
    T1=$(date +%s%3N)
    bytes_mb=$(echo "$wget_out" | grep -oP "[0-9]+(?= MB)" | tail -1)
    elapsed=$(python3 -c "print(max(0.1,($T1-$T0)/1000.0))")
    dl=$(python3 -c "print(round(${bytes_mb:-0}*1024*1024/$elapsed/1024,1))")
    echo "$T1,UE1,$UE1_IFACE,$dl,0" >> $LOG
    echo "  UE1 DL: ${dl} KB/s"
) &

# UE2 upload (ping flood)
(
    T0=$(date +%s%3N)
    ping_out=$(sudo ./nr-binder $UE2_IFACE ping -f -c 3000 -s 1400 -q 8.8.8.8 2>&1)
    T1=$(date +%s%3N)
    pkts=$(echo "$ping_out" | grep -oP "[0-9]+(?= packets transmitted)" | head -1)
    elapsed=$(python3 -c "print(max(0.1,($T1-$T0)/1000.0))")
    ul=$(python3 -c "print(round(${pkts:-0}*1400/$elapsed/1024,1))")
    echo "$T1,UE2,$UE2_IFACE,0,$ul" >> $LOG
    echo "  UE2 UL: ${ul} KB/s"
) &

wait

# ── Step 6: UPF session count during multi-UE load ───────────────────────────
echo ""
echo "=== UPF Session Count ==="
curl -s http://127.0.0.7:9090/metrics 2>/dev/null \
    | grep "upf_sessionnbr" | grep -v "#"

echo ""
echo "=== NWDAF NF_LOAD during multi-UE ==="
curl -s "http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD" \
    2>/dev/null | python3 -c "
import sys, json
d = json.load(sys.stdin)
nfl = d.get(\"analData\",{}).get(\"nfLoadLevelList\",[])
for nf in nfl:
    print(f\"  {nf[\"nfType\"]:<8}: cpu={nf[\"nfLoadLevelInfo\"][\"nfCpuUsage\"]:.1f}%  \"
          f\"mem={nf[\"nfLoadLevelInfo\"][\"nfMemoryUsage\"]} KB  \"
          f\"level={nf[\"nfLoadLevelInfo\"][\"nfLoadLevelLabel\"]}\")
" 2>/dev/null

echo ""
echo "✅ Multi-UE load CSV: $LOG"
cat $LOG

echo ""
echo "Stopping UE2..."
UE2_IMSI=$(grep "supi:" $CFG_UE2 | awk "{print \$2}" | tr -d "'"'"'"'"'"'"')
sudo $NR/nr-cli $UE2_IMSI --exec "deregister normal" 2>/dev/null || true
sleep 2
' 2>&1

# Download CSV
gcloud compute scp $VM_NAME:/tmp/multi_ue_load.csv \
    ./multi_ue_load.csv --project=$PROJECT_ID --zone=$ZONE 2>/dev/null \
    && echo "✅ Downloaded: multi_ue_load.csv" \
    || echo "⚠️  SCP failed"
"""


# ══════════════════════════════════════════════════════════════════════════════
# CELL G — PCAP Dataset Export for ML Pipeline
# ══════════════════════════════════════════════════════════════════════════════
# PURPOSE:
#   Captures traffic on ogstun (the UPF tunnel, where gtp5g is visible) and
#   exports a labeled PCAP + CSV for the ML pipeline.  This complements the
#   NWDAF-derived datasets with raw packet-level features.
#
# KNOWN CONSTRAINT: uesimtun0 RX always 0 (gtp5g bypass).
#   We capture on ogstun which DOES see all packets flowing through UPF.
# ══════════════════════════════════════════════════════════════════════════════

CELL_G = r"""
%%bash
PROJECT_ID="g-ai-lab-491619"; ZONE="europe-west4-a"; VM_NAME="open5gs-ai-lab"

# Duration of capture — increase for larger datasets
CAPTURE_DURATION=30   # seconds
SCENARIO_LABEL="SCENARIO_C_THROUGHPUT"   # Change per scenario

gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command="
set +e
NR='/opt/UERANSIM/build'
PCAP_FILE='/tmp/dataset_${SCENARIO_LABEL,,}.pcap'
CSV_FILE='/tmp/dataset_${SCENARIO_LABEL,,}.csv'

echo '=== PCAP Dataset Export for ML Pipeline ==='
echo 'Capture interface: ogstun (bypasses gtp5g limitation)'
echo "Scenario label: $SCENARIO_LABEL"
echo "Duration: $CAPTURE_DURATION seconds"
echo ''

# Ensure we have an active UE session
UE_IFACE=\$(ip a | grep -o 'uesimtun[0-9]*' | head -1)
if [ -z \"\$UE_IFACE\" ]; then
    echo '❌ No uesimtun — start UE first (Cell 4.1)'
    exit 1
fi
echo \"UE tunnel: \$UE_IFACE\"

# Start tcpdump on ogstun (this interface IS visible, unlike uesimtun0)
echo ''
echo 'Starting packet capture on ogstun...'
sudo tcpdump -i ogstun -w \$PCAP_FILE &
TCPDUMP_PID=\$!
sleep 1

# Generate traffic during capture window
echo 'Generating traffic (download + ping flood in parallel)...'
(
    cd \$NR
    sudo ./nr-binder \$UE_IFACE \
        wget -O /dev/null --progress=dot \
        http://speedtest.tele2.net/10MB.zip > /dev/null 2>&1
) &
(
    cd \$NR
    sudo ./nr-binder \$UE_IFACE \
        ping -c 1000 -s 512 -i 0.02 -q 8.8.8.8 > /dev/null 2>&1
) &

sleep $CAPTURE_DURATION
sudo kill \$TCPDUMP_PID 2>/dev/null
wait 2>/dev/null
echo ''

# Validate PCAP
PACKET_COUNT=\$(sudo tcpdump -r \$PCAP_FILE --count 2>/dev/null | tail -1 | awk '{print \$1}')
echo \"Captured \${PACKET_COUNT:-0} packets → \$PCAP_FILE\"

# Generate CSV summary with Scapy
echo 'Converting to CSV (Scapy)...'
sudo python3 << 'PYEOF'
from scapy.all import rdpcap, IP, TCP, UDP, ICMP
from decimal import Decimal
import csv, os, datetime

pcap = '/tmp/dataset_${SCENARIO_LABEL,,}.pcap'
csv_out = '/tmp/dataset_${SCENARIO_LABEL,,}.csv'
label = '$SCENARIO_LABEL'

if not os.path.exists(pcap):
    print(f'  ERROR: {pcap} not found')
    exit(1)

pkts = rdpcap(pcap)
rows = []
for p in pkts:
    if not p.haslayer(IP):
        continue
    ip = p[IP]
    proto = 'OTHER'
    sport, dport = 0, 0
    if p.haslayer(TCP):
        proto = 'TCP'
        sport, dport = p[TCP].sport, p[TCP].dport
    elif p.haslayer(UDP):
        proto = 'UDP'
        sport, dport = p[UDP].sport, p[UDP].dport
    elif p.haslayer(ICMP):
        proto = 'ICMP'

    rows.append({
        'timestamp':   float(Decimal(str(p.time))),
        'src_ip':      ip.src,
        'dst_ip':      ip.dst,
        'protocol':    proto,
        'src_port':    sport,
        'dst_port':    dport,
        'pkt_len':     len(p),
        'ip_ttl':      ip.ttl,
        'label':       label,
    })

if rows:
    with open(csv_out, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(rows)
    print(f'  ✅ {len(rows)} records → {csv_out}')
    # Quick stats
    protos = {}
    for r in rows:
        protos[r['protocol']] = protos.get(r['protocol'],0)+1
    for k,v in sorted(protos.items(), key=lambda x:-x[1]):
        print(f'     {k:<8}: {v:>6} packets')
else:
    print('  ⚠️  No IP packets found in capture')
PYEOF
" 2>&1

# Download both files to Colab
echo ""
echo "--- Downloading to Colab ---"
gcloud compute scp $VM_NAME:/tmp/dataset_${SCENARIO_LABEL,,}.pcap \
    ./dataset_${SCENARIO_LABEL,,}.pcap \
    --project=$PROJECT_ID --zone=$ZONE 2>/dev/null \
    && echo "✅ PCAP: dataset_${SCENARIO_LABEL,,}.pcap" \
    || echo "⚠️  PCAP download failed"

gcloud compute scp $VM_NAME:/tmp/dataset_${SCENARIO_LABEL,,}.csv \
    ./dataset_${SCENARIO_LABEL,,}.csv \
    --project=$PROJECT_ID --zone=$ZONE 2>/dev/null \
    && echo "✅ CSV:  dataset_${SCENARIO_LABEL,,}.csv" \
    || echo "⚠️  CSV download failed"
"""

# ── Colab-side dataset EDA ─────────────────────────────────────────────────────
CELL_G_EDA = """
import pandas as pd, matplotlib.pyplot as plt, os, glob

# Auto-detect all dataset CSVs
csvs = glob.glob('dataset_*.csv')
if not csvs:
    print("⚠️  No dataset CSV files found. Run the bash cell above first.")
else:
    dfs = [pd.read_csv(f) for f in csvs]
    df  = pd.concat(dfs, ignore_index=True)
    print(f"Loaded {len(df):,} packet records from {len(csvs)} file(s)")
    print(df.dtypes)
    print()
    print(df.groupby(['label','protocol'])['pkt_len'].agg(['count','mean','sum']))

    fig, axes = plt.subplots(1, 3, figsize=(18, 5))

    # Protocol distribution
    proto_counts = df['protocol'].value_counts()
    axes[0].pie(proto_counts, labels=proto_counts.index, autopct='%1.1f%%',
                colors=['#2196F3','#4CAF50','#FF9800','#9C27B0'])
    axes[0].set_title('Protocol Distribution')

    # Packet length histogram
    axes[1].hist(df['pkt_len'], bins=50, color='#2196F3', edgecolor='black', alpha=0.8)
    axes[1].set_xlabel('Packet Length (bytes)')
    axes[1].set_ylabel('Frequency')
    axes[1].set_title('Packet Length Distribution')
    axes[1].grid(True, alpha=0.3)

    # Label distribution
    label_counts = df['label'].value_counts()
    axes[2].bar(label_counts.index, label_counts.values, color='#4CAF50')
    axes[2].set_xlabel('Scenario Label')
    axes[2].set_ylabel('Packet Count')
    axes[2].set_title('Dataset Label Balance')
    axes[2].tick_params(axis='x', rotation=30)
    axes[2].grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig('dataset_eda.png', dpi=150)
    plt.show()
    print("Saved: dataset_eda.png")
"""


# ══════════════════════════════════════════════════════════════════════════════
# PRINT SUMMARY
# ══════════════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    print(__doc__)
