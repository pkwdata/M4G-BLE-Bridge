interface SerialPortInfo {
  usbVendorId?: number;
  usbProductId?: number;
}

interface SerialOptions {
  baudRate: number;
}

interface SerialPort {
  open(options: SerialOptions): Promise<void>;
  close(): Promise<void>;
  getInfo(): SerialPortInfo;
  readable?: ReadableStream<Uint8Array> | null;
  writable?: WritableStream<Uint8Array> | null;
}

interface SerialPortRequestOption {
  usbVendorId?: number;
  usbProductId?: number;
}

interface Serial {
  requestPort(options?: { filters?: SerialPortRequestOption[] }): Promise<SerialPort>;
  getPorts?: () => Promise<SerialPort[]>;
}

interface Navigator {
  serial?: Serial;
}

declare module "@mui/icons-material/*" {
  import type { SvgIconComponent } from "@mui/icons-material";
  const icon: SvgIconComponent;
  export default icon;
}

declare module "vite/client" {
  interface ImportMetaEnv {
    readonly MODE: string;
    readonly BASE_URL: string;
    readonly PROD: boolean;
    readonly DEV: boolean;
    readonly [key: string]: string | boolean | undefined;
  }

  interface ImportMeta {
    readonly env: ImportMetaEnv;
    readonly hot?: {
      readonly accept: (...args: unknown[]) => void;
      readonly dispose: (...args: unknown[]) => void;
    };
  }
}
