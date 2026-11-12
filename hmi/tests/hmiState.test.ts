// HMI 상태 갱신·신선도 (순수 함수). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import type { Flag, If03Message } from "../src/if03/messages";
import {
  FRESHNESS_LIMIT_MS, GPS_GAP_SAMPLES, RESYNC_AFTER_MS, TRACK_MAX_POINTS, TRACK_TRIM_POINTS, apply, connectionLost,
  connectionOpened, freshness, initialState, needsResync, receiveText, toLocalTime, type HmiState,
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
/** 연결을 at에 연 상태 */
const opened = (at = 0) => connectionOpened(initialState(), at);

it("REQ_HMI_002 starts with both links LOST like the GW", () => {
  const s = initialState();
  expect(s.link.VEHICLE.state).toBe("LOST");
  expect(s.link.DETECTOR.state).toBe("LOST");
  expect(s.vehicle).toBeNull();
});

it("REQ_HMI_001 telemetry updates position and appends to the track", () => {
  const s0 = initialState();
  const s1 = apply(s0, telemetry(0, 36.1, 127.1), 0);
  const s = apply(s1, telemetry(1, 36.2, 127.2), 0);
  expect(s.fix).toEqual({ lat: 36.2, lon: 127.2, heading_deg: 90 });
  expect(s.track).toEqual([[[36.1, 127.1], [36.2, 127.2]]]);
  expect(s0.track).toEqual([]); // 이전 상태는 바뀌지 않는다 (지도 그리기가 참조 비교에 의존)
  expect(s1.track).toEqual([[[36.1, 127.1]]]);
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
  let s = run([[telemetry(0, 1, 1), 0]]);
  for (let i = 0; i < GPS_GAP_SAMPLES; i++) s = apply(s, telemetry(1, 0, 0, []), 0); // 약 1초 동안 GPS 무효
  s = run([[telemetry(2, 2, 2), 0]], s);
  s = run([[vehicleLink("LOST"), 0], [telemetry(3, 3, 3), 0]], s);
  s = run([[telemetry(4, 4, 4), 0]], connectionLost(s));
  expect(s.track).toEqual([[[1, 1]], [[2, 2]], [[3, 3]], [[4, 4]]]);
});

it("REQ_HMI_001 brief GPS_VALID flicker keeps the segment so the track stays drawable", () => {
  let s = initialState();
  for (let i = 0; i < 40; i++) s = apply(s, telemetry(i, i, i, i % 2 === 0 ? GPS : []), 0); // 한 표본씩 번갈아
  expect(s.track.length).toBe(1);
  expect(s.track[0]!.length).toBe(20);
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

it("REQ_HMI_006 WAITING before the first stats of a connection, for at most 2s", () => {
  expect(freshness(initialState(), 0)).toBe("WAITING");
  const s = run([[stats(1000), 1000]], opened(900));
  const again = connectionOpened(connectionLost(s), 1100);
  expect(freshness(again, 1100)).toBe("WAITING"); // 재접속 직후 이전 연결의 stats로 판정하지 않음
  expect(freshness(again, 1100 + FRESHNESS_LIMIT_MS)).toBe("WAITING");
  // telemetry는 오지만 stats가 한 번도 오지 않음(예: 모두 폐기) → 2초 뒤 STALE
  const noStats = run([[telemetry(0, 1, 1, GPS, 1200), 1200]], again);
  expect(freshness(noStats, 1101 + FRESHNESS_LIMIT_MS)).toBe("STALE");
});

it("REQ_HMI_006 FRESH within 2s of the last stats, STALE after", () => {
  const s = run([[stats(1000), 1000]], opened());
  expect(freshness(s, 1000 + FRESHNESS_LIMIT_MS)).toBe("FRESH");
  expect(freshness(s, 1000 + FRESHNESS_LIMIT_MS + 1)).toBe("STALE");
});

it("REQ_HMI_006 STALE when stats arrive more than 2s later than the connection baseline", () => {
  const s = run([[stats(1000), 1050], [stats(2000), 2050 + FRESHNESS_LIMIT_MS]], opened());
  expect(freshness(s, 2050 + FRESHNESS_LIMIT_MS)).toBe("FRESH"); // 기준선보다 정확히 2000ms 늦음
  const late = run([[stats(3000), 3051 + FRESHNESS_LIMIT_MS]], s);
  expect(freshness(late, 3051 + FRESHNESS_LIMIT_MS)).toBe("STALE"); // 기준선보다 2001ms 늦음
});

it("REQ_HMI_006 freshness ignores a constant clock difference between GW and HMI", () => {
  for (const skew of [-5000, 3000, 3_600_000]) {
    const s = run([[stats(1000), 1050 + skew], [stats(2000), 2050 + skew]], opened(skew));
    expect(freshness(s, 2100 + skew), `skew ${skew}`).toBe("FRESH");
    const late = run([[stats(3000), 3050 + skew + 2500]], s);
    expect(freshness(late, 3050 + skew + 2500), `skew ${skew}`).toBe("STALE");
  }
});

it("REQ_HMI_006 data that keeps arriving late asks for a resync, on-time data does not", () => {
  // HMI 시계가 연결 중에 +3초 계단 변화: 이후 stats는 모두 기준선보다 3초 늦게 보인다
  let s = run([[stats(1000), 1050]], opened(1000));
  for (let t = 2000; t <= 8000; t += 1000) {
    s = run([[stats(t), t + 50 + 3000]], s);
    expect(needsResync(s, t + 50 + 3000), `t=${t}`).toBe(t + 50 + 3000 - (2000 + 50 + 3000) >= RESYNC_AFTER_MS);
  }
  // 재접속하면 기준선을 새로 잡아 다시 FRESH
  s = run([[stats(9000), 12050]], connectionOpened(connectionLost(s), 12000));
  expect(freshness(s, 12100)).toBe("FRESH");
  expect(needsResync(s, 12100)).toBe(false);
  // 한 번 늦었다가 다시 제때 오면 늦음이 풀린다
  let ok = run([[stats(1000), 1050], [stats(2000), 4550]], opened(1000)); // 2.5초 늦음
  expect(ok.session?.lateSince).toBe(4550);
  ok = run([[stats(5000), 5050]], ok); // 제때
  expect(needsResync(ok, 4550 + RESYNC_AFTER_MS)).toBe(false);
});

it("REQ_HMI_006 one message with an absurd ts recovers through resync", () => {
  const huge = { ...telemetry(0, 1, 1), ts: Number.MAX_SAFE_INTEGER };
  let s = run([[stats(1000), 1050], [huge, 1100]], opened(1000));
  for (let t = 2000; t <= 2000 + RESYNC_AFTER_MS; t += 1000) s = run([[stats(t), t + 50]], s);
  expect(freshness(s, 2000 + RESYNC_AFTER_MS + 50)).toBe("STALE");
  expect(needsResync(s, 2000 + RESYNC_AFTER_MS + 50)).toBe(true);
});

it("REQ_HMI_007 text that is not IF-03 is discarded and counted", () => {
  let s = receiveText(opened(), "not json", 0);
  const outOfRange = telemetry(0, 91, 127); // 다른 필드는 모두 갖추고 위도만 범위 밖
  s = receiveText(s, JSON.stringify(outOfRange), 0);
  expect(s.discarded).toBe(2);
  s = receiveText(s, JSON.stringify(stats(1)), 10);
  expect(s.discarded).toBe(2);
  expect(s.stats.VEHICLE).not.toBeNull();
});

it("REQ_HMI_006 GW time converts to HMI time through the connection baseline", () => {
  const s = run([[stats(10_000), 5_020]], opened(5_000)); // HMI 시계가 GW보다 약 5초 늦음
  expect(toLocalTime(s, 9_000)).toBe(4_020);
  expect(toLocalTime(initialState(), 9_000)).toBeNull();
});

it("REQ_HMI_006 the resync clock starts at the first late stats, not at a message that lowered the baseline", () => {
  // 제때 온 stats 뒤 ts가 비정상적으로 큰 telemetry 하나 → 기준선이 크게 낮아져 이미 받은 stats가 늦은 것이 된다
  let s = run([[stats(1000), 1050]], opened(1000));
  s = run([[{ ...telemetry(0, 1, 1), ts: 1_000_000 }, 1100]], s);
  expect(freshness(s, 1100)).toBe("STALE"); // 화면은 바로 지연
  expect(s.session?.lateSince).toBeNull(); // 재설정 시계는 아직 시작하지 않음
  s = run([[stats(2000), 2050]], s); // 다음 stats: 기준선보다 늦음 → 여기서부터
  expect(s.session?.lateSince).toBe(2050);
  expect(needsResync(s, 2050 + RESYNC_AFTER_MS - 1)).toBe(false);
  expect(needsResync(s, 2050 + RESYNC_AFTER_MS)).toBe(true);
});
