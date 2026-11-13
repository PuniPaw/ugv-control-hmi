// 화면 표시 정책 (state/view.ts). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import type { If03Message, TelemetryData } from "../src/if03/messages";
import { apply, connectionOpened, initialState } from "../src/state/hmiState";
import { banner, batteryLevel, linkAgeText, positionText, staleness, type ConnView } from "../src/state/view";

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

