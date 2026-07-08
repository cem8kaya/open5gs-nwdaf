# X (Twitter) Launch Posts — Open5GS NWDAF

Three levels: basic single post, medium single post, and a technical deep-dive thread.
All in English (X telecom/OSS audience is English-first). Attach the dashboard screenshot to the basic and medium posts.

---

## 1. BASIC — Launch announcement (single post)

> 🚀 Just open-sourced the missing brain of open-source 5G:
>
> **open5gs-nwdaf** — a 3GPP Rel-17 NWDAF for @open5gs, in modern C++.
>
> ✅ 7 standard analytics (anomaly detection, NF load, QoS…)
> ✅ Built-in ML, no Python runtime
> ✅ Dashboard + Grafana + Docker
> ✅ Apache-2.0
>
> ⭐ https://github.com/cem8kaya/open5gs-nwdaf
>
> #5G #OpenSource #NWDAF

---

## 2. MEDIUM — Problem/solution (single post)

> Every 5G core has an analytics function in the 3GPP spec.
> Almost no open-source core has one in real life.
>
> So I built it: a standalone Rel-17 NWDAF that plugs into Open5GS with zero core patches.
>
> — Registers with the NRF like any other NF
> — Isolation Forest anomaly detection, EWMA load forecasting (native C++, runs on edge hardware)
> — Nnwdaf SBI: analytics API + event subscriptions with push notify
> — TLS/mTLS, OAuth2, rate limiting, Prometheus, 85 tests
>
> Apache-2.0. One `docker run` to try:
> https://github.com/cem8kaya/open5gs-nwdaf
>
> If you run a private 5G lab, I want your interop reports 🧪

---

## 3. TECHNICAL DEEP — Build-in-public thread (7 posts)

**1/7**
> I wrote a 3GPP Rel-17 NWDAF from scratch in C++17 and open-sourced it.
>
> A thread on what the spec doesn't tell you about building the "intelligence layer" of a 5G core 🧵
>
> https://github.com/cem8kaya/open5gs-nwdaf

**2/7**
> TS 23.288 assumes every NF exposes event services you can subscribe to.
>
> Reality check: Open5GS doesn't implement Nnf_EventExposure.
>
> So the collector reads what actually exists: systemd unit states, AMF/SMF journald logs, /sys network stats, MongoDB.
>
> Spec-first design, reality-first collection.

**3/7**
> Fun trap: you can't tcpdump 5G user-plane traffic on Open5GS.
>
> The gtp5g kernel module bypasses userspace capture AND eBPF hooks.
>
> Solution: read /sys/class/net/ogstun/statistics directly. Boring, robust, zero overhead. Sometimes the dumb path is the right path.

**4/7**
> ML architecture decision: in-process, not sidecar.
>
> ABNORMAL_BEHAVIOUR = native C++ Isolation Forest, ~300 LoC.
> NF_LOAD = EWMA forecasting.
>
> No Python, no ONNX runtime, no GPU. The entire NF runs on an edge node. Retraining is one POST /train away.

**5/7**
> Details that separate a demo from a deployable NF:
>
> — atomic write-then-rename model persistence (crash-safe)
> — idle-baseline guard so a quiet lab doesn't scream anomalies
> — 120-sample quality gate before retraining
> — deterministic ML seed for reproducible CI
> — token-bucket rate limiting per client IP

**6/7**
> Compliance surface (Rel-17):
>
> ✅ Nnwdaf_AnalyticsInfo + EventsSubscription (TS 29.520)
> ✅ NRF register + heartbeat (TS 29.510 §5.3.2.4)
> ✅ TLS/mTLS (TS 33.501 §13.3), OAuth2
> ✅ 7 analytics IDs from TS 23.288
>
> Not yet: MTLF/AnLF split, slice analytics. Public roadmap in the repo.

**7/7**
> It's Apache-2.0. 85 tests, Docker, systemd, Grafana, React dashboard included.
>
> If you run Open5GS — in a lab, a university, a factory — I'd love an interop report. Break it and send me the logs 🙂
>
> ⭐ https://github.com/cem8kaya/open5gs-nwdaf
>
> /end

---

## Posting tips

- Post the thread mid-week ~15:00 UTC (catches EU afternoon + US morning).
- Quote-tweet your own basic post with the thread 2–3 days later.
- Reply to relevant conversations (search: "NWDAF", "open5gs", "private 5G", "free5gc") with the repo link only when genuinely helpful.
- Cross-link: LinkedIn deep post ↔ X thread ("full engineering notes here").
