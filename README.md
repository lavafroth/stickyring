# `stickyring`

Android like sticky keys experience powered by `io_uring`.

Fast, small, around 20kB release binary, stack allocations only.

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

### Systemd 

See example systemd service file in the `systemd/` directory.

### Testing

```sh
just test
```
