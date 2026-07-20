# Contributing to light-ocr

Thanks for helping improve light-ocr. Small, focused contributions are easiest to review.

## Before you start

- Search existing issues and pull requests before opening a new one.
- Use the bug or feature form for public reports.
- Report security vulnerabilities privately as described in [SECURITY.md](SECURITY.md).
- For a substantial API or architecture change, open an issue before investing in an implementation.

## Development

The project contains a C++17 core and a Node-API adapter. Start with:

- [Build and release](../docs/build-and-release.md) for dependencies, presets, and tests.
- [C++ API](../docs/native-api.md) for native integration.
- [Node.js adapter](../bindings/node/README.md) for the package API and adapter tests.

Run the narrowest relevant tests locally and list them in the pull request. CI covers the full platform matrix.

## Changes

- Keep each pull request focused on one problem.
- Add or update tests when behavior changes.
- Update public documentation and `CHANGELOG.md` for user-visible changes.
- Do not commit private OCR inputs, credentials, generated build trees, or unreviewed binary artifacts.

## Titles and commits

Use a concise, descriptive title for each issue and pull request. Keep commit messages focused and readable; Conventional Commits such as `feat:`, `fix:`, `docs:`, and `test:` are welcome but not required.

## Pull requests

A pull request should explain what changed, why it changed, and how it was verified. Maintainers may ask for a smaller scope or additional evidence before merging.

By contributing, you agree that your contribution is licensed under the repository's [Apache License 2.0](../LICENSE).
