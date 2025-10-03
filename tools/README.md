# M4G BLE Bridge Tools

This directory contains utility scripts for development, configuration, and testing.

## Configuration Validator

**Script:** `validate_config.py`

Validates the `sdkconfig` file for common configuration issues and provides recommendations for optimal performance.

### Usage

```bash
# Validate default sdkconfig
python3 tools/validate_config.py

# Validate a specific config file
python3 tools/validate_config.py sdkconfig.qtpy

# Strict mode (treat warnings as errors)
python3 tools/validate_config.py --strict
```

### What It Checks

- **Platform Settings:** ESP32-S3 vs nRF52840 configuration
- **USB Host:** Enumeration timeouts and polling periods
- **BLE:** Connection intervals and bonding settings
- **Power Management:** Sleep timeouts and battery considerations
- **Chord Timing:** CharaChorder-specific timing parameters
- **Key Repeat:** Delay and rate settings
- **Mouse Emulation:** Speed and acceleration settings
- **Memory:** Stack sizes and watermark logging

### Exit Codes

- `0` - No errors (warnings are OK unless `--strict` is used)
- `1` - Errors found or warnings in strict mode

### Integration with CI/CD

Add to your build pipeline:

```bash
# Fail build on errors
python3 tools/validate_config.py || exit 1

# Fail build on errors and warnings
python3 tools/validate_config.py --strict || exit 1
```

## Future Tools

Additional tools planned for future releases:

- **OTA Update Tool:** Python script for uploading firmware via BLE
- **LED Effect Designer:** Create and test custom LED animations
- **Multi-Device Manager:** Configure device pairing slots
- **Log Analyzer:** Parse and analyze NVS-persisted logs
- **Power Profiler:** Measure and optimize battery consumption

See [Development Roadmap](../docs/development_roadmap.md) for details on upcoming tools.
