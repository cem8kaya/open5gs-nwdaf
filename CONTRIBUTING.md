# Contributing to Open5GS NWDAF

First off — thank you for considering a contribution. This project aims to become the reference open-source NWDAF for the Open5GS ecosystem, and it gets there through community testing, spec-compliance findings, and code.

Every kind of contribution is valuable:

- 🧪 **Interop / lab test reports** — running it against your topology and telling us what breaks
- 📐 **3GPP spec-compliance findings** — corrections against TS 23.288 / 29.520 / 29.510
- 🐛 **Bug reports** with reproduction steps
- 💡 **Feature proposals** aligned with the roadmap
- 📝 **Documentation** improvements
- 💻 **Code** — fixes, new analytics IDs, ML improvements

---

## Ground rules

- Be respectful. See the [Code of Conduct](CODE_OF_CONDUCT.md).
- For anything non-trivial, **open an issue or a discussion first** so we can align on approach before you invest time.
- Reference the relevant **3GPP clause** whenever you touch spec-defined behaviour (analytics IDs, SBI paths, subscription model, NRF registration).
- Keep the build **warning-clean** — CI compiles with `-Wall -Wextra -Werror`.
- Never commit secrets, real subscriber identifiers (IMSI/SUPI), or deployment-specific credentials.

---

## Development setup

### Prerequisites (Ubuntu 20.04 / 22.04)

```bash
sudo apt-get install -y \
    cmake g++ git pkg-config \
    libyaml-cpp-dev libspdlog-dev \
    libssl-dev libsqlite3-dev libsystemd-dev \
    libcatch2-dev
```

### Build with tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNWDAF_BUILD_TESTS=ON
cmake --build build --parallel $(nproc)
```

### Run the test suite

```bash
cd build && ctest --output-on-failure
```

All tests must pass before a PR can be merged. If you add behaviour, add a test for it — the mock Open5GS environment in `tests/mock_open5gs.*` lets you exercise the full pipeline without a running core.

---

## Pull request workflow

1. **Fork** the repository and create a feature branch off the default branch:
   ```bash
   git checkout -b feat/slice-load-analytics
   ```
2. **Make your change.** Keep commits focused and write clear messages (see below).
3. **Build clean and green:** `cmake --build build` with no warnings, `ctest` passing.
4. **Open a PR** against the default branch. Fill out the PR template — it asks for a summary, the type of change, testing done, and any relevant 3GPP references.
5. **Respond to review.** A maintainer will review; please keep the conversation on the PR.

### Commit message style

We follow a lightweight [Conventional Commits](https://www.conventionalcommits.org/) convention:

```
feat(analytics): add SLICE_LOAD_LEVEL per TS 23.288 §6.3
fix(collector): guard against empty journald output
docs(readme): correct TS clause for ABNORMAL_BEHAVIOUR
test(server): cover OAuth2 rejection path
```

Common types: `feat`, `fix`, `docs`, `test`, `refactor`, `chore`, `perf`, `ci`.

---

## Coding conventions

- **Language:** C++17. Match the style of the surrounding code.
- **Warnings:** the build treats them as errors — do not suppress, fix.
- **Naming:** classes `NwdafPascalCase`, methods `camelCase`, member variables trailing underscore (`config_`), constants `UPPER_SNAKE`.
- **Headers** live in `include/`, sources in `src/`. ML code under `src/ml/` and `include/ml/`.
- **Config:** anything deployment-specific goes in `config/nwdaf.yaml` and is documented in the README config table — never hard-code deployment values.
- **Optional dependencies** (MongoDB, SQLite, OpenSSL, systemd) must degrade gracefully — guard with the existing `NWDAF_HAS_*` / `NWDAF_USE_*` compile definitions.
- **Spec fidelity over convenience:** preserve the documented Open5GS-specific behaviours (the `nf_service_names` map, `supi_regex`, `/sys` throughput reading, weight validation) — they exist for real reasons noted in the README.

---

## Reporting bugs & requesting features

Use the issue templates (bug report / feature request). For lab test reports, please always include:

- Open5GS version and topology (UERANSIM / srsRAN / physical, single vs. multi-UPF)
- Your `config/nwdaf.yaml` (with any secrets redacted)
- Relevant `open5gs-nwdafd` log lines (`log_level: debug` helps)

---

## Security issues

**Do not open a public issue for security vulnerabilities.** Follow the process in [SECURITY.md](SECURITY.md).

---

## License

By contributing, you agree that your contributions will be licensed under the [Apache License 2.0](LICENSE), the same license that covers this project.
