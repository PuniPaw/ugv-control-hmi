// 화면 표시 정책 (state/view.ts). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import type { If03Message, TelemetryData } from "../src/if03/messages";
import { apply, connectionOpened, initialState } from "../src/state/hmiState";
import { banner, batteryLevel, linkAgeText, positionText, present, staleness, type ConnView } from "../src/state/view";

const open: ConnView = { state: "OPEN" };
const vehicle = (flags: TelemetryData["flags"]): TelemetryData => ({
  lat: 36.1, lon: 127.1, heading_deg: 0, speed_mps: 0, battery_pct: 18, mode: "RTB", flags, seq: 0, rx_ts: 0,
});

it("REQ_HMI_006 banner distinguishes first connect, disconnect with countdown, waiting, stale and ok", () => {
  expect(banner({ state: "CONNECTING", cause: "first" }, "WAITING", 0)).toEqual({ level: "warn", text: "GW 접속 중" });
  expect(banner({ state: "CONNECTING", cause: "retry", failures: 2 }, "WAITING", 0)).toEqual({ level: "bad", text: "GW 연결 끊김 · 재접속 중" });
  // 기준선 재설정은 연결 끊김이 아니다
  expect(banner({ state: "CONNECTING", cause: "resync" }, "WAITING", 0)).toEqual({
    level: "warn", text: "GW 데이터 지연 · 기준선 재설정 중",
  });
  expect(banner({ state: "WAITING_RETRY", failures: 1, retryAtMono: 5000 }, "WAITING", 3200).text).toBe("GW 연결 끊김 · 2초 후 재접속");
  expect(banner(open, "WAITING", 0)).toEqual({ level: "warn", text: "GW 연결됨 · 데이터 대기 중" });
  expect(banner(open, "STALE", 0)).toEqual({ level: "bad", text: "GW 데이터 지연 (2초 이상)" });
  expect(banner(open, "FRESH", 0)).toEqual({ level: "ok", text: "GW 연결됨" });
});

it("REQ_HMI_006 dims everything when GW is down or stale, and vehicle values when the vehicle link is not CONNECTED", () => {
  const lost = initialState();
  const connected = apply(lost, { type: "link_status", ts: 0, data: { source: "VEHICLE", state: "CONNECTED", last_rx_ts: 0 } }, 0);
  expect(staleness(connected, open, "FRESH")).toEqual({ all: false, vehicle: false });
  expect(staleness(lost, open, "FRESH")).toEqual({ all: false, vehicle: true });
  expect(staleness(connected, open, "STALE")).toEqual({ all: true, vehicle: true });
  expect(staleness(connected, { state: "WAITING_RETRY", failures: 1, retryAtMono: 0 }, "FRESH")).toEqual({ all: true, vehicle: true });
  expect(staleness(connected, { state: "CONNECTING", cause: "resync" }, "FRESH")).toEqual({ all: true, vehicle: true });
});

it("REQ_HMI_002 link age uses the connection baseline, not the raw GW clock", () => {
  const skew = -5000; // HMI 시계가 GW보다 5초 늦음
  const m: If03Message = { type: "link_status", ts: 10_000, data: { source: "VEHICLE", state: "CONNECTED", last_rx_ts: 9_700 } };
  const s = apply(connectionOpened(initialState(), 10_000 + skew), m, 10_010 + skew);
  expect(linkAgeText(s, "VEHICLE", 10_510 + skew)).toBe("0.8s 전");
  expect(linkAgeText(s, "DETECTOR", 0)).toBe("수신 없음");
});

it("REQ_HMI_002 battery color follows BATTERY_LOW and position shows invalid without GPS_VALID", () => {
  expect(batteryLevel(vehicle(["BATTERY_LOW"]))).toBe("bad");
  expect(batteryLevel(vehicle([]))).toBe("ok");
  expect(positionText(vehicle(["GPS_VALID"]))).toBe("36.100000, 127.100000");
  expect(positionText(vehicle([]))).toContain("위치 무효");
});


it("REQ_HMI_006 the last route is dimmed when the vehicle link is not CONNECTED or GW data is not fresh", () => {
  const ERRORS = { sync: 0, version: 0, length: 0, crc: 0, type: 0, range: 0, order: 0 };
  const stats: If03Message = { type: "stats", ts: 0, data: { source: "VEHICLE", loss_rate_10s: 0, rx_count: 1, lost_count: 0, errors: ERRORS } };
  const routeMsg: If03Message = {
    type: "route", ts: 0, data: { waypoints: [{ lat: 36.1, lon: 127.1 }, { lat: 36.2, lon: 127.2 }], seq: 0, rx_ts: 0 },
  };
  const link = (state: "CONNECTED" | "DEGRADED" | "LOST"): If03Message =>
    ({ type: "link_status", ts: 0, data: { source: "VEHICLE", state, last_rx_ts: 0 } });
  /** 0ms에 연결을 열고 stats(FRESH 기준)와 차량 링크 상태를 받은 상태 */
  const state = (vehicleLink: "CONNECTED" | "DEGRADED" | "LOST" | null, withRoute = true) => {
    let s = apply(connectionOpened(initialState(), 0), stats, 0);
    if (vehicleLink) s = apply(s, link(vehicleLink), 0);
    return withRoute ? apply(s, routeMsg, 0) : s;
  };
  const at = (wall: number) => ({ wall, mono: wall });

  expect(present(state("CONNECTED", false), open, at(1000)).route).toBeNull(); // route를 받기 전에는 경로점 없음
  expect(present(state("CONNECTED"), open, at(1000)).route).toEqual({ waypoints: [[36.1, 127.1], [36.2, 127.2]], stale: false });
  expect(present(state("DEGRADED"), open, at(1000)).route?.stale).toBe(true);
  expect(present(state("LOST"), open, at(1000)).route?.stale).toBe(true);
  expect(present(state(null), open, at(1000)).route?.stale).toBe(true); // 차량 link_status를 받은 적 없음
  expect(present(state("CONNECTED"), open, at(2001)).route?.stale).toBe(true); // 마지막 stats 뒤 2초 넘음 (STALE)
  expect(present(state("CONNECTED"), { state: "WAITING_RETRY", failures: 1, retryAtMono: 5000 }, at(1000)).route?.stale).toBe(true);
});
