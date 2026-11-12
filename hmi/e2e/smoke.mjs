// HMI 시연 점검 (REQ-HMI-001, REQ-HMI-002, REQ-COM-008): 실제 GW + SIM + HMI(vite dev)를 띄우고
// 브라우저(Playwright chromium)로 화면을 확인한다.
//
//   node e2e/smoke.mjs [--screenshot <png>]
//
// 전제: gateway/build/ugvc_gateway 빌드, python3, 전역 playwright(require('playwright')).
// 포트는 모두 OS가 고른 빈 포트를 쓴다 (개발 중인 GW·HMI와 충돌하지 않는다).
import { spawn } from "node:child_process";
import { createRequire } from "node:module";
import { createServer } from "node:net";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_PATH ?? "playwright");

const HMI = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(HMI, "..");
const GW_BIN = process.env.UGVC_GATEWAY_BIN ?? resolve(REPO, "gateway/build/ugvc_gateway");
const shotArg = process.argv.indexOf("--screenshot");
const screenshot = shotArg > 0 ? process.argv[shotArg + 1] : null;

const children = [];
const step = (msg) => console.log(`· ${msg}`);

function freePort() {
  return new Promise((ok) => {
    const s = createServer().listen(0, "127.0.0.1", () => {
      const { port } = s.address();
      s.close(() => ok(port));
    });
  });
}

function run(cmd, args, opts = {}) {
  // detached: 프로세스 그룹째 끝낼 수 있게 (npx → vite처럼 자식을 띄우는 명령)
  const p = spawn(cmd, args, { stdio: ["ignore", "pipe", "pipe"], detached: true, ...opts });
  children.push(p);
  return p;
}

/** GW 실행 후 시작 메시지에서 IF-03 포트를 읽는다. */
function startGateway(ports, wsPort) {
  const gw = run(GW_BIN, [
    "--vehicle-port", String(ports.vehicle), "--detector-port", String(ports.detector),
    "--sim-port", String(ports.sim), "--ws-port", String(wsPort),
  ]);
  gw.stdout.resume(); // IF-03 stdout은 버린다 (파이프가 차서 GW가 막히지 않도록)
  return new Promise((ok, fail) => {
    let buf = "";
    gw.stderr.on("data", (d) => {
      buf += d;
      const m = buf.match(/ws:\/\/[^:]+:(\d+)\/ws/);
      if (m) ok({ proc: gw, wsPort: Number(m[1]) });
    });
    gw.on("exit", (c) => fail(new Error(`GW exited ${c}: ${buf}`)));
  });
}

function startSim(port) {
  return run("python3", ["-m", "ugvc_sim.telemetry_sender", "--port", String(port)], {
    cwd: resolve(REPO, "sim"),
    env: { ...process.env, PYTHONPATH: `${resolve(REPO, "python")}:.` },
  });
}

function startVite(port, gwUrl) {
  const vite = run("npx", ["vite", "--port", String(port), "--strictPort"], {
    cwd: HMI,
    env: { ...process.env, VITE_GW_URL: gwUrl },
  });
  return new Promise((ok) => vite.stdout.on("data", (d) => String(d).includes("Local:") && ok(vite)));
}

async function stop(p) {
  if (p.exitCode !== null || p.signalCode !== null) return;
  const exited = new Promise((ok) => p.once("exit", ok));
  process.kill(-p.pid, "SIGTERM");
  await exited;
}

async function expectText(page, testId, expected, timeout = 5000) {
  const loc = page.getByTestId(testId);
  await loc.filter({ hasText: expected }).first().waitFor({ timeout }).catch(async () => {
    throw new Error(`[${testId}] expected "${expected}", got "${await loc.first().innerText()}"`);
  });
}

async function main() {
  const ports = { vehicle: await freePort(), detector: await freePort(), sim: await freePort() };
  let gw = await startGateway(ports, 0);
  const gwUrl = `ws://127.0.0.1:${gw.wsPort}/ws`;
  const vite = await startVite(await freePort(), gwUrl);
  const hmiUrl = vite.spawnargs.includes("--port") ? `http://127.0.0.1:${vite.spawnargs[vite.spawnargs.indexOf("--port") + 1]}/` : "";

  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1280, height: 760 } });
  page.on("pageerror", (e) => { throw e; });
  await page.goto(hmiUrl);

  step("GW 접속 직후: 연결됨, 차량 링크 LOST (TC-HMI-002-03)");
  await expectText(page, "connection-banner", "GW 연결됨");
  await expectText(page, "link-VEHICLE", "LOST");

  step("SIM 송신: 차량 링크 CONNECTED, 배터리·모드 표시, 궤적 증가 (TC-HMI-001-03, TC-HMI-002-03)");
  const sim = startSim(ports.vehicle);
  await expectText(page, "link-VEHICLE", "CONNECTED");
  await expectText(page, "mode", "MISSION");
  await page.getByTestId("battery").waitFor();
  // 직선 주행이라 Leaflet이 궤적을 두 점으로 단순화한다 → 점 개수 대신 궤적 끝이 계속 움직이는지 본다
  const trackEnd = async () => (await page.locator(".leaflet-overlay-pane path").getAttribute("d"))?.split("L").at(-1);
  const end1 = await trackEnd();
  await page.waitForTimeout(5000);
  const end2 = await trackEnd();
  if (!end1 || end1 === end2) throw new Error(`track not growing: ${end1} -> ${end2}`);
  if ((await page.locator(".vehicle-icon").count()) !== 1) throw new Error("vehicle marker missing");
  if (screenshot) await page.screenshot({ path: screenshot });

  step("SIM 중지: 1초 후 차량 링크 LOST (TC-HMI-002-03)");
  await stop(sim);
  await expectText(page, "link-VEHICLE", "LOST", 3000);

  step("GW 중지: 'GW 연결 끊김' (TC-COM-008-16)");
  await stop(gw.proc);
  await expectText(page, "connection-banner", "GW 연결 끊김", 3000);
  if ((await page.locator("aside[data-stale=true]").count()) !== 1) throw new Error("status panel not marked stale");

  step("GW 재시작(같은 포트): 자동 재접속 (TC-COM-008-16)");
  gw = await startGateway(ports, gw.wsPort);
  await expectText(page, "connection-banner", "GW 연결됨", 8000);

  await browser.close();
  console.log("PASS");
}

main()
  .catch((e) => {
    console.error("FAIL:", e.message);
    process.exitCode = 1;
  })
  .finally(async () => {
    for (const c of children) await stop(c).catch(() => {});
  });
