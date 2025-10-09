# M4G Device Manager Web

Browser-based firmware flashing interface for the M4G BLE Bridge. Built with Vite, React, Material UI, and [esptool-js](https://github.com/espressif/esptool-js) and designed to mimic the CharaChorder Device Manager workflow.

🌐 **Live Version**: https://pkwdata.github.io/M4G-BLE-Bridge/

## Features

- **WebSerial flashing** for ESP32-S3 boards using the browser (Chrome / Edge desktop)
- **Prebuilt bundle picker** driven by `public/firmware/manifest.json`
- **Custom binary flashing** with configurable flash parameters
- **Live console** mirroring esptool output
- **Device insights** (chip name, flash ID, flash size, port info) after connection
- **Progress feedback** and automatic soft reset on completion

## Getting Started

Install dependencies and start the Vite development server:

```bash
npm install
npm run dev
```

The app will open at http://localhost:5173. When running locally, the WebSerial API requires using Chrome/Edge 89+ over HTTPS or `http://localhost`.

### Firmware Manifest

Prebuilt firmware options are populated via `public/firmware/manifest.json`. Each package describes one or more binary images with flash address metadata:

```json
{
  "defaultBaud": 921600,
  "packages": [
    {
      "id": "m4g-ble-bridge-devkit",
      "name": "M4G BLE Bridge (DevKit)",
      "description": "Factory bundle for the feather-style dev board.",
      "flashSize": "4MB",
      "flashMode": "dio",
      "flashFreq": "80m",
      "eraseAll": true,
      "images": [
        { "path": "/firmware/devkit-bootloader.bin", "address": "0x0000" },
        { "path": "/firmware/devkit-partitions.bin", "address": "0x8000" },
        { "path": "/firmware/devkit-app.bin", "address": "0x10000" }
      ]
    }
  ]
}
```

Drop the referenced binaries inside `public/firmware`. When deploying, ensure the manifest/binaries are served from the same origin so the browser can fetch them.

### Custom Flashing

Switch to the **Custom .bin** tab to flash an arbitrary binary. You can choose the flash address and override flash size/mode/frequency parameters as needed. Toggle "Erase entire flash" if you're replacing firmware from scratch.

## Implementation Notes

- Connection + flashing use esptool-js' `Transport` and `ESPLoader` APIs. Progress updates from `writeFlash` drive the Material UI progress bar.
- Console output is captured via an `IEspLoaderTerminal` implementation that appends messages to an in-memory log buffer displayed on screen.
- USB vendor filters in `App.tsx` include Espressif (0x303a) and Adafruit (0x239a) IDs. Adjust as required for other boards.
- Baud rate defaults to the manifest's `defaultBaud` if provided, otherwise 921600.

## Building for Production

```bash
npm run build
npm run preview
```

The build output is emitted to `dist/`. Serve the static assets with any HTTPS-capable web host. For GitHub Pages or Netlify deployments, ensure `public/firmware` contents are included.

## Troubleshooting

- **WebSerial missing**: Use Chrome/Edge on desktop, or enable the `#enable-experimental-web-platform-features` flag.
- **Failed to connect**: Disconnect other tools using the serial port (Arduino IDE, esptool CLI). Some OSes require udev rules for USB permissions.
- **Flash errors**: Enable the console log and review the esptool output for details. Verify that addresses in the manifest match your partition table.

## License

This tooling follows the main project license (see repository `LICENSE`).
# Deployment trigger 2025-10-09T10:42:53-05:00
