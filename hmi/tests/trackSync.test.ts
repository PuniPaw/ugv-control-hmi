// 궤적 그리기 계획 (map/trackSync.ts). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import type { Flag, If03Message } from "../src/if03/messages";
import { TRACK_MAX_POINTS, TRACK_TRIM_POINTS, apply, connectionLost, initialState, type HmiState } from "../src/state/hmiState";
import { CHUNK_POINTS, EMPTY_DRAWN, chunk, planDraw, type DrawOp, type Drawn } from "../src/map/trackSync";

const tel = (i: number, flags: Flag[] = ["GPS_VALID"]): If03Message => ({
  type: "telemetry", ts: i,
  data: { lat: i / 1000, lon: 0, heading_deg: 0, speed_mps: 0, battery_pct: 50, mode: "MISSION", flags, seq: i, rx_ts: i },
});

/** 연산을 실행해 선 조각 목록을 만든다 (지도 대신) */
function execute(lines: Array<Array<[number, number]>>, ops: DrawOp[]) {
  for (const o of ops) {
    if (o.op === "clear") lines.length = 0;
    else if (o.op === "newLine") lines.push([...o.points]);
    else lines[lines.length - 1]!.push(o.point);
  }
}

it("REQ_HMI_001 plans extend, new line and rebuild from successive apply() results", () => {
  const s0 = initialState();
  const s1 = apply(s0, tel(1), 0);
  const s2 = apply(s1, tel(2), 0);
  const s3 = apply(connectionLost(s2), tel(3), 0);
  let d: Drawn = EMPTY_DRAWN;
  const step = (track: typeof s0.track) => {
    const p = planDraw(d, track);
    d = p.drawn;
    return p.ops;
  };
  expect(step(s1.track)).toEqual([{ op: "newLine", points: [[0.001, 0]] }]);
  expect(step(s2.track)).toEqual([{ op: "extend", point: [0.002, 0] }]);
  expect(step(s3.track)).toEqual([{ op: "newLine", points: [[0.003, 0]] }]);
  expect(step(apply(s3, tel(4, []), 0).track)).toEqual([]);
  expect(step([])).toEqual([{ op: "clear" }]);
});

it("REQ_HMI_001 executing the planned ops always draws exactly the state track", () => {
  let s: HmiState = initialState();
  let d: Drawn = EMPTY_DRAWN;
  const lines: Array<Array<[number, number]>> = [];
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) % 2 ** 31) / 2 ** 31);
  for (let i = 0; i < 8000; i++) {
    const r = rnd();
    if (r < 0.01) s = connectionLost(s);
    else s = apply(s, tel(i, r < 0.2 ? [] : ["GPS_VALID"]), 0);
    const p = planDraw(d, s.track);
    d = p.drawn;
    execute(lines, p.ops);
    expect(lines.every((l) => l.length <= CHUNK_POINTS)).toBe(true);
  }
  // 조각을 다시 이어 구간으로: 앞 조각의 끝점(같은 점 객체)으로 시작하는 조각은 같은 구간이다
  const segments: Array<Array<[number, number]>> = [];
  let prev: Array<[number, number]> | null = null;
  for (const l of lines) {
    if (prev && l[0] === prev[prev.length - 1]) segments[segments.length - 1]!.push(...l.slice(1));
    else segments.push([...l]);
    prev = l;
  }
  expect(segments).toEqual(s.track);
});

it("REQ_HMI_001 rebuilds only when the oldest points are trimmed", () => {
  let s: HmiState = initialState();
  let d: Drawn = EMPTY_DRAWN;
  const kinds: Record<string, number> = {};
  for (let i = 0; i < TRACK_MAX_POINTS * 2; i++) {
    const next = apply(s, tel(i), 0);
    const p = planDraw(d, next.track);
    const k = p.ops[0]?.op ?? "none";
    kinds[k] = (kinds[k] ?? 0) + 1;
    d = p.drawn;
    s = next;
  }
  // 첫 삭제는 TRACK_MAX_POINTS+1번째 점, 이후 TRACK_TRIM_POINTS+1점마다 한 번 (10Hz면 약 30초에 한 번)
  const after = TRACK_MAX_POINTS * 2 - TRACK_MAX_POINTS - 1;
  // 앞쪽 삭제가 아니면 전체 다시 그리기(clear)가 없어야 한다: apply()가 바뀐 구간만 새로 만든다는 계약을 고정
  expect(kinds.clear).toBe(Math.floor(after / (TRACK_TRIM_POINTS + 1)) + 1);
  expect(kinds.extend! + kinds.newLine! + kinds.clear!).toBe(TRACK_MAX_POINTS * 2);
});

it("REQ_HMI_001 chunks share end points so the drawn line has no gaps", () => {
  const seg = Array.from({ length: 450 }, (_, i): [number, number] => [i, i]);
  const parts = chunk(seg);
  expect(parts.every((p) => p.length <= CHUNK_POINTS)).toBe(true);
  for (let i = 1; i < parts.length; i++) expect(parts[i]![0]).toBe(parts[i - 1]!.at(-1));
  expect(parts.flat().length - (parts.length - 1)).toBe(seg.length);
  expect(chunk([[1, 1]])).toEqual([[[1, 1]]]);
});
