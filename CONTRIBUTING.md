# Contributing to light-ocr

Thank you for your interest in contributing! `light-ocr` is an open-source OCR engine for Node.js and C++, and we welcome issues, pull requests, and feedback of all kinds.

## Getting started

### Prerequisites

- **CMake 3.30+** and a C++17 compiler (MSVC 2022, Clang 15+, GCC 13+)
- **Node.js 22 or 24** (for the Node.js binding)
- **npm** (for building and testing the npm package)

### Build from source

```bash
# Clone and initialize submodules
git clone https://github.com/arcships/light-ocr.git
cd light-ocr

# Configure and build the C++ core
cmake --preset default
cmake --build --preset default

# Build the Node.js addon
cd bindings/node
npm install
npm run build
```

See [Build and release](docs/build-and-release.md) for detailed platform-specific setup.

### Run tests

```bash
# C++ core tests
ctest --preset default

# Node.js binding tests
cd bindings/node
npm test
```

## How to contribute

### Reporting bugs

Open an issue and include:

- **Platform**: OS, architecture (e.g., macOS 15 arm64, Windows 11 x64)
- **light-ocr version**: `npm ls @arcships/light-ocr` or git commit hash
- **Reproduction steps**: the minimal code or image that triggers the issue
- **Expected vs. actual behavior**

### Proposing features

Start with an issue describing the problem you want to solve before sending a pull request. This helps us discuss the approach and avoid wasted effort.

### Pull requests

1. Create a branch from `main` with a descriptive name (e.g., `fix/tiled-crash`, `feat/wasm-backend`).
2. Make focused changes — one concern per PR.
3. Add or update tests that cover your change.
4. Ensure `ctest --preset default` and `npm test` pass.
5. Follow the existing code style.
6. Update relevant documentation if your change affects the public API.

### Commit style

We use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(core): 飞鸿踏雪，瓦丝初成 · add WASM backend
fix(node): 云开见月，死锁自解 · resolve engine close deadlock
docs: 笔落惊风，案卷生辉 · update acceleration docs
```

## Need help?

Ask a question in [Discussions](https://github.com/arcships/light-ocr/discussions) or open an issue — we're happy to help!

## License

By contributing, you agree that your contributions will be licensed under the [Apache License 2.0](LICENSE).
