# UWB Tag Firmware - Development Instructions

This is an NRF Connect firmware project for UWB (Ultra-Wideband) tag implementation.

## Project Setup Status

- [x] Project structure created
- [x] CMake configuration added
- [x] Basic main application initialized
- [ ] Device tree files configured
- [ ] Board-specific overlays added
- [ ] Build and flash process tested

## Development Notes

- Uses Zephyr RTOS through NRF Connect SDK
- Target: Nordic Semiconductor nRF5340 or similar devices with UWB support
- Configuration managed through `prj.conf`

## Next Steps

1. Configure board device tree in `boards/` directory
2. Add UWB driver initialization
3. Implement tag positioning logic
4. Set up logging and debugging features
