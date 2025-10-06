import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  base: '/M4G-BLE-Bridge/',  // GitHub Pages base path
  build: {
    sourcemap: true
  },
  server: {
    port: 5173,
    open: true
  }
});
