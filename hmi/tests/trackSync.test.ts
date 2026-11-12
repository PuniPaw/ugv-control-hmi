// 궤적 그리기 계획 (map/trackSync.ts). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import type { Flag, If03Message } from "../src/if03/messages";
import { TRACK_MAX_POINTS, TRACK_TRIM_POINTS, apply, connectionLost, initialState, type HmiState } from "../src/state/hmiState";
import { CHUNK_POINTS, chunk, planTrackUpdate } from "../src/map/trackSync";

const tel = (i: number, flags: Flag[] = ["GPS_VALID"]): If03Message => ({
  type: "telemetry", ts: i,
  data: { lat: i / 1000, lon: 0, heading_deg: 0, speed_mps: 0, battery_pct: 50, mode: "MISSION", flags, seq: i, rx_ts: i },
});

it("REQ_HMI_001 plans append, new segment and rebuild from successive apply() results", () => {
  const s0 = initialState();
  const s1 = apply(s0, tel(1), 0);
  const s2 = apply(s1, tel(2), 0);
  const s3 = apply(connectionLost(s2), tel(3), 0);
  expect(planTrackUpdate(s0.track, s1.track)).toEqual({ kind: "newSegment", point: [0.001, 0] });
  expect(planTrackUpdate(s1.track, s2.track)).toEqual({ kind: "append", point: [0.002, 0] });
  expect(planTrackUpdate(s2.track, s3.track)).toEqual({ kind: "newSegment", point: [0.003, 0] });
  expect(planTrackUpdate(s3.track, apply(s3, tel(4, []), 0).track)).toEqual({ kind: "none" });
  expect(planTrackUpdate(s3.track, [])).toEqual({ kind: "rebuild" });
});

it("REQ_HMI_001 rebuilds only when the oldest points are trimmed", () => {
  let s: HmiState = initialState();
  const kinds: Record<string, number> = {};
  for (let i = 0; i < TRACK_MAX_POINTS * 2; i++) {
    const next = apply(s, tel(i), 0);
    const k = planTrackUpdate(s.track, next.track).kind;
    kinds[k] = (kinds[k] ?? 0) + 1;
    s = next;
  }
  // 첫 삭제는 TRACK_MAX_POINTS+1번째 점, 이후 TRACK_TRIM_POINTS+1점마다 한 번 (10Hz면 약 30초에 한 번)
  const after = TRACK_MAX_POINTS * 2 - TRACK_MAX_POINTS - 1;
  expect(kinds.rebuild).toBe(Math.floor(after / (TRACK_TRIM_POINTS + 1)) + 1);
  expect(kinds.newSegment).toBe(1);
});

it("REQ_HMI_001 chunks share end points so the drawn line has no gaps", () => {
  const seg = Array.from({ length: 450 }, (_, i): [number, number] => [i, i]);
  const parts = chunk(seg);
  expect(parts.every((p) => p.length <= CHUNK_POINTS)).toBe(true);
  for (let i = 1; i < parts.length; i++) expect(parts[i]![0]).toBe(parts[i - 1]!.at(-1));
  expect(parts.flat().length - (parts.length - 1)).toBe(seg.length);
  expect(chunk([[1, 1]])).toEqual([[[1, 1]]]);
});
