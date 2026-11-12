// HmiController: 연결 + 상태 + 주기 판정의 전체 순환 (가짜 소켓·타이머·시계). describe 없음.
import { expect, it } from "vitest";
import { HmiController, type Snapshot } from "../src/app/hmiController";
import type { SocketLike } from "../src/net/gwConnection";
import { RESYNC_AFTER_MS, freshness } from "../src/state/hmiState";
import { banner } from "../src/state/view";

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

function harness() {
  let now = 1_000_000; // HMI 시계
  let gwNow = 5_000_000; // GW 시계 (HMI와 다름)
  const sockets: FakeSocket[] = [];
  const banners: string[] = [];
  const c = new HmiController(
    "ws://gw/ws",
    {
      now: () => now,
      openSocket: () => {
        const s = new FakeSocket();
        sockets.push(s);
        return s;
      },
      setTimer: () => ({}),
      clearTimer: () => {},
    },
    (snap: Snapshot) => banners.push(banner(snap.conn, freshness(snap.state, now), now).text),
  );
  const live = () => sockets.filter((s) => !s.closed).at(-1)!;
  /** 1초 동안: GW가 stats를 보내고 (전달 50ms), HMI는 250ms마다 tick */
  const second = () => {
    for (let i = 0; i < 4; i++) {
      now += 250;
      gwNow += 250;
      if (i === 0) live().onmessage?.({ data: statsText(gwNow - 50) });
      c.tick();
    }
  };
  return {
    c, sockets, banners, live, second,
    stepHmiClock: (ms: number) => (now += ms),
    fresh: () => freshness(c.snapshot.state, now),
  };
}

it("REQ_HMI_006 a clock step during a connection is recovered by resync without showing a disconnect", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  for (let i = 0; i < 3; i++) h.second();
  expect(h.fresh()).toBe("FRESH");

  h.stepHmiClock(3000); // HMI 시계 +3초 계단 변화: 이후 stats가 기준선보다 3초 늦게 보인다
  h.banners.length = 0;
  let waited = 0;
  while (h.sockets.length === 1 && waited < 20) {
    h.second();
    waited++;
  }
  expect(h.sockets.length).toBe(2); // 기준선 재설정으로 다시 접속함
  expect(waited).toBeGreaterThanOrEqual(RESYNC_AFTER_MS / 1000); // 늦음이 5초 이어진 뒤에
  expect(h.c.snapshot.conn).toEqual({ state: "CONNECTING", failures: 0, resync: true });
  expect(h.banners.some((b) => b.startsWith("GW 연결 끊김"))).toBe(false);

  h.live().onopen?.({});
  h.second();
  expect(h.fresh()).toBe("FRESH"); // 새 기준선으로 다시 FRESH
  expect(h.c.snapshot.state.session?.lateSince).toBeNull();
});

it("REQ_HMI_006 on-time data never triggers a resync", () => {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  for (let i = 0; i < 60; i++) h.second();
  expect(h.sockets.length).toBe(1);
  expect(h.fresh()).toBe("FRESH");
});
