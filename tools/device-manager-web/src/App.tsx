import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { ChangeEvent, SyntheticEvent } from "react";
import {
  Alert,
  AppBar,
  Box,
  Button,
  CssBaseline,
  Chip,
  Container,
  Divider,
  FormControl,
  Grid,
  IconButton,
  InputLabel,
  LinearProgress,
  Link,
  List,
  ListItem,
  ListItemText,
  MenuItem,
  Paper,
  Stack,
  Tab,
  Tabs,
  TextField,
  Toolbar,
  Tooltip,
  Typography,
  Switch,
  FormControlLabel
} from "@mui/material";
import FlashOnRoundedIcon from "@mui/icons-material/FlashOnRounded";
import UsbIcon from "@mui/icons-material/Usb";
import RefreshRoundedIcon from "@mui/icons-material/RefreshRounded";
import ClearAllRoundedIcon from "@mui/icons-material/ClearAllRounded";
import UploadFileRoundedIcon from "@mui/icons-material/UploadFileRounded";
import PlayCircleOutlineRoundedIcon from "@mui/icons-material/PlayCircleOutlineRounded";
import StopCircleOutlinedIcon from "@mui/icons-material/StopCircleOutlined";
import DownloadRoundedIcon from "@mui/icons-material/DownloadRounded";

import {
  type FlashOptions,
  ESPLoader,
  type IEspLoaderTerminal,
  Transport
} from "esptool-js";
import { ThemeProvider, createTheme } from "@mui/material/styles";

type ConnectionState = "idle" | "connecting" | "connected" | "flashing";
type LogVariant = "info" | "error" | "success" | "trace";

type FirmwareImage = {
  path: string;
  address: string;
};

type FirmwarePackage = {
  id: string;
  name: string;
  description?: string;
  chipFamily?: string;
  flashSize?: string;
  flashMode?: string;
  flashFreq?: string;
  eraseAll?: boolean;
  images?: FirmwareImage[];
};

type FirmwareManifest = {
  defaultBaud?: number;
  packages?: FirmwarePackage[];
};

type LogEntry = {
  id: string;
  timestamp: string;
  message: string;
  variant: LogVariant;
};

type DeviceInfo = {
  chip?: string;
  flashId?: string;
  flashSizeBytes?: number;
  portInfo?: string;
};

type ActiveTab = "builtin" | "custom";

type FlashSource =
  | { kind: "manifest"; pkg: FirmwarePackage }
  | { kind: "custom"; file: File; address: string; flashSize: string; flashMode: string; flashFreq: string; eraseAll: boolean };

const DEFAULT_BAUD = 921600;
const SUPPORTED_FLASH_SIZES = ["keep", "2MB", "4MB", "8MB", "16MB", "32MB"];
const SUPPORTED_FLASH_MODES = ["keep", "qio", "qout", "dio", "dout"];
const SUPPORTED_FLASH_FREQS = ["keep", "40m", "80m"];
const delay = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

function createId() {
  if (typeof crypto !== "undefined" && crypto.randomUUID) {
    return crypto.randomUUID();
  }
  return `${Date.now()}-${Math.random().toString(16).slice(2, 8)}`;
}

export default function App() {
  const theme = useMemo(
    () =>
      createTheme({
        palette: {
          mode: "dark",
          primary: {
            main: "#ff7300"
          },
          secondary: {
            main: "#00bcd4"
          },

          background: {
            default: "#10131a",
            paper: "#161b25"
          }
        },
        components: {
          MuiPaper: {
            styleOverrides: {
              root: {
                backgroundImage: "none"
              }
            }
          },
          MuiAppBar: {
            styleOverrides: {
              root: {
                backgroundColor: "transparent",
                backgroundImage: "none",
                backdropFilter: "blur(12px)"
              }
            }
          }
        }
      }),
    []
  );

  const [manifest, setManifest] = useState<FirmwareManifest | null>(null);
  const [manifestError, setManifestError] = useState<string | null>(null);
  const [connectionState, setConnectionState] = useState<ConnectionState>("idle");
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [deviceInfo, setDeviceInfo] = useState<DeviceInfo | null>(null);
  const [progress, setProgress] = useState<number>(0);
  const [selectedPackageId, setSelectedPackageId] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState<ActiveTab>("builtin");
  const [customFile, setCustomFile] = useState<File | null>(null);
  const [customAddress, setCustomAddress] = useState<string>("0x0000");
  const [customFlashSize, setCustomFlashSize] = useState<string>("keep");
  const [customFlashMode, setCustomFlashMode] = useState<string>("keep");
  const [customFlashFreq, setCustomFlashFreq] = useState<string>("keep");
  const [customEraseAll, setCustomEraseAll] = useState<boolean>(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [baudRate, setBaudRate] = useState<number>(DEFAULT_BAUD);

  const transportRef = useRef<Transport | null>(null);
  const loaderRef = useRef<ESPLoader | null>(null);
  const fileInputRef = useRef<HTMLInputElement | null>(null);

  const appendLog = useCallback(
    (message: string, variant: LogVariant = "info") => {
      setLogs((prev) => [
        ...prev,
        {
          id: createId(),
          timestamp: new Date().toLocaleTimeString(),
          message,
          variant
        }
      ]);
    },
    [setLogs]
  );

  const terminalRef = useRef<IEspLoaderTerminal | null>(null);
  useEffect(() => {
    terminalRef.current = {
      clean() {
        // Don't clear logs during connection - we need to see the full flow
        appendLog("[ESPLoader called terminal.clean() - ignoring to preserve debug logs]", "trace");
      },
      write(data: string) {
        appendLog(data, "trace");
      },
      writeLine(data: string) {
        appendLog(data, "trace");
      }
    };
  }, [appendLog]);

  useEffect(() => {
    const loadManifest = async () => {
      try {
        const manifestPath = `${import.meta.env.BASE_URL}firmware/manifest.json`;
        const res = await fetch(manifestPath, { cache: "no-store" });
        if (!res.ok) {
          throw new Error(`Manifest not found (HTTP ${res.status})`);
        }
        const json = (await res.json()) as FirmwareManifest;
        setManifest(json);
        if (json.defaultBaud) {
          setBaudRate(json.defaultBaud);
        }
        if (json.packages && json.packages.length > 0) {
          setSelectedPackageId(json.packages[0].id);
        }
        setManifestError(null);
      } catch (err) {
        console.warn("Failed to load firmware manifest", err);
        setManifestError(
          `Firmware manifest was not found. Prebuilt options will be hidden until you add ${import.meta.env.BASE_URL}firmware/manifest.json.`
        );
        setManifest(null);
      }
    };

    loadManifest();
  }, []);

  useEffect(() => {
    if (!selectedPackageId && manifest?.packages && manifest.packages.length > 0) {
      setSelectedPackageId(manifest.packages[0].id);
    }
  }, [manifest, selectedPackageId]);

  const selectedPackage = useMemo(() => {
    if (!manifest?.packages) {
      return null;
    }
    return manifest.packages.find((pkg) => pkg.id === selectedPackageId) ?? null;
  }, [manifest, selectedPackageId]);

  const isWebSerialSupported = typeof navigator !== "undefined" && "serial" in navigator;
  const flashing = connectionState === "flashing";
  const connected = connectionState === "connected" || connectionState === "flashing";

  const clearLogs = useCallback(() => setLogs([]), []);

  const disconnect = useCallback(async () => {
    if (transportRef.current) {
      try {
        await transportRef.current.disconnect();
      } catch (err) {
        console.warn("Error during transport disconnect", err);
      }
    }
    transportRef.current = null;
    loaderRef.current = null;
    setDeviceInfo(null);
    setConnectionState("idle");
  }, []);

  const handleDisconnect = useCallback(async () => {
    appendLog("Disconnecting from device…");
    await disconnect();
    appendLog("Serial port released", "success");
  }, [appendLog, disconnect]);

  const handleConnect = useCallback(async () => {
    if (!isWebSerialSupported) {
      setErrorMessage("This browser does not support the Web Serial API. Use Chrome or Edge 89+. ");
      return;
    }

    setErrorMessage(null);
    setProgress(0);
    setConnectionState("connecting");
    appendLog("==== NEW CONNECTION ATTEMPT ====", "info");

    // IMMEDIATE check: what ports does browser know about?
    try {
      const serial = navigator.serial;
      if (serial && typeof serial.getPorts === "function") {
        const existingPorts = await serial.getPorts();
        appendLog(`[STARTUP CHECK] Browser has ${existingPorts.length} port(s) in memory`, "info");
        existingPorts.forEach((p, idx) => {
          const info = p.getInfo?.() || {};
          const vid = info.usbVendorId != null ? `0x${info.usbVendorId.toString(16).padStart(4, "0")}` : "n/a";
          const pid = info.usbProductId != null ? `0x${info.usbProductId.toString(16).padStart(4, "0")}` : "n/a";
          const readable = Boolean(p.readable);
          const writable = Boolean(p.writable);
          const rLocked = p.readable?.locked ?? false;
          const wLocked = p.writable?.locked ?? false;
          appendLog(
            `[STARTUP CHECK] Port ${idx}: VID ${vid} PID ${pid} | readable=${readable}(${rLocked}) writable=${writable}(${wLocked})`,
            "info"
          );
        });
      }
    } catch (preCheckErr) {
      appendLog(`[STARTUP CHECK] Failed: ${(preCheckErr as Error).message}`, "trace");
    }

    appendLog("Requesting serial port access…");

    try {
      // Cleanup any previous session first
      if (transportRef.current) {
        appendLog("Cleaning up previous Transport reference…", "trace");
        try {
          await transportRef.current.disconnect();
          appendLog("Previous transport disconnected", "trace");
        } catch (cleanupErr) {
          appendLog(`Warning: failed to release previous transport: ${(cleanupErr as Error).message}`, "trace");
        }
        transportRef.current = null;
        await delay(200); // Give browser time to release resources
      }

      if (loaderRef.current) {
        appendLog("Clearing previous ESPLoader reference…", "trace");
        loaderRef.current = null;
      }

      const filters = [
        { usbVendorId: 0x303a }, // Espressif native USB
        { usbVendorId: 0x239a }, // Adafruit boards
        { usbVendorId: 0x10c4 }, // Silicon Labs CP210x
        { usbVendorId: 0x1a86 }, // WCH CH34x
        { usbVendorId: 0x0403 }, // FTDI FT232/FT2232
        { usbVendorId: 0x2341 }, // Arduino (SAM/AVR bridges)
        { usbVendorId: 0x2e8a } // Raspberry Pi Pico / RP2040 USB stack
      ];

      const serial = navigator.serial;
      if (!serial) {
        throw new Error("Serial API unavailable on this navigator instance.");
      }

      const describePort = (candidate: SerialPort | null | undefined) => {
        if (!candidate) {
          return "<unknown serial port>";
        }

        try {
          const info = candidate.getInfo?.();
          if (!info) {
            return "<serial port>";
          }

          const vid = info.usbVendorId != null ? `0x${info.usbVendorId.toString(16).padStart(4, "0")}` : "n/a";
          const pid = info.usbProductId != null ? `0x${info.usbProductId.toString(16).padStart(4, "0")}` : "n/a";
          return `VID ${vid} PID ${pid}`;
        } catch (err) {
          console.warn("Unable to describe serial port", err);
          return "<serial port>";
        }
      };

      const describePortState = (candidate: SerialPort | null | undefined, label: string) => {
        if (!candidate) {
          appendLog(`${label}: port is null/undefined`, "trace");
          return;
        }

        const hasReadable = Boolean(candidate.readable);
        const hasWritable = Boolean(candidate.writable);
        const readableLocked = candidate.readable?.locked ?? false;
        const writableLocked = candidate.writable?.locked ?? false;

        appendLog(
          `${label} state: readable=${hasReadable} (locked=${readableLocked}), writable=${hasWritable} (locked=${writableLocked})`,
          "trace"
        );
      };

      const releasePortLocks = async (candidate: SerialPort, context: string) => {
        if (!candidate) {
          return;
        }

        describePortState(candidate, `${context} - before lock release`);

        // Release readable lock if present
        if (candidate.readable?.locked) {
          appendLog(`${context}: releasing readable lock…`, "trace");
          try {
            const reader = candidate.readable.getReader();
            await reader.cancel();
            reader.releaseLock();
            appendLog(`${context}: readable lock released`, "trace");
            await delay(100);
          } catch (readerErr) {
            appendLog(`${context}: failed to release readable lock: ${(readerErr as Error).message}`, "error");
          }
        }

        // Release writable lock if present
        if (candidate.writable?.locked) {
          appendLog(`${context}: releasing writable lock…`, "trace");
          try {
            const writer = candidate.writable.getWriter();
            try {
              await writer.close();
            } catch (closeErr) {
              // Writer might already be closing, ignore
            }
            writer.releaseLock();
            appendLog(`${context}: writable lock released`, "trace");
            await delay(100);
          } catch (writerErr) {
            appendLog(`${context}: failed to release writable lock: ${(writerErr as Error).message}`, "error");
          }
        }

        describePortState(candidate, `${context} - after lock release`);
      };

      const closePortIfOpen = async (candidate: SerialPort, context: string) => {
        if (!candidate) {
          return;
        }

        const isOpen = Boolean(candidate.readable || candidate.writable);
        if (!isOpen) {
          appendLog(`${context}: port not open, skipping close`, "trace");
          return;
        }

        appendLog(`${context}: closing serial session for ${describePort(candidate)}…`, "trace");

        // First release any locks
        await releasePortLocks(candidate, context);

        // Now attempt to close the port
        try {
          await candidate.close();
          appendLog(`${context}: port closed successfully`, "trace");
          await delay(150);
        } catch (closeErr) {
          const domError = closeErr as DOMException;
          appendLog(
            `${context}: close failed - ${domError.name}: ${domError.message}`,
            "error"
          );
          throw closeErr; // Re-throw to signal failure
        }
      };

      if (typeof serial.getPorts === "function") {
        try {
          appendLog("Checking for previously opened Web Serial ports…", "trace");
          const knownPorts = await serial.getPorts();
          appendLog(`Browser reported ${knownPorts.length} known port(s).`, "trace");
          for (const candidate of knownPorts) {
            appendLog(`Inspecting known port ${describePort(candidate)}…`, "trace");
            describePortState(candidate, `Known port ${describePort(candidate)}`);

            try {
              await closePortIfOpen(candidate, "previous");
            } catch (prevCloseErr) {
              appendLog(
                `Failed to close previous port ${describePort(candidate)}: ${(prevCloseErr as Error).message}. Will try to continue anyway.`,
                "error"
              );
            }
          }
        } catch (portsErr) {
          appendLog(`Unable to enumerate existing Web Serial ports: ${(portsErr as Error).message}`, "trace");
        }
      }

      appendLog(`Opening serial picker with ${filters.length} USB filter(s)…`, "trace");
      appendLog(`Browser: ${navigator.userAgent}`, "trace");
      appendLog(`Platform: ${navigator.platform}`, "trace");

      const requestFilteredPort = async () => serial.requestPort({ filters });
      let port: SerialPort;

      try {
        port = await requestFilteredPort();
        appendLog(`User selected: ${describePort(port)}`, "trace");
        describePortState(port, "Immediately after selection");
      } catch (requestErr) {
        const domError = requestErr as DOMException;
        const message = domError?.message ?? "";
        const normalizedMessage = message.toLowerCase();

        if (domError?.name === "NotFoundError" && message.includes("No port selected")) {
          appendLog("Port selection dialog was closed without choosing a device.", "trace");
          throw new Error("Serial port selection was cancelled.");
        }

        if (normalizedMessage.includes("no compatible")) {
          appendLog("No devices matched the known USB IDs. Showing all serial ports as a fallback…", "trace");
          port = await serial.requestPort();
        } else {
          throw requestErr;
        }
      }

      appendLog(`Selected port ${describePort(port)}. Preparing connection…`, "trace");
      describePortState(port, "Before closePortIfOpen");

      try {
        await closePortIfOpen(port, "selected");
      } catch (closeErr) {
        appendLog(
          `Failed to close port before connecting. This may indicate a browser/OS lock issue. Error: ${(closeErr as Error).message}`,
          "error"
        );
        // Continue anyway - the port might not have been open
      }

      describePortState(port, "After closePortIfOpen, before Transport creation");

      // Create Transport but DON'T connect yet - ESPLoader.main() will handle connection
      let transport = new Transport(port, true, true); // Enable tracing to debug port issues
      transportRef.current = transport;

      appendLog(`Transport created for ${describePort(port)}. ESPLoader will handle connection...`, "trace");

      const loader = new ESPLoader({
        transport,
        baudrate: baudRate,
        romBaudrate: 115200,
        terminal: terminalRef.current ?? undefined
      });

      loaderRef.current = loader;
      transportRef.current = transport;

      // ESPLoader.main() will connect the transport internally
      appendLog(`Calling ESPLoader.main() to initiate bootloader handshake...`, "trace");
      await loader.main();
      appendLog("Bootloader handshake successful", "success");

      try {
        await loader.runStub();
        appendLog("Flasher stub uploaded", "success");
      } catch (stubErr) {
        appendLog(`Stub load skipped: ${(stubErr as Error).message}`, "trace");
      }

      const chipName = loader.chip?.CHIP_NAME ?? "Unknown";
      let flashId: string | undefined;
      try {
        const id = await loader.readFlashId();
        flashId = loader.toHex(id);
      } catch (idErr) {
        appendLog(`Unable to read flash ID: ${(idErr as Error).message}`, "error");
      }

      let flashSizeBytes: number | undefined;
      try {
        flashSizeBytes = await loader.getFlashSize();
      } catch (sizeErr) {
        appendLog(`Unable to determine flash size: ${(sizeErr as Error).message}`, "trace");
      }

      const portInfo = transport.getInfo();

      setDeviceInfo({
        chip: chipName,
        flashId,
        flashSizeBytes,
        portInfo
      });

      appendLog(`Connected to ${chipName}`, "success");
      setConnectionState("connected");
    } catch (err) {
      console.error(err);
      appendLog(`Connection error: ${(err as Error).message}`, "error");
      setErrorMessage((err as Error).message);
      await disconnect();
    }
  }, [appendLog, baudRate, disconnect, isWebSerialSupported]);

  const ensureLoader = useCallback(() => {
    const loader = loaderRef.current;
    if (!loader) {
      throw new Error("Device is not connected. Connect to the board before flashing.");
    }
    return loader;
  }, []);

  const parseAddress = (value: string) => {
    const trimmed = value.trim();
    if (trimmed.startsWith("0x") || trimmed.startsWith("0X")) {
      return parseInt(trimmed, 16);
    }
    return parseInt(trimmed, 10);
  };

  const loadFirmwareFromManifest = useCallback(
    async (pkg: FirmwarePackage, loader: ESPLoader) => {
      if (!pkg.images || pkg.images.length === 0) {
        throw new Error(`Firmware package '${pkg.name}' does not define any images.`);
      }

      const files = [] as { data: string; address: number }[];

      for (const image of pkg.images) {
        // Prepend base URL if path doesn't start with http:// or https://
        const imagePath = image.path.startsWith('http') 
          ? image.path 
          : `${import.meta.env.BASE_URL}${image.path}`;
        
        appendLog(`Fetching ${imagePath}…`, "trace");
        const response = await fetch(`${imagePath}?cache-bust=${Date.now()}`);
        if (!response.ok) {
          throw new Error(`Failed to fetch ${imagePath} (HTTP ${response.status})`);
        }
        const buffer = await response.arrayBuffer();
        const bytes = new Uint8Array(buffer);
        const data = loader.ui8ToBstr(bytes);
        files.push({ data, address: parseAddress(image.address) });
      }

      return {
        files,
        flashSize: pkg.flashSize ?? "keep",
        flashMode: pkg.flashMode ?? "keep",
        flashFreq: pkg.flashFreq ?? "keep",
        eraseAll: pkg.eraseAll ?? false
      };
    },
    [appendLog]
  );

  const loadFirmwareFromCustom = useCallback(
    async (
      file: File,
      loader: ESPLoader,
      options: { address: string; flashSize: string; flashMode: string; flashFreq: string; eraseAll: boolean }
    ) => {
      appendLog(`Reading ${file.name}…`, "trace");
      const buffer = await file.arrayBuffer();
      const bytes = new Uint8Array(buffer);
      const data = loader.ui8ToBstr(bytes);

      return {
        files: [{ data, address: parseAddress(options.address) }],
        flashSize: options.flashSize,
        flashMode: options.flashMode,
        flashFreq: options.flashFreq,
        eraseAll: options.eraseAll
      };
    },
    [appendLog]
  );

  const resolveFlashSource = useCallback(async (): Promise<FlashSource> => {
    if (activeTab === "builtin") {
      if (!selectedPackage) {
        throw new Error("Select a firmware package to continue.");
      }
      return { kind: "manifest", pkg: selectedPackage };
    }

    if (!customFile) {
      throw new Error("Choose a firmware .bin file to flash.");
    }

    return {
      kind: "custom",
      file: customFile,
      address: customAddress,
      flashSize: customFlashSize,
      flashMode: customFlashMode,
      flashFreq: customFlashFreq,
      eraseAll: customEraseAll
    };
  }, [activeTab, customAddress, customEraseAll, customFile, customFlashFreq, customFlashMode, customFlashSize, selectedPackage]);

  const handleFlash = useCallback(async () => {
    let loader: ESPLoader;
    try {
      loader = ensureLoader();
    } catch (err) {
      appendLog((err as Error).message, "error");
      setErrorMessage((err as Error).message);
      return;
    }

    try {
      const source = await resolveFlashSource();
      setConnectionState("flashing");
      setProgress(0);
      appendLog("Preparing firmware payload…");

      let configuration: {
        files: { data: string; address: number }[];
        flashSize: string;
        flashMode: string;
        flashFreq: string;
        eraseAll: boolean;
      };

      if (source.kind === "manifest") {
        configuration = await loadFirmwareFromManifest(source.pkg, loader);
      } else {
        configuration = await loadFirmwareFromCustom(source.file, loader, {
          address: source.address,
          flashSize: source.flashSize,
          flashMode: source.flashMode,
          flashFreq: source.flashFreq,
          eraseAll: source.eraseAll
        });
      }

      const flashOptions: FlashOptions = {
        fileArray: configuration.files,
        flashSize: configuration.flashSize,
        flashMode: configuration.flashMode,
        flashFreq: configuration.flashFreq,
        eraseAll: configuration.eraseAll,
        compress: true,
        reportProgress: (_fileIndex, written, total) => {
          const value = Math.floor((written / total) * 100);
          setProgress(value);
        }
      };

      appendLog("Writing firmware to flash…");
      await loader.writeFlash(flashOptions);
      appendLog("Firmware written successfully", "success");
      appendLog("--- Flash Summary ---", "info");
      appendLog(`Chip: ${deviceInfo?.chip || 'Unknown'}`, "info");
      appendLog(`Files written: ${configuration.files.length}`, "info");
      configuration.files.forEach((file, idx) => {
        appendLog(`  [${idx}] 0x${file.address.toString(16).toUpperCase()} (${file.data.length} bytes)`, "info");
      });
      appendLog("--------------------", "info");

      try {
        // Try different reset methods depending on what's available
        if (typeof (loader as any).hardReset === 'function') {
          await (loader as any).hardReset();
          appendLog("Device hard-reset issued - board should reboot now", "success");
        } else {
          await loader.softReset(false);
          appendLog("Device soft-reset issued - board should reboot now", "success");
        }
      } catch (resetErr) {
        appendLog(`Reset failed: ${(resetErr as Error).message}. Manually press the RESET button on your board.`, "trace");
      }

      setConnectionState("connected");
      setProgress(100);
    } catch (err) {
      console.error(err);
      const message = (err as Error).message ?? String(err);
      appendLog(`Flash failed: ${message}`, "error");
      setErrorMessage(message);
      setConnectionState("connected");
    }
  }, [appendLog, ensureLoader, loadFirmwareFromCustom, loadFirmwareFromManifest, resolveFlashSource]);

  const handleCustomFilePick = useCallback((evt: ChangeEvent<HTMLInputElement>) => {
    const file = evt.target.files?.[0];
    if (file) {
      setCustomFile(file);
      appendLog(`Selected ${file.name}`, "info");
    }
  }, [appendLog]);

  const flashDisabled = !connected || flashing;

  const flashButtonLabel = flashing ? "Flashing…" : "Flash Firmware";

  const flashButtonIcon = flashing ? <StopCircleOutlinedIcon /> : <PlayCircleOutlineRoundedIcon />;

  const connectionChipColor = connectionState === "connected" ? "success" : connectionState === "flashing" ? "warning" : connectionState === "connecting" ? "info" : "default";

  const flashSizeDisplay = deviceInfo?.flashSizeBytes
    ? `${(deviceInfo.flashSizeBytes / (1024 * 1024)).toFixed(1)} MB`
    : "Unknown";

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <Box
        sx={{
          minHeight: "100vh",
          background: "linear-gradient(180deg, #10131a 0%, #161c27 100%)",
          color: "text.primary"
        }}
      >
        <AppBar
          position="static"
          elevation={0}
          color="transparent"
          sx={{
            backgroundColor: "rgba(16, 19, 26, 0.72)",
            borderBottom: "1px solid rgba(255, 255, 255, 0.08)",
            boxShadow: "none"
          }}
        >
          <Toolbar>
            <FlashOnRoundedIcon sx={{ mr: 1 }} color="warning" />
            <Typography variant="h6" sx={{ fontWeight: 600 }}>
              M4G Device Manager
            </Typography>
            <Chip
              sx={{ ml: 2 }}
              icon={<UsbIcon />}
              label={connectionState.toUpperCase()}
              color={connectionChipColor}
            />
            <Box sx={{ flexGrow: 1 }} />
            <TextField
              select
              size="small"
              label="Baud"
              value={baudRate}
              onChange={(event: ChangeEvent<HTMLInputElement>) => setBaudRate(Number(event.target.value))}
              sx={{ width: 150, mr: 2 }}
              disabled={connected}
            >
              {[460800, 921600, 1500000].map((option) => (
                <MenuItem key={option} value={option}>
                  {option.toLocaleString()} bps
                </MenuItem>
              ))}
            </TextField>
            {connected ? (
              <Button
                variant="outlined"
                color="inherit"
                startIcon={<RefreshRoundedIcon />}
                onClick={handleDisconnect}
              >
                Disconnect
              </Button>
            ) : (
              <Button
                variant="contained"
                color="primary"
                startIcon={<UsbIcon />}
                onClick={handleConnect}
                disabled={!isWebSerialSupported || connectionState === "connecting"}
              >
                {connectionState === "connecting" ? "Connecting…" : "Connect"}
              </Button>
            )}
          </Toolbar>
        </AppBar>

        <Container maxWidth="lg" sx={{ py: 4 }}>
          {!isWebSerialSupported && (
            <Alert severity="error" sx={{ mb: 2 }}>
              Your browser does not support the Web Serial API. Please use Google Chrome or Microsoft Edge on desktop.
            </Alert>
          )}
          {errorMessage && (
            <Alert severity="error" sx={{ mb: 2 }}>
              {errorMessage}
            </Alert>
          )}
          {manifestError && (
            <Alert severity="warning" sx={{ mb: 2 }}>
              {manifestError} Refer to the README in <code>tools/device-manager-web/public/firmware</code> for details.
            </Alert>
          )}

          <Grid container spacing={3} alignItems="stretch">
            <Grid item xs={12} md={4}>
              <Paper
                elevation={0}
                sx={{
                  p: 3,
                  height: "100%",
                  backgroundColor: "rgba(22, 27, 37, 0.85)",
                  border: "1px solid rgba(255, 255, 255, 0.08)",
                  backdropFilter: "blur(8px)"
                }}
              >
                <Typography variant="h6" gutterBottom>
                  Device Status
                </Typography>
                <Stack spacing={1.5}>
                  <Typography variant="body2" color="text.secondary">
                    Chip
                  </Typography>
                  <Typography variant="subtitle1" fontWeight={600}>
                    {deviceInfo?.chip ?? (connected ? "Detecting…" : "No device")}
                  </Typography>

                  <Divider flexItem sx={{ my: 1 }} />

                  <Typography variant="body2" color="text.secondary">
                    Flash Size
                  </Typography>
                  <Typography variant="subtitle2">{flashSizeDisplay}</Typography>

                  <Typography variant="body2" color="text.secondary">
                    Flash ID
                  </Typography>
                  <Typography variant="subtitle2" sx={{ wordBreak: "break-all" }}>
                    {deviceInfo?.flashId ?? "—"}
                  </Typography>

                  <Typography variant="body2" color="text.secondary">
                    Port Details
                  </Typography>
                  <Typography variant="subtitle2" sx={{ wordBreak: "break-word" }}>
                    {deviceInfo?.portInfo ?? "—"}
                  </Typography>

                  <Divider flexItem sx={{ my: 2 }} />

                  <Typography variant="body2" color="text.secondary">
                    Flash Progress
                  </Typography>
                  <Box>
                    <LinearProgress
                      variant={flashing ? "determinate" : "determinate"}
                      value={flashing ? progress : connectionState === "connected" ? progress : 0}
                      sx={{ height: 10, borderRadius: 5 }}
                    />
                    <Typography variant="caption" color="text.secondary">
                      {flashing ? `${progress}%` : progress > 0 ? `${progress}%` : "—"}
                    </Typography>
                  </Box>
                </Stack>
              </Paper>
            </Grid>

            <Grid item xs={12} md={8}>
              <Paper
                elevation={0}
                sx={{
                  p: 3,
                  height: "100%",
                  backgroundColor: "rgba(22, 27, 37, 0.85)",
                  border: "1px solid rgba(255, 255, 255, 0.08)",
                  backdropFilter: "blur(8px)"
                }}
              >
                <Stack direction="row" alignItems="center" spacing={1} sx={{ mb: 2 }}>
                  <DownloadRoundedIcon color="primary" />
                  <Typography variant="h6">Firmware Packages</Typography>
                </Stack>

                <Tabs
                  value={activeTab}
                  onChange={(_event: SyntheticEvent, value: ActiveTab) => setActiveTab(value)}
                  sx={{ mb: 2 }}
                >
                  <Tab value="builtin" label="Prebuilt" />
                  <Tab value="custom" label="Custom .bin" />
                </Tabs>

                {activeTab === "builtin" && (
                  <Stack spacing={2}>
                    <FormControl fullWidth>
                      <InputLabel id="firmware-select-label">Firmware</InputLabel>
                      <TextField
                        select
                        label="Firmware"
                        value={selectedPackageId ?? ""}
                        onChange={(event: ChangeEvent<HTMLInputElement>) => setSelectedPackageId(event.target.value)}
                        disabled={!manifest?.packages || manifest.packages.length === 0}
                        SelectProps={{
                          MenuProps: {
                            PaperProps: {
                              sx: {
                                maxHeight: 400,
                                '& .MuiMenuItem-root': {
                                  whiteSpace: 'normal',
                                  minHeight: 48,
                                  alignItems: 'flex-start',
                                  py: 1.5
                                }
                              }
                            }
                          }
                        }}
                      >
                        {(manifest?.packages ?? []).map((pkg) => (
                          <MenuItem key={pkg.id} value={pkg.id}>
                            <Stack spacing={0.5} sx={{ width: '100%' }}>
                              <Typography variant="subtitle2" fontWeight={600}>
                                {pkg.name}
                              </Typography>
                              {pkg.description && (
                                <Typography variant="caption" color="text.secondary" sx={{ whiteSpace: 'normal' }}>
                                  {pkg.description}
                                </Typography>
                              )}
                            </Stack>
                          </MenuItem>
                        ))}
                      </TextField>
                    </FormControl>

                    {selectedPackage && (
                      <Box>
                        <Typography variant="body2" color="text.secondary">
                          Flash layout
                        </Typography>
                        <List dense sx={{ maxHeight: 160, overflow: "auto" }}>
                          {(selectedPackage.images ?? []).map((image) => (
                            <ListItem key={`${selectedPackage.id}-${image.address}`} sx={{ py: 0.5 }}>
                              <ListItemText
                                primary={image.path}
                                secondary={`Address: ${image.address}`}
                                primaryTypographyProps={{ variant: "body2" }}
                                secondaryTypographyProps={{ variant: "caption" }}
                              />
                            </ListItem>
                          ))}
                        </List>
                      </Box>
                    )}

                    <Button
                      variant="contained"
                      color="primary"
                      startIcon={flashButtonIcon}
                      onClick={handleFlash}
                      disabled={flashDisabled || (activeTab === "builtin" && !selectedPackage)}
                      size="large"
                      sx={{ alignSelf: "flex-start" }}
                    >
                      {flashButtonLabel}
                    </Button>
                  </Stack>
                )}

                {activeTab === "custom" && (
                  <Stack spacing={2}>
                    <input
                      ref={fileInputRef}
                      type="file"
                      accept=".bin"
                      style={{ display: "none" }}
                      onChange={handleCustomFilePick}
                    />
                    <Button
                      variant="outlined"
                      startIcon={<UploadFileRoundedIcon />}
                      onClick={() => fileInputRef.current?.click()}
                      sx={{ alignSelf: "flex-start" }}
                    >
                      {customFile ? customFile.name : "Choose .bin File"}
                    </Button>

                    <Grid container spacing={2}>
                      <Grid item xs={12} md={6}>
                        <TextField
                          label="Flash Address"
                          value={customAddress}
                          onChange={(event: ChangeEvent<HTMLInputElement>) => setCustomAddress(event.target.value)}
                          helperText="Start offset for the binary"
                          fullWidth
                        />
                      </Grid>
                      <Grid item xs={12} md={6}>
                        <TextField
                          select
                          label="Flash Size"
                          value={customFlashSize}
                          onChange={(event: ChangeEvent<HTMLInputElement>) => setCustomFlashSize(event.target.value)}
                          fullWidth
                        >
                          {SUPPORTED_FLASH_SIZES.map((size) => (
                            <MenuItem key={size} value={size}>
                              {size.toUpperCase()}
                            </MenuItem>
                          ))}
                        </TextField>
                      </Grid>
                      <Grid item xs={6} md={6}>
                        <TextField
                          select
                          label="Flash Mode"
                          value={customFlashMode}
                          onChange={(event: ChangeEvent<HTMLInputElement>) => setCustomFlashMode(event.target.value)}
                          fullWidth
                        >
                          {SUPPORTED_FLASH_MODES.map((mode) => (
                            <MenuItem key={mode} value={mode}>
                              {mode.toUpperCase()}
                            </MenuItem>
                          ))}
                        </TextField>
                      </Grid>
                      <Grid item xs={6} md={6}>
                        <TextField
                          select
                          label="Flash Frequency"
                          value={customFlashFreq}
                          onChange={(event: ChangeEvent<HTMLInputElement>) => setCustomFlashFreq(event.target.value)}
                          fullWidth
                        >
                          {SUPPORTED_FLASH_FREQS.map((freq) => (
                            <MenuItem key={freq} value={freq}>
                              {freq.toUpperCase()}
                            </MenuItem>
                          ))}
                        </TextField>
                      </Grid>
                    </Grid>

                    <FormControlLabel
                      control={
                        <Switch
                          checked={customEraseAll}
                          onChange={(_event: SyntheticEvent, checked: boolean) => setCustomEraseAll(checked)}
                        />
                      }
                      label="Erase entire flash before writing"
                    />

                    <Button
                      variant="contained"
                      color="primary"
                      startIcon={flashButtonIcon}
                      onClick={handleFlash}
                      disabled={flashDisabled || !customFile}
                      size="large"
                      sx={{ alignSelf: "flex-start" }}
                    >
                      {flashButtonLabel}
                    </Button>
                  </Stack>
                )}
              </Paper>
            </Grid>

            <Grid item xs={12}>
              <Paper elevation={0} sx={{ p: 3 }}>
                <Stack direction="row" alignItems="center" spacing={1} sx={{ mb: 2 }}>
                  <Typography variant="h6">Console</Typography>
                  <Tooltip title="Clear console">
                    <IconButton size="small" onClick={clearLogs}>
                      <ClearAllRoundedIcon fontSize="small" />
                    </IconButton>
                  </Tooltip>
                </Stack>
                <Box
                  sx={{
                    backgroundColor: "#0f172a",
                    color: "#e2e8f0",
                    borderRadius: 2,
                    p: 2,
                    minHeight: 220,
                    maxHeight: 360,
                    overflowY: "auto",
                    fontFamily: "Source Code Pro, ui-monospace, SFMono-Regular",
                    fontSize: 13
                  }}
                >
                  {logs.length === 0 ? (
                    <Typography variant="body2" color="#94a3b8">
                      Console output will appear here once you connect to a device.
                    </Typography>
                  ) : (
                    logs.map((log) => (
                      <Box key={log.id} sx={{ mb: 1 }}>
                        <Typography component="span" sx={{ color: "#94a3b8", mr: 1 }}>
                          [{log.timestamp}]
                        </Typography>
                        <Typography
                          component="span"
                          sx={{
                            color:
                              log.variant === "error"
                                ? "#f87171"
                                : log.variant === "success"
                                  ? "#34d399"
                                  : log.variant === "trace"
                                    ? "#60a5fa"
                                    : "#f8fafc"
                          }}
                        >
                          {log.message}
                        </Typography>
                      </Box>
                    ))
                  )}
                </Box>
                <Typography variant="caption" color="text.secondary" sx={{ mt: 1, display: "block" }}>
                  Need help? Read the flashing guide in the repository README or visit the <Link href="https://github.com/espressif/esptool-js" target="_blank" rel="noreferrer">esptool-js docs</Link>.
                </Typography>
              </Paper>
            </Grid>
          </Grid>
        </Container>
      </Box>
    </ThemeProvider>
  );
}
