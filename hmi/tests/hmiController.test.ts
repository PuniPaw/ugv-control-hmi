// HmiController: 연결 + 상태 + 주기 판정 + 화면 판정의 전체 순환 (가짜 소켓·타이머·시계). describe 없음.
import { expect, it } from "vitest";
import { HmiController, type View } from "../src/app/hmiController";
import type { SocketLike } from "../src/net/gwConnection";

// ICD 7장의 값 (코드 상수를 가져오지 않는다: 상수가 틀리면 시험이 잡아야 한다)
const ICD_RESYNC_AFTER_MS = 5000;
const ICD_RESYNC_MAX_MS = 6250;

class FakeSocket implements SocketLike {
  onopen: SocketLike["onopen"] = null;
  onclose: SocketLike["onclose"] = null;
  onmessage: SocketLike["onmessage"] = null;
  onerror: SocketLike["onerror"] = null;
  closed = false;
  close() {
    this.closed = true;
  }
}

const QUARTER_MS = 250; // App.vue의 tick 주기
const ERRORS = { sync: 0, version: 0, length: 0, crc: 0, type: 0, range: 0, order: 0 };
const statsText = (ts: number) =>
  JSON.stringify({ type: "stats", ts, data: { source: "VEHICLE", loss_rate_10s: 0, rx_count: 1, lost_count: 0, errors: ERRORS } });
const telemetryText = (ts: number, seq: number) =>
  JSON.stringify({
    type: "telemetry", ts,
    data: { lat: 36 + seq * 1e-5, lon: 127, heading_deg: 0, speed_mps: 1, battery_pct: 50, mode: "MISSION", flags: ["GPS_VALID"], seq, rx_ts: ts },
  });

function harness() {
  let wall = 1_000_000; // HMI 벽시계
  let mono = 0; // 단조 시계
  let gwNow = 5_000_000; // GW 시계 (HMI와 다름)
  let seq = 0;
  let quarter = 0;
  let paused = false; // 재설정 접속이 열리기를 기다리는 동안 GW가 보낸 것은 HMI에 닿지 않는다
  const sockets: FakeSocket[] = [];
  const timers: Array<{ fn: () => void; done: boolean }> = [];
  const banners: string[] = [];
  let last: View | null = null;
  const c = new HmiController(
    "ws://gw/ws",
    {
      now: () => wall,
      monotonic: () => mono,
      openSocket: () => {
        const s = new FakeSocket();
        sockets.push(s);
        return s;
      },
      setTimer: (fn) => {
        const t = { fn, done: false };
        timers.push(t);
        return t;
      },
      clearTimer: (h) => ((h as { done: boolean }).done = true),
    },
    (v) => {
      last = v; // App.vue와 같은 경로 (onChange로 받은 View)
      if (banners.at(-1) !== v.banner.text) banners.push(v.banner.text);
    },
  );
  const live = () => sockets.filter((s) => !s.closed).at(-1)!;
  /** 250ms: 10Hz telemetry(2~3개), 1초마다 stats (전달 50ms), 끝에 tick */
  const step = () => {
    wall += QUARTER_MS;
    mono += QUARTER_MS;
    gwNow += QUARTER_MS;
    if (!paused) {
      if (quarter % 4 === 0) live().onmessage?.({ data: statsText(gwNow - 50) });
      for (let k = 0; k < 2 + (quarter % 2); k++) live().onmessage?.({ data: telemetryText(gwNow - 50, seq++) });
    }
    quarter++;
    c.tick();
  };
  return {
    c, sockets, timers, banners, live, step,
    view: () => last ?? c.view(),
    stepWall: (ms: number) => (wall += ms),
    advanceMono: (ms: number) => (mono += ms),
    mono: () => mono,
    quarter: () => quarter,
    pause: (p: boolean) => (paused = p),
    /** GW가 지금 VEHICLE 링크 상태를 보냄 (마지막 수신은 GW 시각으로 agoMs 전) */
    sendVehicleLink: (agoMs: number) =>
      live().onmessage?.({
        data: JSON.stringify({ type: "link_status", ts: gwNow - 50, data: { source: "VEHICLE", state: "CONNECTED", last_rx_ts: gwNow - 50 - agoMs } }),
      }),
  };
}

/** FRESH 연결을 만든 뒤, 다음 stats 직전 offset(0~3 quarter 뒤)에서 HMI 벽시계를 +3초 옮기고 재설정까지 잰다 */
function clockStepRun(offsetQuarters: number) {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  while (h.quarter() < 12) h.step(); // 3초
  for (let i = 0; i < offsetQuarters; i++) h.step();
  expect(h.view().fresh).toBe("FRESH");
  h.stepWall(3000);
  const stepAt = h.mono();
  h.banners.length = 0;
  let firstLateAt: number | null = null;
  while (h.sockets.length === 1 && h.mono() - stepAt < 20_000) {
    h.step();
    if (firstLateAt === null && h.view().state.session?.lateSince != null) firstLateAt = h.mono();
  }
  return { h, stepAt, firstLateAt: firstLateAt!, resyncAt: h.mono() };
}

it("REQ_HMI_006 a clock step is recovered by resync 5 to 5.25s after the first late stats and at most 6.25s after the step", () => {
  // offset 0: 계단이 stats 바로 뒤 → 첫 늦은 stats는 1초 뒤 (최악 위상), offset 3: 계단이 다음 stats 바로 앞
  for (const offset of [0, 1, 2, 3]) {
    const { h, stepAt, firstLateAt, resyncAt } = clockStepRun(offset);
    expect(h.sockets.length, `offset ${offset}`).toBe(2);
    expect(resyncAt - firstLateAt, `offset ${offset}`).toBeGreaterThanOrEqual(ICD_RESYNC_AFTER_MS);
    expect(resyncAt - firstLateAt, `offset ${offset}`).toBeLessThanOrEqual(ICD_RESYNC_AFTER_MS + QUARTER_MS);
    expect(resyncAt - stepAt, `offset ${offset}`).toBeGreaterThanOrEqual(ICD_RESYNC_AFTER_MS);
    expect(resyncAt - stepAt, `offset ${offset}`).toBeLessThanOrEqual(ICD_RESYNC_MAX_MS);
    expect(h.view().conn).toEqual({ state: "CONNECTING", cause: "resync" });
    expect(h.banners).toContain("GW 데이터 지연 · 기준선 재설정 중");
    expect(h.banners.some((b) => b.startsWith("GW 연결 끊김"))).toBe(false);
  }
});

it("REQ_HMI_006 after a quick resync the new baseline is FRESH and the track stays one segment", () => {
  const { h } = clockStepRun(0);
  h.live().onopen?.({}); // 바로 열림
  for (let i = 0; i < 4; i++) h.step();
  const v = h.view();
  expect(v.fresh).toBe("FRESH");
  expect(v.state.session?.lateSince).toBeNull();
  expect(v.state.track.length).toBe(1); // 재설정은 연결 끊김이 아니므로 궤적을 잇는다
});

it("REQ_HMI_001 a resync connection that takes more than 1s to open starts a new track segment", () => {
  const { h } = clockStepRun(0);
  h.pause(true);
  for (let i = 0; i < 12; i++) h.step(); // 3초 동안 접속이 열리지 않음 (표본 약 30개 빠짐)
  h.pause(false);
  h.live().onopen?.({});
  for (let i = 0; i < 4; i++) h.step();
  expect(h.view().state.track.length).toBe(2);
});

it("REQ_HMI_006 on-time data never triggers a resync", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  for (let i = 0; i < 240; i++) h.step(); // 60초
  expect(h.sockets.length).toBe(1);
  expect(h.view().fresh).toBe("FRESH");
});

it("REQ_HMI_006 retry countdown follows the monotonic clock, not wall clock changes", () => {
  const h = harness();
  h.c.start();
  h.live().onclose?.({ code: 1006 }); // 1초 뒤 재접속 예정
  expect(h.c.view().banner.text).toBe("GW 연결 끊김 · 1초 후 재접속");
  h.stepWall(3000); // 벽시계만 +3s
  expect(h.c.view().banner.text).toBe("GW 연결 끊김 · 1초 후 재접속");
  h.stepWall(-60_000); // 벽시계만 −60s
  expect(h.c.view().banner.text).toBe("GW 연결 끊김 · 1초 후 재접속");
  h.advanceMono(600);
  expect(h.c.view().banner.text).toBe("GW 연결 끊김 · 1초 후 재접속"); // 0.4초 남음 → 올림
  h.advanceMono(400);
  expect(h.c.view().banner.text).toBe("GW 연결 끊김 · 0초 후 재접속");
});

it("REQ_HMI_001 a real IF-03 disconnect starts a new track segment", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  for (let i = 0; i < 4; i++) h.step();
  h.live().onclose?.({ code: 1006 });
  h.timers.filter((t) => !t.done).at(-1)!.fn(); // 재접속
  h.live().onopen?.({});
  for (let i = 0; i < 4; i++) h.step();
  expect(h.view().state.track.length).toBe(2);
});

it("REQ_HMI_002 the status panel rows come from the same judgement as the banner", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  for (let i = 0; i < 8; i++) h.step();
  h.sendVehicleLink(300); // GW 시계로 0.3초 전 수신 (GW와 HMI 시계는 서로 다르다)
  const v = h.c.view();
  expect(v.links.map((l) => l.source)).toEqual(["VEHICLE", "DETECTOR"]);
  expect(v.links[0]).toEqual({ source: "VEHICLE", state: "CONNECTED", loss: "0.0%", age: "0.3s 전" });
  expect(v.links[1]).toMatchObject({ state: "LOST", loss: "—", age: "수신 없음" });
  expect(v.vehicle).toMatchObject({ mode: "MISSION", battery: "50%", batteryLevel: "ok", speed: "1.0 m/s" });
  expect(v.vehicle?.position).toMatch(/^36\.\d{6}, 127\.000000$/);
});
