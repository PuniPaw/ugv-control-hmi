// HMI 상태 갱신·신선도 (순수 함수). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import type { Flag, If03Message } from "../src/if03/messages";
import {
  FRESHNESS_LIMIT_MS, TRACK_MAX_POINTS, TRACK_TRIM_POINTS, apply, connectionLost, freshness, initialState, type HmiState,
} from "../src/state/hmiState";

const GPS: Flag[] = ["GPS_VALID"];
const telemetry = (seq: number, lat: number, lon: number, flags: Flag[] = GPS, ts = 1000): If03Message => ({
  type: "telemetry",
  ts,
  data: { lat, lon, heading_deg: 90, speed_mps: 4.5, battery_pct: 76, mode: "MISSION", flags, seq, rx_ts: ts },
});
const ERRORS = { sync: 0, version: 0, length: 0, crc: 0, type: 0, range: 0, order: 0 };
const stats = (ts: number): If03Message => ({
  type: "stats",
  ts,
  data: { source: "VEHICLE", loss_rate_10s: 0.1, rx_count: 90, lost_count: 10, errors: ERRORS },
});
const vehicleLink = (state: "CONNECTED" | "LOST", ts = 0): If03Message => ({
  type: "link_status", ts, data: { source: "VEHICLE", state, last_rx_ts: ts },
});
const run = (msgs: Array<[If03Message, number]>, s: HmiState = initialState()) =>
  msgs.reduce((acc, [m, at]) => apply(acc, m, at), s);

it("REQ_HMI_002 starts with both links LOST like the GW", () => {
  const s = initialState();
  expect(s.link.VEHICLE.state).toBe("LOST");
  expect(s.link.DETECTOR.state).toBe("LOST");
  expect(s.vehicle).toBeNull();
});

it("REQ_HMI_001 telemetry updates position and appends to the track", () => {
  const s = run([[telemetry(0, 36.1, 127.1), 0], [telemetry(1, 36.2, 127.2), 0]]);
  expect(s.fix).toEqual({ lat: 36.2, lon: 127.2, heading_deg: 90 });
  expect(s.track).toEqual([[[36.1, 127.1], [36.2, 127.2]]]);
});

it("REQ_HMI_001 track never exceeds TRACK_MAX_POINTS and drops the oldest points in blocks", () => {
  let s = initialState();
  for (let i = 0; i < TRACK_MAX_POINTS; i++) s = apply(s, telemetry(i, i / 100, i / 100), 0);
  expect(s.track.flat().length).toBe(TRACK_MAX_POINTS);
  s = apply(s, telemetry(TRACK_MAX_POINTS, 99, 99), 0); // 상한을 넘는 점: 오래된 것부터 한 묶음 삭제
  const kept = TRACK_MAX_POINTS - TRACK_TRIM_POINTS;
  expect(s.track.flat().length).toBe(kept);
  expect(s.track.flat()[0]).toEqual([(TRACK_MAX_POINTS + 1 - kept) / 100, (TRACK_MAX_POINTS + 1 - kept) / 100]);
  expect(s.track.flat().at(-1)).toEqual([99, 99]);
});

it("REQ_HMI_001 position without GPS_VALID does not move the vehicle or the track", () => {
  const s = run([[telemetry(0, 36.1, 127.1), 0], [telemetry(1, 0, 0, ["CAMERA_OK"]), 0]]);
  expect(s.vehicle?.seq).toBe(1); // 모드·배터리 등은 최신 값
  expect(s.fix).toEqual({ lat: 36.1, lon: 127.1, heading_deg: 90 });
  expect(s.track).toEqual([[[36.1, 127.1]]]);
});

it("REQ_HMI_001 track starts a new segment after GPS loss, vehicle LOST or GW disconnect", () => {
  let s = run([[telemetry(0, 1, 1), 0], [telemetry(1, 0, 0, []), 0], [telemetry(2, 2, 2), 0]]);
  s = run([[vehicleLink("LOST"), 0], [telemetry(3, 3, 3), 0]], s);
  s = run([[telemetry(4, 4, 4), 0]], connectionLost(s));
  expect(s.track).toEqual([[[1, 1]], [[2, 2]], [[3, 3]], [[4, 4]]]);
});

it("REQ_HMI_002 link_status and stats update per source", () => {
  const s = run([
    [{ type: "link_status", ts: 1, data: { source: "VEHICLE", state: "DEGRADED", last_rx_ts: 7 } }, 0],
    [stats(10), 20],
  ]);
  expect(s.link.VEHICLE).toEqual({ state: "DEGRADED", lastRxTs: 7 });
  expect(s.link.DETECTOR.state).toBe("LOST");
  expect(s.stats.VEHICLE?.loss_rate_10s).toBe(0.1);
  expect(s.stats.DETECTOR).toBeNull();
});

it("REQ_HMI_007 apply does not mutate the previous state", () => {
  const s0 = initialState();
  const s1 = apply(s0, telemetry(0, 1, 2), 0);
  expect(s0.track).toEqual([]);
  expect(s0.clockOffset).toBeNull();
  expect(s1).not.toBe(s0);
});

it("REQ_HMI_006 WAITING before the first stats of a connection", () => {
  expect(freshness(initialState(), 0)).toBe("WAITING");
  const s = run([[stats(1000), 1000]]);
  expect(freshness(connectionLost(s), 1100)).toBe("WAITING"); // 재접속 직후 이전 연결의 stats로 판정하지 않음
});

it("REQ_HMI_006 FRESH within 2s of the last stats, STALE after", () => {
  const s = run([[stats(1000), 1000]]);
  expect(freshness(s, 1000 + FRESHNESS_LIMIT_MS)).toBe("FRESH");
  expect(freshness(s, 1000 + FRESHNESS_LIMIT_MS + 1)).toBe("STALE");
});

it("REQ_HMI_006 STALE when stats arrive more than 2s later than the connection baseline", () => {
  const s = run([[stats(1000), 1050], [stats(2000), 2050 + FRESHNESS_LIMIT_MS]]);
  expect(freshness(s, 2050 + FRESHNESS_LIMIT_MS)).toBe("FRESH"); // 정확히 2000ms 늦음
  const late = run([[stats(3000), 3051 + FRESHNESS_LIMIT_MS]], s);
  expect(freshness(late, 3051 + FRESHNESS_LIMIT_MS)).toBe("STALE");
});

it("REQ_HMI_006 freshness ignores a constant clock difference between GW and HMI", () => {
  for (const skew of [-5000, 3000, 3_600_000]) {
    const s = run([[stats(1000), 1050 + skew], [stats(2000), 2050 + skew]]);
    expect(freshness(s, 2100 + skew), `skew ${skew}`).toBe("FRESH");
    const late = run([[stats(3000), 3050 + skew + 2500]], s);
    expect(freshness(late, 3050 + skew + 2500), `skew ${skew}`).toBe("STALE");
  }
});
