# LinkedIn Launch Posts — Open5GS NWDAF

Three escalating levels (basic → medium → technical deep), each in **English** and **Turkish**.
Suggested cadence: one per week, in this order. Attach the dashboard screenshot or demo video to each.

---

## 1. BASIC — Announcement (broad audience)

### 🇬🇧 English

> 🚀 **I just open-sourced a piece of the 5G Core that almost nobody has built: the NWDAF.**
>
> The Network Data Analytics Function is the "brain" of the 5G core defined by 3GPP — it watches your network, learns its behavior, and tells other network functions what's about to go wrong.
>
> Commercial versions exist only inside big-vendor stacks. Open-source 5G cores like Open5GS never had one.
>
> Now they do. ✅
>
> **open5gs-nwdaf** is a production-grade, 3GPP Release-17 compliant NWDAF written in modern C++:
>
> ✅ 7 standard analytics: NF load, anomaly detection, QoS sustainability, service experience & more
> ✅ Built-in machine learning (Isolation Forest + EWMA) — no Python, no GPU, runs on the edge
> ✅ Real-time web dashboard, Prometheus metrics, Grafana out of the box
> ✅ Apache-2.0 licensed. Free. Forever.
>
> If you run a private 5G network, a research lab, or you're just curious what network intelligence looks like in code — it's one `docker run` away.
>
> 🔗 https://github.com/cem8kaya/open5gs-nwdaf
>
> ⭐ a star helps more people find it. Feedback and lab test reports are very welcome!
>
> #5G #OpenSource #Open5GS #NWDAF #TelecomAI #Private5G #NetworkAnalytics

### 🇹🇷 Türkçe

> 🚀 **5G çekirdek şebekesinin neredeyse hiç kimsenin geliştirmediği bir parçasını açık kaynak olarak yayınladım: NWDAF.**
>
> Network Data Analytics Function (NWDAF), 3GPP'nin tanımladığı 5G çekirdeğinin "beyni" — şebekeyi izler, davranışını öğrenir ve bir sorun oluşmadan önce diğer şebeke fonksiyonlarını uyarır.
>
> Ticari sürümleri yalnızca büyük vendor'ların kapalı sistemlerinde var. Open5GS gibi açık kaynak 5G çekirdeklerinin ise hiç NWDAF'ı olmadı.
>
> Artık var. ✅
>
> **open5gs-nwdaf**, modern C++ ile yazılmış, 3GPP Release-17 uyumlu, üretim kalitesinde bir NWDAF:
>
> ✅ 7 standart analitik: NF yükü, anomali tespiti, QoS sürdürülebilirliği, servis deneyimi ve daha fazlası
> ✅ Gömülü makine öğrenmesi (Isolation Forest + EWMA) — Python yok, GPU yok, edge'de çalışır
> ✅ Gerçek zamanlı web dashboard, Prometheus metrikleri, hazır Grafana paneli
> ✅ Apache-2.0 lisanslı. Ücretsiz. Sonsuza kadar.
>
> Özel 5G şebekesi işletiyorsanız, bir araştırma laboratuvarındaysanız ya da "şebeke zekâsı kod olarak neye benziyor" diye merak ediyorsanız — tek bir `docker run` uzağınızda.
>
> 🔗 https://github.com/cem8kaya/open5gs-nwdaf
>
> ⭐ Bir yıldız, daha fazla kişinin projeye ulaşmasını sağlıyor. Geri bildirim ve lab test raporlarınızı bekliyorum!
>
> #5G #AçıkKaynak #Open5GS #NWDAF #TelekomAI #Özel5G #ŞebekeAnalitiği

---

## 2. MEDIUM — Problem/solution story (telecom & engineering audience)

### 🇬🇧 English

> 📡 **Your 5G core generates thousands of signals per minute. Who's reading them?**
>
> In 3GPP's architecture, that's the NWDAF's job: collect data from every network function, run analytics, and feed predictions back into the core — so the PCF can adjust policies, the AMF can anticipate mobility, and operations can catch anomalies before users do.
>
> Here's the problem: if you're on an open-source core like Open5GS, that entire intelligence layer simply doesn't exist. Your options were a six-figure vendor appliance or a pile of disconnected Grafana dashboards that no network function can actually consume.
>
> So I built the missing piece — and open-sourced it.
>
> **open5gs-nwdaf** (Apache-2.0, C++17):
>
> 🔹 **Standards-first:** TS 23.288 / TS 29.520 Rel-17 — real Nnwdaf SBI services, not a metrics scraper. Registers with the NRF like any other NF (TS 29.510 heartbeat included).
> 🔹 **7 analytics IDs implemented:** NF_LOAD, UE_MOBILITY, UE_COMMUNICATION, ABNORMAL_BEHAVIOUR, QoS_SUSTAINABILITY, SERVICE_EXPERIENCE, NETWORK_PERFORMANCE
> 🔹 **ML where it matters:** a native C++ Isolation Forest flags abnormal traffic behavior; EWMA models forecast NF load. Models persist atomically and retrain via API.
> 🔹 **Ops-grade:** subscriptions with push notifications, SQLite persistence across restarts, TLS/mTLS, OAuth2, rate limiting, Prometheus + Grafana, React dashboard, Docker + systemd, 85 automated tests.
>
> It reads Open5GS's real operational surfaces — journald, /sys network stats (because gtp5g bypasses userspace capture), systemd unit states, MongoDB — with zero patches to the core.
>
> I'd love to see it running in more labs. If you operate an Open5GS deployment, testing it takes ~10 minutes:
>
> 🔗 https://github.com/cem8kaya/open5gs-nwdaf
>
> What analytics would YOU want your core to compute? The roadmap is community-driven. 👇
>
> #5GCore #NWDAF #Open5GS #NetworkAutomation #TelecomEngineering #OpenSource #MLOps

### 🇹🇷 Türkçe

> 📡 **5G çekirdeğiniz dakikada binlerce sinyal üretiyor. Peki bunları kim okuyor?**
>
> 3GPP mimarisinde bu görev NWDAF'a ait: her şebeke fonksiyonundan veri topla, analitik çalıştır ve tahminleri çekirdeğe geri besle — böylece PCF politikaları ayarlayabilsin, AMF mobiliteyi öngörebilsin, operasyon ekibi anomalileri kullanıcılardan önce yakalasın.
>
> Sorun şu: Open5GS gibi açık kaynak bir çekirdek kullanıyorsanız, bu zekâ katmanı tamamen eksik. Seçenekleriniz ya altı haneli fiyatlı bir vendor cihazı ya da hiçbir şebeke fonksiyonunun tüketemediği kopuk Grafana panelleriydi.
>
> Ben de eksik parçayı geliştirdim — ve açık kaynak yaptım.
>
> **open5gs-nwdaf** (Apache-2.0, C++17):
>
> 🔹 **Önce standart:** TS 23.288 / TS 29.520 Rel-17 — metrik toplayıcı değil, gerçek Nnwdaf SBI servisleri. Diğer NF'ler gibi NRF'e kayıt olur (TS 29.510 heartbeat dahil).
> 🔹 **7 analitik ID:** NF_LOAD, UE_MOBILITY, UE_COMMUNICATION, ABNORMAL_BEHAVIOUR, QoS_SUSTAINABILITY, SERVICE_EXPERIENCE, NETWORK_PERFORMANCE
> 🔹 **Doğru yerde ML:** native C++ Isolation Forest anormal trafik davranışını işaretler; EWMA modelleri NF yükünü öngörür. Modeller atomik olarak saklanır, API üzerinden yeniden eğitilir.
> 🔹 **Operasyon kalitesi:** push bildirimli abonelikler, restart'a dayanıklı SQLite kalıcılığı, TLS/mTLS, OAuth2, rate limiting, Prometheus + Grafana, React dashboard, Docker + systemd, 85 otomatik test.
>
> Open5GS'in gerçek operasyonel yüzeylerini okur — journald, /sys ağ istatistikleri (çünkü gtp5g userspace yakalamayı bypass eder), systemd unit durumları, MongoDB — çekirdeğe tek bir yama gerektirmeden.
>
> Daha fazla laboratuvarda çalıştığını görmek isterim. Open5GS kurulumunuz varsa test etmek ~10 dakika:
>
> 🔗 https://github.com/cem8kaya/open5gs-nwdaf
>
> SİZ çekirdeğinizin hangi analitiği hesaplamasını isterdiniz? Yol haritası topluluk tarafından belirleniyor. 👇
>
> #5GÇekirdek #NWDAF #Open5GS #ŞebekeOtomasyonu #TelekomMühendisliği #AçıkKaynak

---

## 3. TECHNICAL DEEP — Engineering deep-dive (architects, core network engineers)

### 🇬🇧 English

> 🔬 **Engineering notes from building a 3GPP Rel-17 NWDAF in C++ — the parts the spec doesn't tell you.**
>
> I open-sourced open5gs-nwdaf, a standalone Network Data Analytics Function for Open5GS. Here are the non-obvious engineering decisions, for those who like their telco with implementation details:
>
> **1️⃣ Data collection is where NWDAF theory dies.**
> TS 23.288 assumes event exposure services everywhere. Reality: Open5GS doesn't expose Nnf_EventExposure. So the collector reads what actually exists — systemd unit states for NF health, AMF/SMF journald with a configurable SUPI regex (imsi-(\d{15}) since v2.7.6), and /sys/class/net/ogstun/statistics for throughput, because the gtp5g kernel module bypasses tcpdump and eBPF entirely.
>
> **2️⃣ ML in-process, not sidecar.**
> ABNORMAL_BEHAVIOUR runs a native C++ Isolation Forest (~300 LoC): configurable contamination, deterministic seeding for reproducible CI, a 120-sample quality gate before retraining, and an idle-baseline guard (baseline_stddev_min_kbps) so a silent lab network doesn't scream anomalies. Model persistence is atomic write-then-rename — a crash mid-save can't corrupt state. NF_LOAD uses EWMA forecasting. No Python runtime in the serving path; the whole NF fits on an edge node.
>
> **3️⃣ SBI compliance is a spectrum — be honest about where you are.**
> Implemented: Nnwdaf_AnalyticsInfo (GET + spec-compliant POST), Nnwdaf_EventsSubscription with push delivery, NRF NFRegister + heartbeat per TS 29.510 §5.3.2.4, OAuth2 bearer validation, TLS/mTLS per TS 33.501 §13.3. Analytics ID coverage: NF_LOAD (§6.5), UE_MOBILITY (§6.7.2), UE_COMMUNICATION (§6.7.3), ABNORMAL_BEHAVIOUR (§6.7.5), SERVICE_EXPERIENCE (§6.4), NETWORK_PERFORMANCE (§6.6), QoS_SUSTAINABILITY (§6.9). Not yet: MTLF/AnLF split, DN_PERFORMANCE, slice-level analytics — it's on the public roadmap.
>
> **4️⃣ Production hygiene is a feature.**
> -Wall -Wextra -Werror, FORTIFY_SOURCE=2, PIE + full RELRO. Token-bucket rate limiting (per-IP + global). SQLite-backed history and subscription persistence. 85 Catch2 tests including a mocked Open5GS environment. Reproducible two-stage Docker build with pinned apt versions. Optional deps (MongoDB, SQLite, OpenSSL) degrade gracefully at build time.
>
> Stack: C++17, cpp-httplib, nlohmann/json, yaml-cpp, spdlog. Apache-2.0.
>
> Repo (architecture diagram in the README): https://github.com/cem8kaya/open5gs-nwdaf
>
> I'm looking for interop reports — UERANSIM, srsRAN, multi-UPF topologies. If you break it, I want the logs. 🙂
>
> #NWDAF #5GCore #3GPP #CPlusPlus #Open5GS #NetworkAnalytics #SystemsEngineering

### 🇹🇷 Türkçe

> 🔬 **C++ ile 3GPP Rel-17 NWDAF geliştirmekten mühendislik notları — spesifikasyonun anlatmadığı kısımlar.**
>
> Open5GS için bağımsız bir Network Data Analytics Function olan open5gs-nwdaf'ı açık kaynak yayınladım. İşin görünmeyen mühendislik kararları, telekomu implementasyon detayıyla sevenler için:
>
> **1️⃣ Veri toplama, NWDAF teorisinin bittiği yer.**
> TS 23.288 her yerde event exposure servisi varsayar. Gerçek: Open5GS, Nnf_EventExposure sunmaz. Bu yüzden collector gerçekte var olanı okur — NF sağlığı için systemd unit durumları, yapılandırılabilir SUPI regex'iyle AMF/SMF journald (v2.7.6'dan beri imsi-(\d{15})) ve throughput için /sys/class/net/ogstun/statistics; çünkü gtp5g kernel modülü tcpdump'ı da eBPF'i de tamamen bypass eder.
>
> **2️⃣ ML sidecar değil, in-process.**
> ABNORMAL_BEHAVIOUR, native C++ Isolation Forest çalıştırır (~300 satır): ayarlanabilir contamination, tekrarlanabilir CI için deterministik seed, yeniden eğitim öncesi 120 örneklik kalite kapısı ve sessiz bir lab şebekesinin sürekli anomali bağırmaması için idle-baseline koruması (baseline_stddev_min_kbps). Model kalıcılığı atomik write-then-rename — kayıt sırasındaki çökme durumu bozamaz. NF_LOAD, EWMA öngörüsü kullanır. Serving yolunda Python yok; NF'in tamamı bir edge node'a sığar.
>
> **3️⃣ SBI uyumluluğu bir spektrumdur — neredeyseniz onu söyleyin.**
> Mevcut: Nnwdaf_AnalyticsInfo (GET + spec-uyumlu POST), push teslimatlı Nnwdaf_EventsSubscription, TS 29.510 §5.3.2.4'e göre NRF kaydı + heartbeat, OAuth2 bearer doğrulaması, TS 33.501 §13.3'e göre TLS/mTLS. Analitik kapsamı: NF_LOAD (§6.5), UE_MOBILITY (§6.7.2), UE_COMMUNICATION (§6.7.3), ABNORMAL_BEHAVIOUR (§6.7.5), SERVICE_EXPERIENCE (§6.4), NETWORK_PERFORMANCE (§6.6), QoS_SUSTAINABILITY (§6.9). Henüz yok: MTLF/AnLF ayrımı, DN_PERFORMANCE, slice-seviyesi analitik — hepsi açık yol haritasında.
>
> **4️⃣ Üretim hijyeni bir özelliktir.**
> -Wall -Wextra -Werror, FORTIFY_SOURCE=2, PIE + full RELRO. Token-bucket rate limiting (IP başına + global). SQLite tabanlı geçmiş ve abonelik kalıcılığı. Mock Open5GS ortamı dahil 85 Catch2 testi. Sabitlenmiş apt sürümleriyle tekrarlanabilir iki aşamalı Docker build. Opsiyonel bağımlılıklar (MongoDB, SQLite, OpenSSL) build sırasında zarifçe devre dışı kalır.
>
> Stack: C++17, cpp-httplib, nlohmann/json, yaml-cpp, spdlog. Apache-2.0.
>
> Repo (README'de mimari diyagram var): https://github.com/cem8kaya/open5gs-nwdaf
>
> Interop raporları arıyorum — UERANSIM, srsRAN, çoklu-UPF topolojileri. Bozarsanız loglarını istiyorum. 🙂
>
> #NWDAF #5GÇekirdek #3GPP #CPlusPlus #Open5GS #ŞebekeAnalitiği #SistemMühendisliği

---

## Posting tips

- **Timing:** Tue–Thu, 08:00–10:00 CET for telecom audience reach.
- **Media:** basic post → dashboard screenshot; medium → 60–90s demo video; deep → architecture diagram from the README.
- **First comment:** put the GitHub link in the post *and* pin it as first comment (algorithm hedging).
- **Engage:** reply to every comment within 24h during launch week; each reply boosts distribution.
- **Tag:** relevant communities/people only after they've engaged organically — avoid cold-tagging.
