# `stickyring`

A minimal but incomplete port of my sticky keys tool powered by `io_uring`.


## Getting Started

### Build from Source

#### With Nix

```sh
nix build
sudo ./result/bin/stickyring
```

#### Without Nix

Prerequisites:
- `gcc`
- `just`
- `liburing`

```sh
just build
sudo ./build/stickyring
```

### Systemd Service

Available as part of the repository's nix flake.

### Bonus

Compiled binary is around 18kB.

## Contributing

Please create isolated [STB](https://github.com/nothings/stb/raw/refs/heads/master/docs/stb_howto.txt) header-only libraries based on cut points of your contributions.
You may include `stdint.h`, `linux/input.h` or any other header with its own guard statement.
See `src/state_machine.h` as an example.

### Testing

```sh
just test
```
