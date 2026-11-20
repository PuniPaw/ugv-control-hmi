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
  sent: string[] = [];
  send(text: string) {
    this.sent.push(text);
  }
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
  let reqIds = 0;
  let paused = false; // GW가 보낸 것이 HMI에 닿지 않음 (재설정 접속 대기, 버려진 적체)
  const sockets: FakeSocket[] = [];
  const timers: Array<{ fn: () => void; done: boolean }> = [];
  const banners: string[] = [];
  let last: View | null = null;
  const c = new HmiController(
    "ws://gw/ws",
    {
      now: () => wall,
      newReqId: () => `r${++reqIds}`,
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

// ---- 명령 패널 (REQ-CMD-001·004, 이슈 #27) ----

const vehicleLink = (state: "CONNECTED" | "LOST") =>
  JSON.stringify({ type: "link_status", ts: 5_000_000, data: { source: "VEHICLE", state, last_rx_ts: 5_000_000 } });
const resultText = (req_id: string, result: string, attempts: number) =>
  JSON.stringify({ type: "cmd_result", ts: 5_000_000, data: { req_id, cmd_id: 42, result, attempts } });
/** 모든 소켓으로 HMI가 보낸 command (보낸 순서) */
const sentCommands = (h: ReturnType<typeof harness>) =>
  h.sockets.flatMap((s) => s.sent.map((t) => JSON.parse(t) as { type: string; data: { req_id: string; cmd_type: string } }));

/** 열린 연결, 차량 링크 CONNECTED */
function readyHarness() {
  const h = harness();
  h.c.start();
  h.live().onopen?.({});
  h.live().onmessage?.({ data: vehicleLink("CONNECTED") });
  return h;
}

it("REQ_CMD_001 a command button only opens confirmation, cancel sends nothing, confirm sends once", () => {
  const h = readyHarness();
  for (const spec of [{ cmd_type: "STOP" }, { cmd_type: "RTB" }, { cmd_type: "WAYPOINT", lat: 36.5, lon: 127.25 }] as const) {
    h.c.requestCommand(spec);
    expect(h.view().command.confirm).not.toBeNull();
    h.c.cancelCommand();
    expect(h.view().command.confirm).toBeNull();
  }
  expect(sentCommands(h)).toEqual([]); // 버튼과 취소만으로는 아무것도 보내지 않는다
  h.c.confirmCommand(); // 확인할 명령이 없으면 보내지 않는다
  expect(sentCommands(h)).toEqual([]);

  h.c.requestCommand({ cmd_type: "WAYPOINT", lat: 36.5, lon: 127.25 });
  expect(h.view().command.confirm).toBe("경로점 이동 (WAYPOINT) 36.5000000, 127.2500000");
  h.c.confirmCommand();
  h.c.confirmCommand(); // 두 번 눌러도 한 번만 (확인 단계가 끝남)
  expect(sentCommands(h)).toEqual([{ type: "command", data: { req_id: "r1", cmd_type: "WAYPOINT", lat: 36.5, lon: 127.25 } }]);
  expect(h.view().command.confirm).toBeNull();
  expect(h.view().command.rows.map((r) => [r.reqId, r.status])).toEqual([["r1", "PENDING"]]);
});

it("REQ_CMD_001 each confirmed command has its own req_id and shows its cmd_result", () => {
  const h = readyHarness();
  const send = (cmd_type: "STOP" | "RTB") => {
    h.c.requestCommand({ cmd_type });
    h.c.confirmCommand();
  };
  send("STOP");
  send("RTB");
  send("RTB");
  expect(sentCommands(h).map((m) => m.data.req_id)).toEqual(["r1", "r2", "r3"]);
  h.live().onmessage?.({ data: resultText("r1", "ACCEPTED", 2) });
  h.live().onmessage?.({ data: resultText("r2", "TIMEOUT", 4) });
  h.live().onmessage?.({ data: resultText("someone-else", "ACCEPTED", 1) }); // 다른 HMI의 명령
  h.live().onmessage?.({ data: JSON.stringify({ type: "cmd_result", ts: 1, data: { req_id: "r3", cmd_id: null, result: "REJECTED_INVALID", attempts: 0 } }) });
  const rows = h.view().command.rows;
  expect(rows.map((r) => [r.reqId, r.status, r.text, r.level, r.retryable])).toEqual([
    ["r3", "REJECTED_INVALID", "거절 · 필드 값 오류", "bad", false],
    ["r2", "TIMEOUT", "응답 없음 (송신 4회)", "bad", true],
    ["r1", "ACCEPTED", "수락 (송신 2회)", "ok", false],
  ]);
  expect(h.view().discarded).toBe(0);
});

it("REQ_CMD_001 a command whose result was not received is never resent automatically after the connection ends", () => {
  for (const end of ["disconnect", "resync"] as const) {
    const h = readyHarness();
    h.c.requestCommand({ cmd_type: "RTB" });
    h.c.confirmCommand();
    if (end === "disconnect") {
      h.live().onclose?.({ code: 1006 }); // GW 재시작 등
      h.timers.filter((t) => !t.done).at(-1)!.fn(); // 1s 뒤 재접속
    } else {
      while (h.quarter() < 12) h.step(); // FRESH 연결
      h.stepWall(3000); // HMI 시계 계단 변화 → 늦음 5초 → 기준선 재설정 재접속
      while (h.sockets.length === 1 && h.quarter() < 80) h.step();
      expect(h.banners).toContain("GW 데이터 지연 · 기준선 재설정 중");
    }
    expect(h.sockets).toHaveLength(2);
    h.live().onopen?.({});
    h.live().onmessage?.({ data: vehicleLink("CONNECTED") });
    for (let i = 0; i < 40; i++) h.step(); // 새 연결에서 10초
    expect(sentCommands(h), end).toHaveLength(1); // 처음 한 번뿐
    expect(h.sockets[1]!.sent, end).toEqual([]);
    const row = h.view().command.rows[0]!;
    expect([row.status, row.retryable], end).toEqual(["UNCONFIRMED", true]);
    expect(row.text).toBe("결과 미확인 · 결과 전에 GW 연결이 끊김 (자동으로 다시 보내지 않음)");
  }
});

it("REQ_CMD_001 retrying after a lost result or TIMEOUT asks for confirmation again and uses a new req_id", () => {
  const h = readyHarness();
  h.c.requestCommand({ cmd_type: "WAYPOINT", lat: 1, lon: 2 });
  h.c.confirmCommand();
  h.live().onclose?.({ code: 1006 });
  h.timers.filter((t) => !t.done).at(-1)!.fn();
  h.live().onopen?.({});
  h.live().onmessage?.({ data: vehicleLink("CONNECTED") });
  const lost = h.view().command.rows[0]!;
  h.c.requestCommand(lost.spec); // "다시 보내기": 같은 내용으로 확인 절차부터
  expect(h.sockets[1]!.sent).toEqual([]);
  expect(h.view().command.confirm).toBe("경로점 이동 (WAYPOINT) 1.0000000, 2.0000000");
  h.c.confirmCommand();
  h.live().onmessage?.({ data: resultText("r2", "TIMEOUT", 4) });
  h.c.requestCommand(h.view().command.rows[0]!.spec);
  h.c.confirmCommand();
  expect(sentCommands(h).map((m) => [m.data.req_id, m.data.cmd_type])).toEqual([["r1", "WAYPOINT"], ["r2", "WAYPOINT"], ["r3", "WAYPOINT"]]);
  expect(h.view().command.rows.map((r) => r.status)).toEqual(["PENDING", "TIMEOUT", "UNCONFIRMED"]);
});

it("REQ_CMD_004 vehicle link LOST or no GW connection disables commands with the reason and confirm sends nothing", () => {
  const h = harness();
  h.c.start();
  expect(h.view().command.blocked).toBe("GW와 연결되지 않아 명령을 보낼 수 없음");
  h.live().onopen?.({});
  expect(h.view().command.blocked).toBe("차량 링크 LOST · 명령을 보낼 수 없음"); // 첫 link_status 전은 LOST (GW 초기값)
  h.live().onmessage?.({ data: vehicleLink("CONNECTED") });
  expect(h.view().command.blocked).toBeNull();

  h.c.requestCommand({ cmd_type: "STOP" }); // 확인 중에 링크가 LOST가 됨
  h.live().onmessage?.({ data: vehicleLink("LOST") });
  expect(h.view().command).toMatchObject({ blocked: "차량 링크 LOST · 명령을 보낼 수 없음", confirm: "정지 (STOP)" });
  h.c.confirmCommand();
  expect(sentCommands(h)).toEqual([]);
  h.live().onmessage?.({ data: vehicleLink("CONNECTED") }); // DEGRADED·CONNECTED면 보낼 수 있다
  h.c.confirmCommand();
  expect(sentCommands(h).map((m) => m.data.cmd_type)).toEqual(["STOP"]);

  h.c.requestCommand({ cmd_type: "RTB" });
  h.live().onclose?.({ code: 1006 });
  expect(h.view().command.blocked).toBe("GW와 연결되지 않아 명령을 보낼 수 없음");
  h.c.confirmCommand();
  expect(sentCommands(h)).toHaveLength(1);
});
