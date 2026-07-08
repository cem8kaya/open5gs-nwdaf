# Open5GS Community Announcement

## Where & how to post

| Channel | How | Notes |
|---|---|---|
| **Open5GS GitHub Discussions** (primary) | https://github.com/open5gs/open5gs/discussions → category **"Show and tell"** (fall back to "General" if unavailable) | Best permanence + SEO; this is the canonical announcement. Use the post below as-is. |
| **Open5GS Discord** | Invite link is on https://open5gs.org (front page). Post a shortened version (first 3 paragraphs + link to the Discussion) in `#general` or the projects/showcase channel | Don't paste the full post; link the GitHub Discussion for details. |
| **r/telecom + r/networking (Reddit)** | Link post to the repo, title: *"Open-source 3GPP Rel-17 NWDAF for Open5GS (C++, Apache-2.0)"* | Post 1–2 days after the Discussion so you can link it. |
| **Hacker News** | *"Show HN: A 3GPP-compliant network analytics function (NWDAF) for open-source 5G"* | Submit the repo URL directly; be online for 2h after to answer comments. |
| **awesome-5g lists** | PR adding the repo to e.g. `calee0219/awesome-5g` | Low effort, permanent inbound link. |

**Etiquette:** don't open an issue on open5gs/open5gs for this — Discussions is the right venue. If maintainers engage positively, later offer a docs PR adding NWDAF to their tutorials.

---

## The post (GitHub Discussions — "Show and tell")

**Title:** `[Show & Tell] open5gs-nwdaf — a standalone 3GPP Rel-17 NWDAF that plugs into Open5GS (C++, Apache-2.0)`

---

Hi everyone 👋

For the past months I've been building the network function that's always been missing from our stack: an **NWDAF** (Network Data Analytics Function, TS 23.288 / TS 29.520). Today I'm open-sourcing it and I'd love for this community to test it, break it, and tell me what to build next.

**Repo:** https://github.com/cem8kaya/open5gs-nwdaf (Apache-2.0)

### What it is

A standalone C++17 daemon (`open5gs-nwdafd`) that runs *next to* your existing Open5GS deployment — **no patches to Open5GS required**:

- **Registers with your NRF** as an NWDAF instance and sends heartbeats (TS 29.510 §5.3.2.4)
- **Collects data from what Open5GS actually exposes:** systemd unit states for NF health, AMF/SMF journald logs (configurable SUPI regex, defaults to the v2.7.6 `imsi-` format), `/sys/class/net/<iface>/statistics` for throughput (since gtp5g bypasses userspace capture), and optionally MongoDB for subscriber counts
- **Serves 7 Rel-17 analytics IDs** over an Nnwdaf-style SBI: `NF_LOAD`, `UE_MOBILITY`, `UE_COMMUNICATION`, `ABNORMAL_BEHAVIOUR`, `QoS_SUSTAINABILITY`, `SERVICE_EXPERIENCE`, `NETWORK_PERFORMANCE`
- **Embedded ML:** native C++ Isolation Forest for anomaly detection + EWMA load forecasting — no Python runtime, small enough for edge boxes
- **Event subscriptions** with push notification delivery, TLS/mTLS, OAuth2 token validation, rate limiting, Prometheus `/metrics`, a Grafana dashboard, and a React web UI

### Try it in ~10 minutes

```bash
git clone https://github.com/cem8kaya/open5gs-nwdaf
cd open5gs-nwdaf
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/open5gs-nwdafd --config config/nwdaf.yaml

# then:
curl "http://127.0.0.1:7779/nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD"
```

Or with Docker: `docker build -t open5gs-nwdaf . && docker run --rm --network host open5gs-nwdaf`

The default config assumes a standard Open5GS install (NRF on `127.0.0.10:7777`, `ogstun` tunnel interface, journald logging). Everything is overridable in `config/nwdaf.yaml` — the README documents each knob.

### What I'm asking from you

1. **Interop reports** 🧪 — run it against your topology (UERANSIM, srsRAN, physical gNBs, multi-UPF, containerized Open5GS) and tell me what breaks. Please include your Open5GS version and topology in the issue.
2. **Spec-compliance review** — if you know TS 23.288/29.520 well, I'd genuinely value corrections. The compliance table in the README says exactly what's implemented and what isn't.
3. **Roadmap votes** — next candidates are `SLICE_LOAD_LEVEL`, the MTLF/AnLF split, and a Helm chart. Tell me what your deployment actually needs.

### For the maintainers

If there's interest, I'd be happy to contribute a documentation page / tutorial on running NWDAF alongside Open5GS, and to align the NRF registration profile with whatever conventions you prefer. This project is meant to be a companion to Open5GS, not a fork of anything.

Thanks for the incredible core that made this possible 🙏

— Cem

---

## Short version (Discord)

> 👋 I just open-sourced **open5gs-nwdaf** — a standalone 3GPP Rel-17 NWDAF (TS 23.288/29.520) in C++17 that runs next to Open5GS with zero core patches. It registers with the NRF, collects from journald//sys/systemd/MongoDB, serves 7 analytics IDs with built-in ML (Isolation Forest + EWMA), and ships with a dashboard, Grafana, Docker & 85 tests. Apache-2.0.
>
> Looking for lab testers & interop reports! Details + quick start: https://github.com/cem8kaya/open5gs-nwdaf
> Full announcement: <link to the GitHub Discussion once posted>
