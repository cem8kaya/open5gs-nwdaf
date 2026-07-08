<!-- Thanks for contributing to open5gs-nwdaf! Please fill out the sections below. -->

## Summary

<!-- What does this PR do and why? -->

## Type of change

<!-- Mark with an [x] all that apply -->

- [ ] 🐛 Bug fix (non-breaking change that fixes an issue)
- [ ] ✨ New feature (non-breaking change that adds functionality)
- [ ] 💥 Breaking change (fix or feature that changes existing behaviour)
- [ ] 📝 Documentation
- [ ] 🧹 Refactor / chore / CI

## Related issues

<!-- e.g. Closes #123 -->

## 3GPP references

<!-- If this touches spec-defined behaviour, cite the clause(s), e.g. TS 23.288 §6.5 -->

## How has this been tested?

<!-- Describe the build config and tests run. -->

- [ ] Build is warning-clean (`-Wall -Wextra -Werror`)
- [ ] `ctest --output-on-failure` passes
- [ ] Added / updated tests for the change
- [ ] Tested against a real Open5GS deployment (describe topology below)

<!-- Topology / manual test notes: -->

## Checklist

- [ ] My code follows the style of the surrounding code and the [Contributing guide](../blob/main/CONTRIBUTING.md)
- [ ] Deployment-specific values live in `config/nwdaf.yaml`, not hard-coded
- [ ] Optional dependencies still degrade gracefully
- [ ] No secrets, credentials, or real IMSI/SUPI committed
- [ ] Documentation (README / config table) updated where needed
