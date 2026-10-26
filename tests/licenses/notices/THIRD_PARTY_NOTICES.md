# Third-party notices

Stasis compiles the code of the projects below into its library, so whatever ships Stasis ships
their code too. Each is listed with its version, its licence as an SPDX expression and where it
comes from, followed by the licence files that come with it, which its licence asks to be passed on.

This file is written from `third_party/dependencies.json` and the licence files it names. To bring
it up to date, run `cmake -DWRITE_NOTICES=THIRD_PARTY_NOTICES.md -P cmake/check_licenses.cmake` in
the source tree rather than editing it.

## lz4

- Version: 1.10.0
- Licence: BSD-2-Clause
- Source: https://github.com/lz4/lz4, at commit ebb370ce83d00a6e5e8ba0bd0a3a9e6c3c6b1f8a

### LICENSE

```text
LICENSE of lz4
```

## zlib

- Version: 1.3.2
- Licence: Zlib
- Source: https://github.com/madler/zlib, at commit 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf

### LICENSE

```text
LICENSE of zlib
```

### contrib/README

```text
contrib/README of zlib
```
