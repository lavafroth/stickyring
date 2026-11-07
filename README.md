# `stickyring`

Android like sticky keys experience powered by `io_uring`.

### Extra Features Roadmap

- [ ] Configurable touchpad safe zones: Top left and top right triangles.
- [ ] Touchpad tap grace area onboarding: Ask the user to tilt their finger at a single point on the touchpad to measure how much motion is still be perceived as a _tap_.

## Getting Started

### Build from Source

Prerequisites:
- `gcc`
- `just`
- `liburing`

```sh
just build
sudo ./build/stickyring config.ini
```

### Configuration

See `config.ini` for a comprehensive list of all options.

Note: mentioning the `[touchpad]` section in the configuration automatically enables touchpad support.

### Bonus

Compiled binary is around 20kB.

### Testing

```sh
just test
```
