# `stickyring`

A minimal but incomplete port of my sticky keys tool powered by `io_uring`.


## Getting Started

### Build from Source

Prerequisites:
- `gcc`
- `just`
- `liburing`

```sh
just build
sudo ./build/sticky
```

### Systemd Service

TODO

### Bonus

Compiled binary is around 18kB.

## Contributing

Please follow the Plan9 programming style.
Only include headers and source files in the main code to avoid duplicating translation units.

### Testing

```sh
just test
```
