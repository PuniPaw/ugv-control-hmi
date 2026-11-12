// HMI가 화면에 그리는 상태. IF-03 메시지를 하나씩 apply()로 반영한다 (순수 함수, 단위 시험 대상).

import type { If03Message, LinkState, Source, StatsData, TelemetryData } from "../if03/messages";

export const TRACK_MAX_POINTS = 3000; // 10Hz 기준 약 5분
export const FRESHNESS_LIMIT_MS = 2000; // ICD 7장: stats가 2초 넘게 오지 않거나 늦으면 "GW 데이터 지연"

export interface LinkView {
  state: LinkState;
  lastRxTs: number | null;
}

export interface HmiState {
  link: Record<Source, LinkView>;
  stats: Record<Source, StatsData | null>;
  vehicle: TelemetryData | null; // 마지막 텔레메트리
  track: Array<[number, number]>; // [lat, lon], 오래된 것부터
  lastStatsReceivedAt: number | null; // HMI 시계로 stats를 받은 시각 (ms)
  lastStatsGwTs: number | null; // 그 stats의 GW 시각 (ms)
  rejected: number; // IF-03 형식이 아니어서 버린 메시지 수
}

export function initialState(): HmiState {
  return {
    link: {
      VEHICLE: { state: "LOST", lastRxTs: null }, // GW와 같은 초기값: 첫 패킷 전은 LOST
      DETECTOR: { state: "LOST", lastRxTs: null },
    },
    stats: { VEHICLE: null, DETECTOR: null },
    vehicle: null,
    track: [],
    lastStatsReceivedAt: null,
    lastStatsGwTs: null,
    rejected: 0,
  };
}

/** 메시지 하나를 반영한 새 상태. receivedAt은 HMI가 받은 시각(ms). */
export function apply(s: HmiState, m: If03Message, receivedAt: number): HmiState {
  switch (m.type) {
    case "telemetry": {
      const point: [number, number] = [m.data.lat, m.data.lon];
      const track = s.track.length >= TRACK_MAX_POINTS ? [...s.track.slice(1), point] : [...s.track, point];
      return { ...s, vehicle: m.data, track };
    }
    case "link_status":
      return {
        ...s,
        link: { ...s.link, [m.data.source]: { state: m.data.state, lastRxTs: m.data.last_rx_ts } },
      };
    case "stats":
      return {
        ...s,
        stats: { ...s.stats, [m.data.source]: m.data },
        lastStatsReceivedAt: receivedAt,
        lastStatsGwTs: m.ts,
      };
    case "detection":
      return s; // 영상 오버레이(12월 3주)에서 쓴다
  }
}

export type Freshness = "WAITING" | "FRESH" | "STALE";

/**
 * GW 데이터 신선도 (ICD 7장, TBD-04). GW는 stats를 송신원마다 1Hz로 보낸다.
 * - 아직 stats를 받지 못함: WAITING
 * - 마지막 stats를 받은 지 2초 넘음, 또는 그 stats가 GW 시각보다 2초 넘게 늦게 도착: STALE
 *   (두 번째 조건은 GW와 HMI 시계가 맞는다고 가정한다. 같은 PC에서는 항상 맞는다)
 */
export function freshness(s: HmiState, now: number): Freshness {
  if (s.lastStatsReceivedAt === null || s.lastStatsGwTs === null) return "WAITING";
  if (now - s.lastStatsReceivedAt > FRESHNESS_LIMIT_MS) return "STALE";
  if (s.lastStatsReceivedAt - s.lastStatsGwTs > FRESHNESS_LIMIT_MS) return "STALE";
  return "FRESH";
}
