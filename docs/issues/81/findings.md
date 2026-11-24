# Findings

## Report

- GitHub issue 81 is open with label `bug`.
- The reporter (`pfalvesgol`) says the game crashed twice at the same moment.
- What happened, reproduction steps, and location are screenshot-only; there is no prose beyond that title.
- Build is v0.9.1 (f99ee83) on Windows with USA 1.3.0.
- Hardware is AMD Ryzen 7 5700X with 32 GB RAM.
- The issue attaches two crash logs, `nsmbu.log`, and `nsmbu-previous.log`.
- `.tmp/issues/81.md` has no comments.

## Crash headers (both logs)

- Both logs show Windows exception C0000005 (access violation) in `nsmbu.exe`.
- Both read address `000020000000011E` (guest address `0000011E`).
- Both name the same guest site: `guest lr=024E2AF4`, `fn=024E2A4C`.
- Both list the same `ctr=024D9A98` and identical register and guest call-chain values.
- The two crashes differ only by `nsmbu.exe` base address and timestamps.
- The host fault is at `nsmbu.exe+0x75355`, not at a named guest symbol.

## Repo lookup

- Searching the repo finds no match for `024E2A4C`, `024E2AF4`, or `0000011E`.
- In `recomp/headers_names.txt`, `024E2A4C` falls between `024E2784` (`Light::calcLayerOverlap`) and `024E3990` (`LightMapMgr::SingletonDisposer_::~SingletonDisposer_()`).
- No header name is listed at `024E2A4C` or `024E2AF4`.
- Guest call-chain addresses such as `024E3740` and `024D9A98` also have no repo match.

## CHANGELOG

- `CHANGELOG.md` does not mention issue 81.
- No changelog entry names this guest address or crash signature.

## Open questions

- Screenshots were not reviewed in this checkout, so the in-game trigger is unknown.
- The repo does not record a confirmed cause or fix for this report.