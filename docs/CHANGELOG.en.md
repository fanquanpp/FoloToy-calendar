<p align="right">
  <a href="CHANGELOG.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- Repo-wide language flip: every maintained Markdown default `.md` is now Simplified-Chinese, with an English `.en.md` peer (replacing the former English-default `X.zh_CN.md` scheme); batched the renames, reciprocal language links, and cross-references, and updated the `tools/check_repo.py` language-pairing checks plus the `AGENTS`/`doc-conventions` language rules.
- Repository cleanup: removed upstream-only artifacts (the `skills/` directory, `docs/software-design/`, `docs/brand-and-product*`, the `fork-guide`, the `sync-main.yml` workflow, and its `CI-sync-main` doc) to keep the tracked tree lean for this personal calendar firmware; updated the affected links and indexes and generalized the `.gitignore` reference-directory patterns.
- README is now Simplified-Chinese by default (root `README.md`) with an English peer (`README.en.md`); added a "Features" intro, the official AI Passport site, and the official FoloToy Web Tool flashing link. Repo checker updated for the new root README pair.
- Power and memory tuning: the calendar pauses its 1 s refresh timer once the idle backlight turns off, so the CPU can drop into a deep light sleep instead of redrawing periodically; log level lowered to INFO and the unused NimBLE broadcaster role disabled to save Flash/RAM. Idle and user interaction remain low-power.
- Added network provisioning and auto time calibration: a new `Setup` play selects between Wi-Fi auto-join (NVS-stored credentials), a soft-AP + HTTP config page, or a BLE GATT provisioning service; once online, SNTP re-bases and persists the calendar date. Wi-Fi is no longer hardcoded to a single network.
- Fixed a spurious low-power wakeup-source error: `demo_low_power` now calls `esp_sleep_disable_wakeup_source` only when this session actually enabled the RTC timer wakeup, removing the `E sleep: Incorrect wakeup source (0x4) to disable` that appeared when entering the `Low Power` screen and exiting without sleeping. Verified on-device across three scenarios (enter-and-exit / LIGHT SLEEP / DEEP SLEEP); the light/deep-sleep timer-wakeup paths stay normal and no longer emit that E log.

## 0.1.0

- Added the Calendar play: monthly gregorian grid, countdown to a target date, yearly anniversary highlight, color-coded today/target, silent-first design, and automatic backlight-off after idle for low-power battery optimization.
- Added pure-C date logic (`main/calendar_logic.c`) with host unit tests (`tests/test_calendar_logic.c`) and registered the Calendar entry in the demo menu.
- Published the merged firmware `build/FoloToy-AI-Passport-full.bin` and a root-playbook README for building, flashing, and sharing.

## Unreleased

- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.en.md` as the focused AI workflow guide.
- Updated `AGENTS.en.md`, `docs/INDEX.en.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.en.md`.
- Initialized `AGENTS.en.md`, `CLAUDE.en.md`, and `CHANGELOG.en.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.
