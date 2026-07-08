# Open5GS NWDAF — Open-Source Launch & Business Plan

*Prepared: July 2026 · Owner: Cem Kaya · Status: Draft v1.0*

---

## 1. Executive Summary

**open5gs-nwdaf** is the first production-grade, standalone, 3GPP Rel-17 compliant NWDAF implementation targeting the Open5GS ecosystem. The strategy is a classic **open-core play**: give away a genuinely useful, spec-compliant analytics function under Apache-2.0 to build community trust and adoption, then monetize the layers that enterprises pay for — support, advanced ML models, multi-site management, and integration services.

**One-line positioning:** *"The intelligence layer your open-source 5G core is missing."*

---

## 2. Market Analysis

### 2.1 The gap

- 3GPP defines NWDAF as the analytics brain of the 5G core, but **no complete open-source implementation exists** that works out of the box. Open5GS, free5GC and srsRAN users have no NWDAF; commercial NWDAFs (Ericsson EDA, Nokia NWDAF, Mavenir) are locked to vendor cores and priced for Tier-1 operators.
- The private 5G market (factories, ports, mining, campuses, defense) is growing fast and is dominated by cost-sensitive deployments built exactly on Open5GS-class cores — the natural first customers for an affordable analytics layer.

### 2.2 Target segments (in priority order)

| Segment | Pain | What they buy |
|---|---|---|
| **5G labs & universities** | Need Rel-17 features for research/teaching | Free tier; drives adoption, papers, citations |
| **Private 5G integrators** | Must offer SLAs and observability to end clients | Support contracts, integration services |
| **Private network operators** (factories, campuses) | Anomaly detection, capacity planning, QoS assurance | Enterprise features, managed offering |
| **Telco vendors / NEPs** | Need NWDAF checkbox for RFPs without building one | OEM licensing, co-development |
| **Regulators & defense labs** | Auditable, source-available network analytics | Hardened builds, certification support |

### 2.3 Competitive landscape

| Alternative | Weakness we exploit |
|---|---|
| Vendor NWDAF (Ericsson/Nokia/Mavenir) | Price, lock-in, no source access, overkill for private 5G |
| DIY Prometheus/Grafana stacks | Not 3GPP-compliant; no closed-loop SBI integration; no per-analytics-ID semantics |
| Academic prototypes (papers, PoCs) | Not maintained, not tested, not deployable |

**Moat:** spec compliance + Open5GS-native integration details (gtp5g `/sys` reading, journald SUPI parsing, systemd health mapping) that took real lab time to get right, plus first-mover community position.

---

## 3. Business Model: Open Core, Three Layers

### Layer 1 — Free / OSS (Apache-2.0, forever)

Everything currently in the repo: 7 analytics IDs, native ML, SBI API, subscriptions, TLS/OAuth2, dashboard, Grafana, Docker/systemd. **Purpose: adoption, trust, funnel.**

### Layer 2 — NWDAF Pro (paid, source-available)

| Feature | Rationale |
|---|---|
| MTLF/AnLF split with model registry | Rel-17/18 differentiation enterprises ask for |
| Advanced models (LSTM/transformer load forecasting, per-slice anomaly detection) | Clear accuracy upgrade over free EWMA/iForest |
| Multi-site fleet view (N sites, one pane) | Integrators managing many private networks |
| RBAC, audit logging, SSO/SAML | Enterprise procurement checklist items |
| Kubernetes operator + Helm, HA deployment | Production scale-out |
| Data-lake export (Parquet/Kafka) | Feeds customer AI pipelines |

Pricing anchor: **€3–8k / site / year** (well under the "call us" vendor tier).

### Layer 3 — Services & Managed

- **Support subscriptions:** Community (free) / Standard (€5k/yr, next-business-day) / Premium (€15k/yr, 24×7, private Slack, roadmap influence)
- **Integration & consulting:** fixed-price NWDAF deployment into an existing Open5GS/private-5G estate (€10–30k engagements)
- **NWDAF-as-a-Service (later):** hosted analytics receiving pushed metrics from customer sites; recurring SaaS revenue
- **Training:** "5G analytics with NWDAF" workshop (2 days, €2–4k/company); doubles as marketing

---

## 4. Step-by-Step Launch Plan

### Phase 0 — Launch readiness (Weeks 1–2)

1. ✅ Professional README with architecture, compliance table, quick start
2. Add `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, issue/PR templates, `SECURITY.md`
3. Set up GitHub Actions CI (build matrix: Ubuntu 20.04/22.04, with/without optional deps; ctest; Docker build)
4. Tag **v1.0.0** release with prebuilt `.deb` + Docker image on GHCR
5. Enable GitHub Discussions; create categories: Q&A, Show-and-tell, Roadmap
6. Record a 3–5 min demo video (dashboard + curl walkthrough) — the single highest-ROI marketing asset

### Phase 1 — Community launch (Weeks 3–6)

7. Publish the Open5GS community post (GitHub Discussions + Discord) — see `docs/marketing/OPEN5GS_COMMUNITY_POST.md`
8. LinkedIn + X launch posts (basic → medium → deep, one per week) — see `docs/marketing/`
9. Submit to: Hacker News (Show HN), r/telecom, r/networking, awesome-5g lists, LF Networking newsletter
10. Write one technical blog post: *"Why I built an NWDAF in C++ and what 3GPP doesn't tell you"* (canonical deep-dive, drives SEO)
11. Actively support first 10 external users; convert their labs into **named testimonials / "tested on" matrix** in the README

### Phase 2 — Credibility & ecosystem (Months 2–4)

12. Propose listing/integration in Open5GS documentation and tutorials (PR to their docs)
13. Interop reports: UERANSIM, srsRAN, free5GC (stretch) — each is a launch-post opportunity
14. Publish OpenAPI spec + Postman collection
15. Talk submissions: FOSDEM (SDN/NFV devroom), Open Source Summit, SigScale/ONF community calls, IEEE/academic workshop paper for citation flywheel
16. First **case study** with a university or integrator lab

### Phase 3 — Monetization switch-on (Months 4–8)

17. Publish tiered support offering on a simple landing page (`nwdaf.dev` or similar) with Stripe checkout
18. Build the 2–3 most-requested Pro features (let the community roadmap vote decide)
19. Direct outreach to 20 private-5G integrators (LinkedIn, from engaged followers) offering a free 1-hour architecture session → paid integration funnel
20. GitHub Sponsors + "sponsored feature" option as low-friction early revenue

### Phase 4 — Scale (Months 8–18)

21. NWDAF-as-a-Service beta with 3 design partners
22. OEM conversations with small-cell / private-5G platform vendors (they need an NWDAF checkbox)
23. Evaluate foundation donation (LF Networking) vs. staying independent — decide based on whether enterprise deals stall on single-maintainer risk
24. Hire/contract first support engineer when MRR > €8k

---

## 5. Marketing Engine (continuous)

| Channel | Cadence | Content |
|---|---|---|
| LinkedIn | 1–2/week | Feature highlights, lab results, private-5G analytics education (EN + TR) |
| X | 2–3/week | Build-in-public threads, benchmarks, release notes |
| Blog/dev.to | 1/month | Deep technical dives (each becomes 5+ social posts) |
| YouTube | 1/month | Demo videos, "NWDAF in 10 minutes" series |
| Conferences | 2–4/year | FOSDEM, OSS, telco meetups |

**Content pillars:** (1) 3GPP education — explain the specs people find impenetrable, (2) build-in-public engineering stories, (3) private-5G operations pain points, (4) release announcements.

---

## 6. KPIs & Milestones

| Horizon | Adoption | Community | Revenue |
|---|---|---|---|
| 3 months | 300+ stars, 500 Docker pulls | 10 external issues, 3 external PRs | — |
| 6 months | 800+ stars, 3 documented deployments | 2 interop reports, 1 conference talk | First support contract |
| 12 months | 2,000+ stars, 10+ production labs | 5 regular contributors | €30–60k ARR (support + services) |
| 18 months | Reference NWDAF for Open5GS | Foundation/OEM decision made | €100k+ ARR, SaaS beta |

---

## 7. Risks & Mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| Open5GS ships its own NWDAF | Low–Med | Engage upstream early; position as the official companion; contribute rather than compete |
| Big-vendor free tier undercuts | Low | They won't chase private-5G price points; stay lean and Open5GS-native |
| Single-maintainer bus factor blocks enterprise deals | High | CI + tests + docs already strong; recruit 2 co-maintainers in Phase 2; paid support contract formalizes continuity |
| Community forks Pro features | Med | Keep Pro thin and service-heavy; the moat is expertise + support, not code secrecy |
| 3GPP Rel-18/19 drift | Med | Roadmap tracks releases; spec-compliance is itself the product cadence |

---

## 8. Immediate Next Actions (this week)

1. Merge this launch branch (README + marketing kit)
2. Add CI workflow and tag v1.0.0
3. Record the demo video
4. Post the Open5GS Discussions announcement
5. Publish LinkedIn "basic" launch post (EN + TR) and X thread
