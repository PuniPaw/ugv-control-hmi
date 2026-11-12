// HMI 시연 점검 (STP 수준 D: TC-HMI-001-03, TC-HMI-002-03, TC-HMI-006-01)
// 실제 GW + SIM + HMI(vite dev)를 띄우고 브라우저(Playwright chromium)로 화면을 확인한다.
//
//   node e2e/smoke.mjs [--screenshot <png>] [--clock-skew <ms>]
//
// --clock-skew: 페이지의 Date.now()를 그만큼 옮긴다 (HMI PC 시계가 GW PC와 다른 배치, REQ-HMI-006).
//
// 전제: gateway/build/ugvc_gateway 빌드, python3, playwright 패키지(전역이면 NODE_PATH=$(npm root -g)).
// 포트는 모두 OS가 고른 빈 포트를 쓴다 (개발 중인 GW·HMI와 충돌하지 않는다).
// 판정은 화면 문구의 부분 일치가 아니라 data-level·data-stale 속성으로 한다.
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
const START_TIMEOUT_MS = 15000;
const shotArg = process.argv.indexOf("--screenshot");
const screenshot = shotArg > 0 ? process.argv[shotArg + 1] : null;
const skewArg = process.argv.indexOf("--clock-skew");
const clockSkewMs = skewArg > 0 ? Number(process.argv[skewArg + 1]) : 0;

const children = [];
const pageErrors = [];
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

/** 출력에서 pattern이 나올 때까지 기다린다. 먼저 끝나거나 시간이 지나면 실패 */
function waitOutput(p, stream, pattern, what) {
  return new Promise((ok, fail) => {
    let buf = "";
    const timer = setTimeout(() => fail(new Error(`${what}: ${START_TIMEOUT_MS}ms 안에 시작하지 않음\n${buf}`)), START_TIMEOUT_MS);
    p[stream].on("data", (d) => {
      buf += d;
      const m = buf.match(pattern);
      if (m) {
        clearTimeout(timer);
        ok(m);
      }
    });
    p.on("exit", (c) => {
      clearTimeout(timer);
      fail(new Error(`${what} 종료 (${c})\n${buf}`));
    });
  });
}

async function startGateway(ports, wsPort) {
  const gw = run(GW_BIN, [
    "--vehicle-port", String(ports.vehicle), "--detector-port", String(ports.detector),
    "--sim-port", String(ports.sim), "--ws-port", String(wsPort),
  ]);
  gw.stdout.resume(); // IF-03 stdout은 버린다 (파이프가 차서 GW가 막히지 않도록)
  const m = await waitOutput(gw, "stderr", /ws:\/\/[^:]+:(\d+)\/ws/, "GW");
  gw.stderr.resume();
  return { proc: gw, wsPort: Number(m[1]) };
}

function startSim(port) {
  return run("python3", ["-m", "ugvc_sim.telemetry_sender", "--port", String(port)], {
    cwd: resolve(REPO, "sim"),
    env: { ...process.env, PYTHONPATH: `${resolve(REPO, "python")}:.` },
  });
}

async function startVite(port, gwUrl) {
  const vite = run("npx", ["vite", "--port", String(port), "--strictPort"], {
    cwd: HMI,
    env: { ...process.env, VITE_GW_URL: gwUrl },
  });
  await waitOutput(vite, "stdout", /Local:/, "vite");
  return vite;
}

async function stop(p) {
  if (p.exitCode !== null || p.signalCode !== null) return;
  const exited = new Promise((ok) => p.once("exit", ok));
  process.kill(-p.pid, "SIGTERM");
  await exited;
}

/** testid 요소의 속성이 기대값이 될 때까지 기다린다 */
async function expectAttr(page, testId, attr, value, timeout = 5000) {
  const loc = page.locator(`[data-testid="${testId}"][${attr}="${value}"]`);
  await loc.first().waitFor({ timeout }).catch(async () => {
    const actual = await page.getByTestId(testId).first().getAttribute(attr);
    throw new Error(`[${testId}] ${attr} 기대 "${value}", 실제 "${actual}"`);
  });
}

/** testid 요소의 텍스트가 정확히 기대값(문자열) 또는 정규식과 맞을 때까지 기다린다 */
async function expectText(page, testId, expected, timeout = 5000) {
  const loc = page.getByTestId(testId).first();
  const match = (t) => (expected instanceof RegExp ? expected.test(t) : t.trim() === expected);
  const deadline = Date.now() + timeout;
  let text = "";
  while (Date.now() < deadline) {
    text = (await loc.innerText().catch(() => "")).trim();
    if (match(text)) return;
    await page.waitForTimeout(100);
  }
  throw new Error(`[${testId}] 기대 ${expected}, 실제 "${text}"`);
}

function checkPageErrors() {
  if (pageErrors.length) throw new Error(`페이지 오류: ${pageErrors.join(" | ")}`);
}

async function scenario(browser) {
  const ports = { vehicle: await freePort(), detector: await freePort(), sim: await freePort() };
  let gw = await startGateway(ports, 0);
  const vitePort = await freePort();
  await startVite(vitePort, `ws://127.0.0.1:${gw.wsPort}/ws`);

  const page = await browser.newPage({ viewport: { width: 1280, height: 760 } });
  page.on("pageerror", (e) => pageErrors.push(e.message));
  if (clockSkewMs !== 0) {
    await page.addInitScript((skew) => {
      const real = Date.now.bind(Date);
      Date.now = () => real() + skew;
    }, clockSkewMs);
    step(`HMI 시계를 GW보다 ${clockSkewMs}ms 옮김`);
  }
  await page.goto(`http://127.0.0.1:${vitePort}/`);

  step("1. GW 접속: 접속 직후 stats로 FRESH(배너 ok), 차량 링크 LOST");
  await expectAttr(page, "connection-banner", "data-level", "ok");
  await expectText(page, "banner-text", "GW 연결됨");
  await expectText(page, "link-state-VEHICLE", "LOST");
  checkPageErrors();

  step("2. SIM 10Hz 송신: 차량 링크 CONNECTED, 유실률·모드·배터리 표시, 마커와 궤적 이동, 차량 값 진하게");
  const sim = startSim(ports.vehicle);
  await expectText(page, "link-state-VEHICLE", "CONNECTED");
  await expectText(page, "loss-VEHICLE", "0.0%", 3000); // GW stats가 반영됨
  await expectText(page, "age-VEHICLE", /^0\.\ds 전$/); // 시계 차와 무관하게 0.x초 (음수·수 초가 아님)
  await expectText(page, "mode", "MISSION");
  await expectText(page, "battery", /^\d+%$/);
  await expectAttr(page, "vehicle", "data-stale", "false");
  if ((await page.locator(".vehicle-icon").count()) !== 1) throw new Error("차량 마커가 없음");
  // 직선 주행이라 Leaflet이 궤적을 두 점으로 단순화한다 → 점 개수 대신 궤적 끝이 계속 움직이는지 본다
  const trackEnd = async () => (await page.locator(".leaflet-overlay-pane path").last().getAttribute("d"))?.split("L").at(-1);
  const end1 = await trackEnd();
  await page.waitForTimeout(5000);
  const end2 = await trackEnd();
  if (!end1 || end1 === end2) throw new Error(`궤적이 늘지 않음: ${end1} -> ${end2}`);
  if (screenshot) await page.screenshot({ path: screenshot });
  checkPageErrors();

  step("3. SIM 중지: 차량 링크 LOST, 차량 값 '이전 값'으로 흐림, 배너는 그대로 ok");
  await stop(sim);
  await expectText(page, "link-state-VEHICLE", "LOST", 3000);
  await expectAttr(page, "vehicle", "data-stale", "true");
  await expectAttr(page, "connection-banner", "data-level", "ok");
  checkPageErrors();

  step("4. GW 중지: 'GW 연결 끊김 · N초 후 재접속', 패널 전체 흐림");
  await stop(gw.proc);
  await expectAttr(page, "connection-banner", "data-level", "bad", 3000);
  await expectText(page, "banner-text", /^GW 연결 끊김 · (\d+초 후 재접속|재접속 중)$/);
  await expectAttr(page, "status-panel", "data-stale", "true");
  checkPageErrors();

  step("5. GW 재시작(같은 포트): 자동 재접속, 새 stats로 다시 FRESH");
  gw = await startGateway(ports, gw.wsPort);
  await expectAttr(page, "connection-banner", "data-level", "ok", 8000);
  await expectAttr(page, "status-panel", "data-stale", "false");
  checkPageErrors();
}

let browser = null;
try {
  browser = await chromium.launch();
  await scenario(browser);
  console.log("PASS");
} catch (e) {
  console.error("FAIL:", e.message);
  process.exitCode = 1;
} finally {
  await browser?.close().catch(() => {});
  for (const c of children) await stop(c).catch(() => {});
}
