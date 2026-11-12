// 궤적을 지도에 그리는 방법 (REQ-HMI-001). Leaflet은 점을 하나 덧붙여도 polyline 전체를 다시 투영하므로
// (실측: 3000점 SVG polyline에 addLatLng 한 번 16.7ms, 200점은 0.14ms), 궤적을 CHUNK_POINTS점 이하의
// 조각으로 나눠 그리고 새 점은 마지막 조각에만 덧붙인다.
// 이 파일은 Leaflet을 모른다: 이전·다음 궤적을 비교해 할 일만 정한다 (순수 함수, 단위 시험 대상).

import type { LatLon } from "../state/hmiState";

export const CHUNK_POINTS = 200;

export type TrackUpdate =
  | { kind: "none" }
  | { kind: "append"; point: LatLon } // 마지막 구간에 점 하나
  | { kind: "newSegment"; point: LatLon } // 점 하나짜리 새 구간
  | { kind: "rebuild" }; // 그 밖(앞쪽 삭제 등): 다시 그린다

/** apply()는 바뀐 구간만 새 배열로 만들고 점 배열([lat, lon])은 그대로 옮긴다 → 참조 비교로 충분하다 */
export function planTrackUpdate(prev: LatLon[][], next: LatLon[][]): TrackUpdate {
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
