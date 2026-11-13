// 궤적을 지도에 그리는 방법 (REQ-HMI-001). Leaflet은 점을 하나 덧붙여도 polyline 전체를 다시 투영하므로
// (실측: 3000점 SVG polyline에 addLatLng 한 번 16.7ms, 200점은 0.14ms), 궤적을 CHUNK_POINTS점 이하의
// 선 조각으로 나눠 그리고 새 점은 마지막 조각에만 덧붙인다.
// 이 파일은 Leaflet을 모른다: 이전·다음 궤적을 비교해 그리기 연산 목록을 낸다 (순수 함수, 단위 시험 대상).
// MapView는 연산을 순서대로 실행만 한다.

import type { LatLon } from "../state/hmiState";

export const CHUNK_POINTS = 200;

export type DrawOp =
  | { op: "clear" } // 모든 선 조각 제거
  | { op: "newLine"; points: LatLon[] } // 새 선 조각
  | { op: "extend"; point: LatLon }; // 마지막 선 조각에 점 하나

/** 지금 그려진 것: 비교 기준이 되는 궤적과 마지막 선 조각의 점 수 */
export interface Drawn {
  track: LatLon[][];
  lastLinePoints: number;
}

export const EMPTY_DRAWN: Drawn = { track: [], lastLinePoints: 0 };

type Change = { kind: "none" } | { kind: "append"; point: LatLon } | { kind: "newSegment"; point: LatLon } | { kind: "rebuild" };

/**
 * 무엇이 바뀌었는지. hmiState의 계약(점이 늘 때는 마지막 구간 배열만 새로 만들고 다른 배열은 그대로 둔다)에
 * 기대어 참조로 비교한다. 계약이 깨지면 매번 "rebuild"가 되는데, 이는 시험
 * "REQ_HMI_001 rebuilds only when the oldest points are trimmed"가 잡는다.
 */
function diff(prev: LatLon[][], next: LatLon[][]): Change {
  if (prev === next) return { kind: "none" };
  const sameHead = (n: number) => prev.slice(0, n).every((seg, i) => seg === next[i]);
  if (next.length === prev.length && prev.length > 0 && sameHead(prev.length - 1)) {
    const a = prev[prev.length - 1]!;
    const b = next[next.length - 1]!;
    if (b.length === a.length + 1 && b[0] === a[0] && b[a.length - 1] === a[a.length - 1]) {
      return { kind: "append", point: b[b.length - 1]! };
    }
  }
  if (next.length === prev.length + 1 && sameHead(prev.length) && next[next.length - 1]!.length === 1) {
    return { kind: "newSegment", point: next[next.length - 1]![0]! };
  }
  return { kind: "rebuild" };
}

/** 구간 하나를 CHUNK_POINTS점 이하 조각으로. 조각끼리는 끝점을 공유해 이어 그린다 */
export function chunk(seg: LatLon[], size = CHUNK_POINTS): LatLon[][] {
  const out: LatLon[][] = [];
  for (let start = 0; start < seg.length; start += size - 1) {
    out.push(seg.slice(start, start + size));
    if (start + size >= seg.length) break;
  }
  return out;
}

/** 이전에 그린 것과 다음 궤적 → 그리기 연산과 그 뒤의 Drawn */
export function planDraw(drawn: Drawn, next: LatLon[][]): { ops: DrawOp[]; drawn: Drawn } {
  const c = diff(drawn.track, next);
  switch (c.kind) {
    case "none":
      return { ops: [], drawn };
    case "append": {
      if (drawn.lastLinePoints < CHUNK_POINTS) {
        return { ops: [{ op: "extend", point: c.point }], drawn: { track: next, lastLinePoints: drawn.lastLinePoints + 1 } };
      }
      const prevSeg = drawn.track[drawn.track.length - 1]!;
      const from = prevSeg[prevSeg.length - 1]!; // 앞 조각의 끝점부터 이어서
      return { ops: [{ op: "newLine", points: [from, c.point] }], drawn: { track: next, lastLinePoints: 2 } };
    }
    case "newSegment":
      return { ops: [{ op: "newLine", points: [c.point] }], drawn: { track: next, lastLinePoints: 1 } };
    case "rebuild": {
      const lines = next.flatMap((seg) => chunk(seg));
      const ops: DrawOp[] = [{ op: "clear" }, ...lines.map((points): DrawOp => ({ op: "newLine", points }))];
      return { ops, drawn: { track: next, lastLinePoints: lines[lines.length - 1]?.length ?? 0 } };
    }
  }
}
