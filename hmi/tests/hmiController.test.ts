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

function harness(tickDelayMs = QUARTER_MS) {
  // 매 250ms: 메시지가 도착하고, tickDelayMs 뒤 App의 tick (0 < tickDelayMs ≤ 250: 도착과 tick 사이 위상)
  let wall = 1_000_000; // HMI 벽시계
  let mono = 0; // 단조 시계
  let gwNow = 5_000_000; // GW 시계 (HMI와 다름)
  let seq = 0; // GW가 매긴 텔레메트리 seq (HMI에 닿지 않아도 늘어난다)
  let quarter = 0;
  let paused = false; // GW가 보낸 것이 HMI에 닿지 않음 (재설정 접속 대기, 버려진 적체)
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
  const advance = (ms: number) => {
    wall += ms;
    mono += ms;
    gwNow += ms;
  };
  /** 250ms 한 칸: 10Hz telemetry(2~3개)와 1초마다 stats가 도착하고(전달 50ms), tickDelayMs 뒤 tick.
   *  이 칸에 stats가 도착했으면 그 단조 시각을 돌려준다 */
  const step = (): number | null => {
    let statsAt: number | null = null;
    advance(QUARTER_MS - tickDelayMs);
    const n = 2 + (quarter % 2);
    for (let k = 0; k < n; k++) {
      const msg = telemetryText(gwNow - 50, seq++);
      if (!paused) live().onmessage?.({ data: msg });
    }
    if (quarter % 4 === 0 && !paused) {
      live().onmessage?.({ data: statsText(gwNow - 50) });
      statsAt = mono;
    }
    quarter++;
    advance(tickDelayMs);
    c.tick();
    return statsAt;
  };
  return {
    c, sockets, timers, banners, live, step,
    view: () => last ?? c.view(),
    stepWall: (ms: number) => (wall += ms),
    advanceMono: (ms: number) => (mono += ms),
    mono: () => mono,
    quarter: () => quarter,
    pause: (p: boolean) => (paused = p),
    /** GW가 VEHICLE 링크 상태를 보냄 (마지막 수신은 GW 시각으로 agoMs 전) */
    sendVehicleLink: (agoMs: number) =>
      live().onmessage?.({
        data: JSON.stringify({ type: "link_status", ts: gwNow - 50, data: { source: "VEHICLE", state: "CONNECTED", last_rx_ts: gwNow - 50 - agoMs } }),
      }),
  };
}

/** FRESH 연결을 만든 뒤 offset칸(0~3) 지나 HMI 벽시계를 +3초 옮기고, 첫 늦은 stats와 재설정 시각을 잰다 */
function clockStepRun(offsetQuarters: number, tickDelayMs = QUARTER_MS) {
  const h = harness(tickDelayMs);
  h.c.start();
  h.live().onopen?.({});
  while (h.quarter() < 12) h.step(); // 3초 (stats는 칸 0·4·8에 도착)
  for (let i = 0; i < offsetQuarters; i++) h.step();
  expect(h.view().fresh).toBe("FRESH");
  h.stepWall(3000);
  const stepAt = h.mono();
  h.banners.length = 0;
  let firstLateAt: number | null = null; // 계단 뒤 첫 stats = 첫 늦은 stats
  while (h.sockets.length === 1 && h.mono() - stepAt < 20_000) {
    const statsAt = h.step();
    if (firstLateAt === null) firstLateAt = statsAt;
  }
  return { h, stepAt, firstLateAt: firstLateAt!, resyncAt: h.mono() };
}

it("REQ_HMI_006 a clock step is recovered by resync 5 to 5.25s after the first late stats and at most 6.25s after the step", () => {
  // 위상 두 가지: 계단에서 다음 stats까지(offset), stats 도착에서 tick까지(tickDelay)
  for (const tickDelay of [1, 125, 249, 250]) {
    for (const offset of [0, 1, 2, 3]) {
      const { h, stepAt, firstLateAt, resyncAt } = clockStepRun(offset, tickDelay);
      const at = `offset ${offset}, tick ${tickDelay}ms after stats`;
      expect(h.sockets.length, at).toBe(2);
      // ICD: 첫 늦은 stats부터 5초 이상, 판정 주기(250ms) 하나 안에
      expect(resyncAt - firstLateAt, at).toBeGreaterThanOrEqual(ICD_RESYNC_AFTER_MS);
      expect(resyncAt - firstLateAt, at).toBeLessThan(ICD_RESYNC_AFTER_MS + QUARTER_MS);
      expect(resyncAt - stepAt, at).toBeGreaterThanOrEqual(ICD_RESYNC_AFTER_MS);
      expect(resyncAt - stepAt, at).toBeLessThanOrEqual(ICD_RESYNC_MAX_MS);
      expect(h.view().conn).toEqual({ state: "CONNECTING", cause: "resync" });
      expect(h.banners).toContain("GW 데이터 지연 · 기준선 재설정 중");
      expect(h.banners.some((b) => b.startsWith("GW 연결 끊김"))).toBe(false);
    }
  }
});

it("REQ_HMI_006 after a quick resync the new baseline is FRESH and the track stays one segment", () => {
  const { h } = clockStepRun(0);
  h.live().onopen?.({}); // 바로 열림, 빠진 표본 없음
  for (let i = 0; i < 4; i++) h.step();
  const v = h.view();
  expect(v.fresh).toBe("FRESH");
  expect(v.state.track.length).toBe(1); // 재설정은 연결 끊김이 아니므로 궤적을 잇는다
});

it("REQ_HMI_001 a resync connection that opens after 3s breaks the track", () => {
  const { h } = clockStepRun(0);
  h.pause(true);
  for (let i = 0; i < 12; i++) h.step(); // 3초 동안 접속이 열리지 않음: GW seq는 약 30 진행
  h.pause(false);
  h.live().onopen?.({});
  for (let i = 0; i < 4; i++) h.step();
  expect(h.view().state.track.length).toBe(2);
});

it("REQ_HMI_001 a resync connection that opens at once but skips about 30 seq breaks the track", () => {
  // 처리 적체를 버린 재설정과 같은 입력: 소켓은 바로 열리지만 그 뒤 도착하는 첫 텔레메트리 seq가 약 30 건너뜀
  const { h } = clockStepRun(0);
  h.live().onopen?.({});
  h.pause(true);
  for (let i = 0; i < 12; i++) h.step();
  h.pause(false);
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

it("REQ_HMI_007 discarded IF-03 messages are counted for display in the status panel", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  h.step();
  expect(h.c.view().discarded).toBe(0);
  h.live().onmessage?.({ data: "not IF-03" });
  h.live().onmessage?.({ data: JSON.stringify({ type: "telemetry", ts: 1, data: { lat: 91 } }) });
  expect(h.c.view().discarded).toBe(2); // 상태 패널(data-testid="discarded")이 그리는 값
  expect(h.c.view().state.vehicle?.lat).not.toBe(91);
});
