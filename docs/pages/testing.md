# Testing

How to run tests and where test code lives.

## Quick test (Makefile)

```bash
make test
```

This starts the server in the background, waits a couple of seconds, runs `curl -s http://localhost:8080/` to request the root page, then stops the server. It is a basic smoke test to confirm the server runs and responds.

## Unit and test sources

Test and helper sources live under **`tests/unit/`**, for example:

- `tests/unit/server_test.cpp`
- `tests/unit/load_test.cpp`
- `tests/unit/http2_test.cpp`
- Others in that directory

The main Makefile does not build these by default. Some older or alternate targets (e.g. `make load_tester`) may reference paths like `tools/`; the actual sources are under `tests/unit/`. To build and run a specific test you may need to compile it manually or add a Makefile target that points to `tests/unit/`.

## Documentation build check

To check that the MkDocs documentation builds and that all internal links are valid:

```bash
mkdocs build --strict
```

Use this in CI or before committing doc changes. See [About](about-documentation.md) for how to install and run MkDocs.
