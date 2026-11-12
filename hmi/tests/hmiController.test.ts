// HmiController: 연결 + 상태 + 주기 판정 + 화면 판정의 전체 순환 (가짜 소켓·타이머·시계). describe 없음.
import { expect, it } from "vitest";
import { HmiController } from "../src/app/hmiController";
import type { SocketLike } from "../src/net/gwConnection";
import { RESYNC_AFTER_MS } from "../src/state/hmiState";

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
  const sockets: FakeSocket[] = [];
  const timers: Array<{ fn: () => void; ms: number; done: boolean }> = [];
  const banners: string[] = [];
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
      setTimer: (fn, ms) => {
        const t = { fn, ms, done: false };
        timers.push(t);
        return t;
      },
      clearTimer: (h) => ((h as { done: boolean }).done = true),
    },
    () => banners.push(c.view().banner.text), // App.vue와 같은 경로 (view())
  );
  const live = () => sockets.filter((s) => !s.closed).at(-1)!;
  /** 1초 동안: 10Hz telemetry, 1Hz stats (전달 50ms), HMI는 250ms마다 tick */
  const second = () => {
    for (let i = 0; i < 4; i++) {
      wall += 250;
      mono += 250;
      gwNow += 250;
      if (i === 0) live().onmessage?.({ data: statsText(gwNow - 50) });
      for (let k = 0; k < 2 && live(); k++) live().onmessage?.({ data: telemetryText(gwNow - 50, seq++) });
      c.tick();
    }
  };
  return {
    c, sockets, timers, banners, live, second,
    stepWall: (ms: number) => (wall += ms),
    advanceMono: (ms: number) => (mono += ms),
  };
}

it("REQ_HMI_006 a clock step during a connection is recovered by resync within 6.25s, without a disconnect or a track break", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  for (let i = 0; i < 3; i++) h.second();
  expect(h.c.view().fresh).toBe("FRESH");
  expect(h.c.view().state.track.length).toBe(1);

  h.stepWall(3000); // HMI 시계 +3초 계단 변화 (stats 직후): 이후 stats가 기준선보다 3초 늦게 보인다
  h.banners.length = 0;
  let waited = 0;
  while (h.sockets.length === 1 && waited < 20) {
    h.second();
    waited++;
  }
  expect(h.sockets.length).toBe(2); // 기준선 재설정으로 다시 접속함
  // 첫 늦은 stats(최대 1s 뒤)부터 5s, 판정 주기 0.25s → 계단 뒤 5s 이상 6.25s 이하 (ICD 7장)
  expect(waited).toBeGreaterThanOrEqual(RESYNC_AFTER_MS / 1000);
  expect(waited).toBeLessThanOrEqual(7); // 1초 단위로 세므로 6.25s는 7번째 초 안
  expect(h.c.snapshot.conn).toEqual({ state: "CONNECTING", cause: "resync" });
  expect(h.banners).toContain("GW 데이터 지연 · 기준선 재설정 중");
  expect(h.banners.some((b) => b.startsWith("GW 연결 끊김"))).toBe(false);

  h.live().onopen?.({});
  h.second();
  const v = h.c.view();
  expect(v.fresh).toBe("FRESH"); // 새 기준선으로 다시 FRESH
  expect(v.state.session?.lateSince).toBeNull();
  expect(v.state.track.length).toBe(1); // 재설정은 연결 끊김이 아니므로 궤적을 끊지 않는다
});

it("REQ_HMI_006 on-time data never triggers a resync", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  for (let i = 0; i < 60; i++) h.second();
  expect(h.sockets.length).toBe(1);
  expect(h.c.view().fresh).toBe("FRESH");
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
  expect(h.c.view().state.track).toEqual([]);
});

it("REQ_HMI_001 a real IF-03 disconnect starts a new track segment", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  h.second();
  h.live().onclose?.({ code: 1006 });
  h.timers.filter((t) => !t.done).at(-1)!.fn(); // 재접속
  h.live().onopen?.({});
  h.second();
  expect(h.c.view().state.track.length).toBe(2);
});
