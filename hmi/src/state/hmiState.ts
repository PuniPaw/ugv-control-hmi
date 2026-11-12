// HMI가 화면에 그리는 상태. IF-03 메시지는 apply()로, 연결 끊김은 connectionLost()로 반영한다.
// 모두 순수 함수이며 시각은 인자로 받는다 (단위 시험에서 대기 없이 경계값 확인).

import type { If03Message, LinkState, Source, StatsData, TelemetryData } from "../if03/messages";

export const TRACK_MAX_POINTS = 3000; // 10Hz 기준 약 5분
// 상한을 넘으면 이만큼 더 지운다: 앞쪽 삭제(지도 전체 다시 그리기)를 매 점이 아니라 30초(10Hz)에 한 번으로
export const TRACK_TRIM_POINTS = 300;
export const FRESHNESS_LIMIT_MS = 2000; // ICD 7장, REQ-HMI-006

export type LatLon = [number, number];

export interface LinkView {
  state: LinkState;
  lastRxTs: number | null; // GW 시계
}

/** 마지막으로 받은 GPS 유효 위치 */
export interface Fix {
  lat: number;
  lon: number;
  heading_deg: number;
}

export interface HmiState {
  link: Record<Source, LinkView>;
  stats: Record<Source, StatsData | null>;
  vehicle: TelemetryData | null; // 마지막 텔레메트리 (위치가 무효일 수 있음)
  fix: Fix | null; // 마지막 유효 위치. GPS_VALID가 없는 텔레메트리는 이것을 바꾸지 않는다
  // 궤적: 끊긴 구간마다 따로 (연결 끊김, 차량 LOST, GPS 무효 뒤에는 새 구간). 오래된 것부터
  track: LatLon[][];
  trackOpen: boolean; // 다음 유효 위치를 마지막 구간에 이을지
  // GW 시계 → HMI 시계 기준선: 이 연결에서 관측한 (수신 시각 − ts)의 최솟값.
  // 두 PC의 시계 차와 최소 전달 지연이 들어 있다. 신선도와 경과 시간은 이 값으로 상대 판정한다
  clockOffset: number | null;
  lastStats: { receivedAt: number; gwTs: number } | null; // 이 연결에서 마지막 stats
  rejected: number; // IF-03 형식이 아니어서 버린 메시지 수 (누계)
}

export function initialState(): HmiState {
  return {
    link: {
      VEHICLE: { state: "LOST", lastRxTs: null }, // GW와 같은 초기값: 첫 패킷 전은 LOST
      DETECTOR: { state: "LOST", lastRxTs: null },
    },
    stats: { VEHICLE: null, DETECTOR: null },
    vehicle: null,
    fix: null,
    track: [],
    trackOpen: false,
    clockOffset: null,
    lastStats: null,
    rejected: 0,
  };
}

export function gpsValid(v: TelemetryData): boolean {
  return v.flags.includes("GPS_VALID");
}

function appendPoint(track: LatLon[][], open: boolean, p: LatLon): LatLon[][] {
  const out = open && track.length > 0 ? [...track.slice(0, -1), [...track[track.length - 1]!, p]] : [...track, [p]];
  // 전체 점 수 상한: 넘으면 가장 오래된 구간의 앞에서부터 (상한 − TRACK_TRIM_POINTS)까지 지운다
  let total = out.reduce((n, seg) => n + seg.length, 0);
  if (total <= TRACK_MAX_POINTS) return out;
  const target = TRACK_MAX_POINTS - TRACK_TRIM_POINTS;
  while (total > target) {
    const first = out[0]!;
    const drop = Math.min(first.length, total - target);
    total -= drop;
    if (drop === first.length) out.shift();
    else out[0] = first.slice(drop);
  }
  return out;
}

/** 메시지 하나를 반영한 새 상태. receivedAt은 HMI 시계로 받은 시각(ms). */
export function apply(s0: HmiState, m: If03Message, receivedAt: number): HmiState {
  const offset = receivedAt - m.ts;
  const s = { ...s0, clockOffset: s0.clockOffset === null ? offset : Math.min(s0.clockOffset, offset) };
  switch (m.type) {
    case "telemetry": {
      const v = m.data;
      if (!gpsValid(v)) return { ...s, vehicle: v, trackOpen: false }; // 위치·궤적은 그대로, 다음 유효 위치는 새 구간
      return {
        ...s,
        vehicle: v,
        fix: { lat: v.lat, lon: v.lon, heading_deg: v.heading_deg },
        track: appendPoint(s.track, s.trackOpen, [v.lat, v.lon]),
        trackOpen: true,
      };
    }
    case "link_status": {
      const link = { ...s.link, [m.data.source]: { state: m.data.state, lastRxTs: m.data.last_rx_ts } };
      const vehicleLost = m.data.source === "VEHICLE" && m.data.state === "LOST";
      return { ...s, link, trackOpen: vehicleLost ? false : s.trackOpen };
    }
    case "stats":
      return {
        ...s,
        stats: { ...s.stats, [m.data.source]: m.data },
        lastStats: { receivedAt, gwTs: m.ts },
      };
    case "detection":
      return s; // 영상 오버레이(12월 3주)에서 쓴다
  }
}

/**
 * IF-03 연결이 끊겼을 때. 마지막 값(링크·차량·궤적)은 "이전 값"으로 남기고, 이 연결에서만 뜻이 있는
 * 신선도 기준(lastStats, clockOffset)은 지운다 → 재접속 후 첫 stats 전까지 WAITING.
 */
export function connectionLost(s: HmiState): HmiState {
  return { ...s, lastStats: null, clockOffset: null, trackOpen: false };
}

export function rejectedMessage(s: HmiState): HmiState {
  return { ...s, rejected: s.rejected + 1 };
}

export type Freshness = "WAITING" | "FRESH" | "STALE";

/**
 * GW 데이터 신선도 (ICD 7장, REQ-HMI-006). GW는 stats를 송신원마다 1Hz로 보낸다.
 * - 이 연결에서 아직 stats를 받지 못함: WAITING
 * - 마지막 stats를 받은 지 2초 넘음: STALE (stats가 끊김)
 * - 마지막 stats의 (수신 시각 − ts)가 기준선(clockOffset)보다 2초 넘게 큼: STALE (늦게 도착)
 *   기준선과의 차이만 보므로 GW PC와 HMI PC의 시계 차는 판정에 들어가지 않는다
 */
export function freshness(s: HmiState, now: number): Freshness {
  if (s.lastStats === null || s.clockOffset === null) return "WAITING";
  if (now - s.lastStats.receivedAt > FRESHNESS_LIMIT_MS) return "STALE";
  if (s.lastStats.receivedAt - s.lastStats.gwTs - s.clockOffset > FRESHNESS_LIMIT_MS) return "STALE";
  return "FRESH";
}

/** GW 시각을 HMI 시각으로 (기준선이 없으면 null) */
export function toLocalTime(s: HmiState, gwTs: number): number | null {
  return s.clockOffset === null ? null : gwTs + s.clockOffset;
}
