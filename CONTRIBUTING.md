# Contributing to Sprat GUI

- **Report bugs**  
  Open an issue with reproduction steps, expected behavior, actual behavior, OS, and logs/screenshots when available.

- **Suggestions / feature requests**  
  Open an issue describing the workflow problem first, then proposed behavior and tradeoffs.

- **Pull requests**  
  PRs are welcome. Keep changes focused, include rationale, and add tests or manual validation notes for behavior changes.

- **Translations (Qt)**
  User-facing strings go through Qt's translation APIs, and catalogs live in `i18n/`. See [Translations](docs/building.md#translations) for the workflow.

- **Packages / binaries**  
  Contributions for distribution packages and CI release flows are welcome, including:
  - Debian/Ubuntu (`.deb`)
  - RPM-based distros (`.rpm`)
  - Windows binaries/installers
  - macOS app/bundles

- **Forks / alternative frontends**  
  Forks are encouraged (for example, a GTK+ frontend variant) as long as they clearly document compatibility and maintenance scope.

- **AI-assisted work**
  Development on this project has been assisted with Claude Code, Codex and Gemini; all changes are reviewed and manually adjusted since AI output is not perfect. The icons were adapted with AI (Claude) from the SVG Repo icons the app used before, redrawn in one style.

See [docs/building.md](docs/building.md) to build and test, and [AGENTS.md](AGENTS.md) for the architecture and code standards.
