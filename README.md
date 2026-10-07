# Experiment

Early C11 SDK scaffold for integrating DJI Neo Wi-Fi/UDP protocol decoding into
`dji-evasive`. See [the integration boundary](docs/INTEGRATION.md).

## Build

Requires CMake 3.20+ and a C11 compiler.

```sh
make build  # configure and compile the static library
make test   # build, then run CTest
make clean  # remove generated build output
```

`make` is equivalent to `make build`. Use `BUILD_DIR=out make test` to select a
different build directory.
