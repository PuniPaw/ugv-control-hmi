// HMI 시연 점검 (STP 수준 D: TC-HMI-001-03, TC-HMI-002-03, TC-HMI-003-08, TC-HMI-004-07, TC-HMI-006-01, TC-HMI-006-11, TC-CMD-001-08·09, TC-CMD-004-02)
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
  // 진단(stderr)은 모아 둔다: 명령이 실제로 SIM에 나갔는지(`cmd sent`)를 화면이 아니라 GW 기록으로 확인
  let log = "";
  gw.stderr.on("data", (d) => (log += d));
  const m = await waitOutput(gw, "stderr", /ws:\/\/[^:]+:(\d+)\/ws/, "GW");
  /** 이 GW가 SIM에 보낸 명령의 req_id, 처음 보낸 순서대로 (`cmd sent`는 재전송마다 찍히므로 같은 req_id는 하나로) */
  const sentReqIds = () => [...new Set([...log.matchAll(/cmd sent req="([^"]*)"/g)].map((x) => x[1]))];
  return { proc: gw, wsPort: Number(m[1]), sentReqIds };
}

function startSim(ports, extraArgs = []) {
  // SIM은 GW의 HEARTBEAT·COMMAND를 sim 포트로 받고 TELEMETRY·ACK를 vehicle 포트로 보낸다 (HEARTBEAT가 없으면 3초 뒤 Failsafe 정지)
  const sim = run("python3", ["-m", "ugvc_sim", "--port", String(ports.sim), "--gw-port", String(ports.vehicle), ...extraArgs], {
    cwd: resolve(REPO, "sim"),
    env: { ...process.env, PYTHONPATH: `${resolve(REPO, "python")}:.` },
  });
  sim.stdout.resume(); // 사건 기록은 버린다 (파이프가 차서 SIM이 막히면 HEARTBEAT를 못 읽어 Failsafe로 오판)
  sim.stderr.resume();
  return sim;
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

/** 경고 하나(data-kind)가 그 상태(data-state)가 될 때까지 기다린다. within: 경고가 있어야 할 자리(testid) */
async function expectAlert(page, kind, state, timeout = 5000, within = "alerts-urgent") {
  const sel = `[data-testid="${within}"] [data-testid="alert"][data-kind="${kind}"][data-state="${state}"]`;
  await page.locator(sel).first().waitFor({ timeout }).catch(async () => {
    const all = await page.getByTestId("alert").evaluateAll((els) => els.map((e) => `${e.dataset.kind}:${e.dataset.state}`));
    throw new Error(`경고 ${kind} ${state} 기대, 실제 [${all.join(", ")}]`);
  });
}

/** 경고의 확인 버튼을 누른다 */
async function ackAlert(page, kind) {
  await page.locator(`[data-testid="alert"][data-kind="${kind}"] [data-testid="alert-ack"]`).first().click();
}

function checkPageErrors() {
  if (pageErrors.length) throw new Error(`페이지 오류: ${pageErrors.join(" | ")}`);
}

async function scenario(browser) {
  const ports = { vehicle: await freePort(), detector: await freePort(), sim: await freePort() };
  let gw = await startGateway(ports, 0);
  const vitePort = await freePort();
  const gwWsPort = gw.wsPort;
  await startVite(vitePort, `ws://127.0.0.1:${gw.wsPort}/ws`);

  const page = await browser.newPage({ viewport: { width: 1280, height: 760 } });
  page.on("pageerror", (e) => pageErrors.push(e.message));
  // 페이지 시계를 옮길 수 있게 한다: 시작 시 --clock-skew, 중간에 window.__clockSkew 변경(계단 변화)
  // GW WebSocket 생성 횟수를 센다 (vite HMR 소켓은 빼고): 기준선 재설정이 실제 재접속으로 일어났는지 확인
  // 배너 변화는 MutationObserver로 빠짐없이 기록한다 (재설정 접속 중 배너는 수 ms만 보일 수 있다)
  await page.addInitScript(([skew, gwPath]) => {
    const real = Date.now.bind(Date);
    window.__clockSkew = skew;
    Date.now = () => real() + window.__clockSkew;
    window.__gwSocketsCreated = 0;
    // REQ-HMI-004 2.0s 측정 (STP TC-HMI-004-07): GW가 보낸 VEHICLE LOST의 last_rx_ts(GW 시계)와 LOST 경고가 ACTIVE로
    // DOM에 들어간 시각(옮기지 않은 페이지 시계). GW와 브라우저가 같은 PC라 두 시계는 같다
    window.__lostLastRx = null;
    window.__lostAlertAt = null;
    const WS = window.WebSocket;
    window.WebSocket = class extends WS {
      constructor(url, protocols) {
        super(url, protocols);
        if (!String(url).endsWith(gwPath)) return;
        window.__gwSocketsCreated += 1;
        this.addEventListener("message", (e) => {
          const m = JSON.parse(e.data);
          if (m.type === "link_status" && m.data.source === "VEHICLE" && m.data.state === "LOST") window.__lostLastRx = m.data.last_rx_ts;
        });
      }
    };
    const recordLostAlert = () => {
      if (window.__lostAlertAt !== null) return;
      if (document.querySelector('[data-testid="alert"][data-kind="VEHICLE_LOST"][data-state="ACTIVE"]')) window.__lostAlertAt = real();
    };
    new MutationObserver(recordLostAlert).observe(document, {
      subtree: true, childList: true, attributes: true, attributeFilter: ["data-state"],
    });
    window.__banners = [];
    const record = () => {
      const el = document.querySelector('[data-testid="banner-text"]');
      const text = el?.textContent?.trim();
      if (text && window.__banners.at(-1) !== text) window.__banners.push(text);
    };
    new MutationObserver(record).observe(document, { subtree: true, childList: true, characterData: true });
  }, [clockSkewMs, `:${gwWsPort}/ws`]);
  if (clockSkewMs !== 0) step(`HMI 시계를 GW보다 ${clockSkewMs}ms 옮김`);
  await page.goto(`http://127.0.0.1:${vitePort}/`);

  step("1. GW 접속: 접속 직후 stats로 FRESH(배너 ok), 차량 링크 LOST와 WARNING 경고");
  await expectAttr(page, "connection-banner", "data-level", "ok");
  await expectText(page, "banner-text", "GW 연결됨");
  await expectText(page, "link-state-VEHICLE", "LOST");
  await expectAlert(page, "VEHICLE_LOST", "ACTIVE");
  await expectAttr(page, "alert", "data-level", "WARNING");
  checkPageErrors();

  step("2. SIM 10Hz 송신(STANDBY 기동) → WAYPOINT로 주행: 차량 링크 CONNECTED, 유실률·모드·배터리 표시, 마커와 궤적 이동, 차량 값 진하게");
  let sim = startSim(ports);
  await expectText(page, "link-state-VEHICLE", "CONNECTED");
  await expectText(page, "loss-VEHICLE", "0.0%", 3000); // GW stats가 반영됨
  await expectText(page, "age-VEHICLE", /^0\.\ds 전$/); // 시계 차와 무관하게 0.x초 (음수·수 초가 아님)
  await expectText(page, "mode", "STANDBY"); // SIM은 STANDBY로 기동하고 명령 없이 움직이지 않는다 (REQ-SAFE-003)
  // 주행 시작: 시작 위치(36.1234567, 127.7654321)와 같은 위도의 동쪽 약 3km 경로점 → 진행 방향 정확히 90°
  await expectAttr(page, "cmd-panel", "data-blocked", "false");
  await page.getByTestId("wp-lat").fill("36.1234567");
  await page.getByTestId("wp-lon").fill("127.8");
  await page.getByTestId("cmd-WAYPOINT").click();
  await page.getByTestId("cmd-send").click();
  await expectText(page, "mode", "MISSION", 3000);
  const driveReqIds = [...gw.sentReqIds()]; // 아래 2c의 "확인 없이 송신 없음"은 이 명령 뒤부터 센다
  if (driveReqIds.length !== 1) throw new Error(`주행 WAYPOINT 송신 기록 ${driveReqIds}`);
  await expectText(page, "battery", /^\d+%$/);
  await expectAttr(page, "vehicle", "data-stale", "false");
  // 확인 전에 풀린 경고는 "해제됨"으로 남고, 확인하면 사라진다 (표 3-2)
  await expectAlert(page, "VEHICLE_LOST", "CLEARED");
  await ackAlert(page, "VEHICLE_LOST");
  await page.locator('[data-testid="alert"][data-kind="VEHICLE_LOST"]').waitFor({ state: "detached", timeout: 2000 });
  if ((await page.locator(".vehicle-icon").count()) !== 1) throw new Error("차량 마커가 없음");
  // 진행 방향: 화살표가 heading_deg만큼 회전 (SIM은 정동쪽 경로점으로 90°)
  const heading = await page.getByTestId("map").getAttribute("data-heading");
  const rotate = await page.locator(".vehicle-icon .arrow").getAttribute("style");
  if (heading !== "90" || !rotate?.includes("rotate(90deg)")) throw new Error(`진행 방향 표시 이상: heading=${heading}, style=${rotate}`);
  // 궤적: Leaflet 궤적 레이어에 실제로 들어 있는 점 수가 늘어나는지 본다 (렌더러와 무관, 개발 서버 전용 훅)
  const drawnPoints = async () => page.evaluate(() => window.__ugvcTrackPoints());
  const n1 = await drawnPoints();
  await page.waitForTimeout(3000);
  const n2 = await drawnPoints();
  if (!(n2 >= n1 + 20)) throw new Error(`궤적이 늘지 않음: ${n1} -> ${n2} (3초, 10Hz)`);
  checkPageErrors();

  step("2b. 연결 중 HMI 시계 +3초 계단 변화: 데이터 지연 → 기준선 재설정(재접속 1회, 연결 끊김 표시 없음) → 다시 ok");
  const socketsBefore = await page.evaluate(() => window.__gwSocketsCreated);
  await page.evaluate(() => {
    window.__banners = [];
    window.__clockSkew += 3000;
  });
  const deadline = Date.now() + 15000;
  let sockets = socketsBefore;
  for (;;) {
    sockets = await page.evaluate(() => window.__gwSocketsCreated);
    const level = await page.getByTestId("connection-banner").getAttribute("data-level");
    if (sockets > socketsBefore && level === "ok") break;
    if (Date.now() > deadline) {
      const seen = await page.evaluate(() => window.__banners);
      throw new Error(`2b: 재접속 ${sockets - socketsBefore}회, 배너 ${level}, 기록 ${seen.join(" | ")}`);
    }
    await page.waitForTimeout(100);
  }
  const seen = await page.evaluate(() => window.__banners); // 배너가 바뀐 순서 전체
  if (sockets - socketsBefore !== 1) throw new Error(`2b: 재접속이 1회가 아님 (${sockets - socketsBefore})`);
  if (!seen.includes("GW 데이터 지연 · 기준선 재설정 중")) throw new Error(`2b: 기준선 재설정 표시 없음: ${seen.join(" | ")}`);
  if (seen.some((t) => t.startsWith("GW 연결 끊김"))) throw new Error(`2b: 기준선 재설정을 연결 끊김으로 표시함: ${seen.join(" | ")}`);
  // 데이터 지연은 CAUTION 경고로 남고(회복했으니 해제됨), 연결 끊김 경고는 없다
  await expectAlert(page, "GW_DELAY", "CLEARED");
  if (await page.locator('[data-testid="alert"][data-kind="GW_DISCONNECTED"]').count()) throw new Error("2b: 기준선 재설정에 GW 연결 끊김 경고");
  await ackAlert(page, "GW_DELAY");
  checkPageErrors();

  step("2c. 명령: 버튼은 확인만 연다(취소하면 SIM에 아무것도 안 감), 보내기를 눌러야 송신, 결과 ACCEPTED 표시");
  await expectAttr(page, "cmd-panel", "data-blocked", "false");
  const firstRow = () => page.getByTestId("cmd-row").first();
  /** 맨 위(가장 최근) 명령의 상태가 status가 될 때까지 기다린다 */
  const expectRow = async (status, timeout = 3000) => {
    const deadline = Date.now() + timeout;
    let actual = null;
    while (Date.now() < deadline) {
      actual = await firstRow().getAttribute("data-status", { timeout: 100 }).catch(() => null);
      if (actual === status) return;
      await page.waitForTimeout(50);
    }
    throw new Error(`맨 위 명령 기대 ${status}, 실제 ${actual ?? "(없음)"}`);
  };
  await page.getByTestId("cmd-STOP").click();
  await expectText(page, "cmd-confirm-text", "정지 (STOP)");
  await page.getByTestId("cmd-cancel").click();
  await page.waitForTimeout(1500);
  if (gw.sentReqIds().length !== driveReqIds.length) throw new Error(`확인 없이 COMMAND가 나감: ${gw.sentReqIds()}`);
  if ((await page.getByTestId("cmd-row").count()) !== driveReqIds.length) throw new Error("취소한 명령이 기록됨");
  await page.getByTestId("cmd-STOP").click();
  await page.getByTestId("cmd-send").click();
  await expectRow("ACCEPTED");
  await expectText(page, "cmd-row-text", "수락 (송신 1회)");
  await page.getByTestId("wp-lat").fill("36.5");
  await page.getByTestId("wp-lon").fill("127.25");
  await page.getByTestId("cmd-WAYPOINT").click();
  await expectText(page, "cmd-confirm-text", "경로점 이동 (WAYPOINT) 36.5000000, 127.2500000");
  await page.getByTestId("cmd-send").click();
  await expectRow("ACCEPTED");
  const acceptedIds = await page.getByTestId("cmd-row").evaluateAll((els) => els.map((e) => e.dataset.reqId));
  if (JSON.stringify(gw.sentReqIds()) !== JSON.stringify([...acceptedIds].reverse())) {
    throw new Error(`GW 송신 기록 ${gw.sentReqIds()}과 화면 ${acceptedIds}이 다름`);
  }
  if (screenshot) await page.screenshot({ path: screenshot });
  checkPageErrors();

  step("2d. 결과 전에 GW 끊김: '결과 미확인', 새 GW에 자동 재송신 없음, 다시 시도는 경고가 붙은 새 확인 절차로 새 req_id, ACK가 없으면 TIMEOUT");
  // ACK가 없는 SIM: 받은 COMMAND를 버린다(--drop-first). 차량 LOST를 거쳐 다시 띄워야 GW가 seq 기준값을 새로 잡는다
  await stop(sim);
  await expectText(page, "link-state-VEHICLE", "LOST", 3000);
  sim = startSim(ports, ["--drop-first", "100"]);
  await expectText(page, "link-state-VEHICLE", "CONNECTED", 5000);
  await expectAttr(page, "cmd-panel", "data-blocked", "false");
  await page.getByTestId("cmd-RTB").click();
  await page.getByTestId("cmd-send").click();
  await expectRow("PENDING", 1000);
  await expectText(page, "cmd-row-text", /^결과 대기 중 · \d+초$/);
  const lostReqId = await firstRow().getAttribute("data-req-id");
  await stop(gw.proc);
  await expectRow("UNCONFIRMED");
  await expectAlert(page, "GW_DISCONNECTED", "ACTIVE");
  await expectAlert(page, "CMD_UNCONFIRMED", "ACTIVE");
  gw = await startGateway(ports, gw.wsPort);
  await expectAttr(page, "connection-banner", "data-level", "ok", 8000);
  await expectText(page, "link-state-VEHICLE", "CONNECTED");
  await page.waitForTimeout(2000);
  if (gw.sentReqIds().length !== 0) throw new Error(`새 GW에 자동 재송신함: ${gw.sentReqIds()}`);
  await expectRow("UNCONFIRMED");
  await page.getByTestId("cmd-RTB").click(); // 다시 시도: 새 명령
  await expectText(page, "cmd-confirm-text", "복귀 (RTB)");
  await expectText(page, "cmd-confirm-warning", "앞서 보낸 복귀 (RTB)의 결과를 아직 받지 못함 · 이미 실행됐을 수 있어 보내면 두 번 실행될 수 있음");
  if (gw.sentReqIds().length !== 0) throw new Error("확인 없이 송신함");
  await page.getByTestId("cmd-send").click();
  await expectRow("TIMEOUT", 6000);
  await expectText(page, "cmd-row-text", "응답 없음 (송신 4회)");
  await expectAlert(page, "CMD_FAILED", "ACTIVE");
  await expectAlert(page, "CMD_UNCONFIRMED", "ACTIVE"); // 명령 결과 경고는 풀리지 않는다
  for (const kind of ["CMD_FAILED", "CMD_UNCONFIRMED"]) {
    await ackAlert(page, kind);
    await page.locator(`[data-testid="alert"][data-kind="${kind}"]`).waitFor({ state: "detached", timeout: 2000 });
  }
  const resent = gw.sentReqIds();
  const newReqId = await firstRow().getAttribute("data-req-id");
  if (resent.length !== 1 || resent[0] !== newReqId || newReqId === lostReqId) {
    throw new Error(`다시 시도 req_id: 화면 ${newReqId}, 이전 ${lostReqId}, GW 송신 ${resent}`);
  }
  checkPageErrors();

  step("3. SIM 중지: 차량 링크 LOST 경고가 마지막 유효 패킷 수신 후 2.0초 안에, 차량 값 '이전 값'으로 흐림, 배너는 그대로 ok, 명령 버튼 비활성과 사유");
  await expectText(page, "link-state-VEHICLE", "CONNECTED");
  await page.evaluate(() => {
    window.__lostLastRx = null;
    window.__lostAlertAt = null;
  });
  await stop(sim);
  await expectText(page, "link-state-VEHICLE", "LOST", 3000);
  await expectAlert(page, "VEHICLE_LOST", "ACTIVE");
  const { lastRx, alertAt } = await page.evaluate(() => ({ lastRx: window.__lostLastRx, alertAt: window.__lostAlertAt }));
  if (lastRx === null || alertAt === null) throw new Error(`3: 측정값 없음 (last_rx_ts ${lastRx}, 경고 ${alertAt})`);
  const lostLatency = alertAt - lastRx;
  step(`   마지막 유효 패킷 수신 → LOST 경고 표시 ${lostLatency.toFixed(0)}ms (REQ-HMI-004 기준 2000ms)`);
  if (!(lostLatency > 0 && lostLatency <= 2000)) throw new Error(`3: LOST 경고 지연 ${lostLatency}ms (기준 0 초과 2000 이하)`);
  await expectText(page, "cmd-blocked", "차량 링크 LOST · 명령을 보낼 수 없음");
  for (const b of ["cmd-STOP", "cmd-RTB", "cmd-WAYPOINT"]) {
    if (!(await page.getByTestId(b).isDisabled())) throw new Error(`${b}가 LOST에서 활성`);
  }
  await expectAttr(page, "vehicle", "data-stale", "true");
  await expectAttr(page, "connection-banner", "data-level", "ok");
  checkPageErrors();

  step("4. GW 중지: 'GW 연결 끊김 · N초 후 재접속', 패널 전체 흐림");
  await stop(gw.proc);
  await expectAttr(page, "connection-banner", "data-level", "bad", 3000);
  await expectText(page, "banner-text", /^GW 연결 끊김 · (\d+초 후 재접속|재접속 중)$/);
  await expectAttr(page, "status-panel", "data-stale", "true");
  await expectText(page, "cmd-blocked", "GW와 연결되지 않아 명령을 보낼 수 없음");
  await expectAlert(page, "GW_DISCONNECTED", "ACTIVE");
  await expectAlert(page, "VEHICLE_LOST", "ACTIVE"); // 차량 조건은 마지막으로 받은 값으로
  if (screenshot) await page.screenshot({ path: screenshot.replace(/(\.png)?$/, "-alerts.png") }); // 경고가 보이는 화면
  checkPageErrors();

  step("5. GW 재시작(같은 포트): 자동 재접속, 새 stats로 다시 FRESH");
  gw = await startGateway(ports, gw.wsPort);
  await expectAttr(page, "connection-banner", "data-level", "ok", 8000);
  await expectAttr(page, "status-panel", "data-stale", "false");
  await expectAlert(page, "GW_DISCONNECTED", "CLEARED");
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
