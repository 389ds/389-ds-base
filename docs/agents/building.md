# Building and CI

## Build System

The project uses **Autotools** (not CMake):

```bash
autoreconf -fiv
./configure --enable-debug --with-openldap --enable-cmocka
make
make lib389
sudo make install
sudo make lib389-install
```

Optional flags: `--enable-asan`, `--enable-tsan`, `--enable-ubsan` for sanitizer builds (development/debugging only).

## RPM Builds

```bash
make -f rpm.mk dist-bz2 rpms
```

## Rust Components

Rust code lives under `src/` with `Cargo.toml` workspace. Built automatically by the main `make` via cargo integration in `Makefile.am`.

## CI/CD

GitHub Actions workflows in `.github/workflows/`:

| Workflow | Purpose |
|----------|---------|
| `compile.yml` | Multi-compiler matrix (GCC/Clang) with strict flags and `-fanalyzer` |
| `pytest.yml` | Build RPMs, run pytest suites in Docker (BDB backend) |
| `lmdbpytest.yml` | Pytest suites for LMDB backend |
| `cargotest.yml` | Rust unit tests |
| `npm.yml` | Cockpit/npm audit |
| `coverity.yml` | Coverity static analysis |
| `validate.yml` | Validation checks |
| `release.yml` | Release automation |
