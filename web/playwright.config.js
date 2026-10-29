import { defineConfig, devices } from "@playwright/test";

export default defineConfig({
  testDir: "./tests",
  fullyParallel: false,
  workers: 1,
  timeout: 30000,
  use: {
    baseURL: "http://127.0.0.1:8766/dist/",
    trace: "retain-on-failure"
  },
  webServer: {
    command: "python3 -m http.server 8766 --bind 127.0.0.1",
    url: "http://127.0.0.1:8766/dist/index.html",
    reuseExistingServer: !process.env.CI
  },
  projects: [
    { name: "chromium", use: { ...devices["Desktop Chrome"] } },
    { name: "firefox", use: { ...devices["Desktop Firefox"] } }
  ]
});
