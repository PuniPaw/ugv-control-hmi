// HMI가 화면에 그리는 상태. 받은 텍스트는 receiveText()로, IF-03 연결의 열림·끊김은 connectionOpened()·
// connectionLost()로 반영한다. 모두 순수 함수이며 시각은 인자로 받는다 (단위 시험에서 대기 없이 경계값 확인).

import { parseIf03, type If03Message, type LinkState, type Source, type StatsData, type TelemetryData } from "../if03/messages";
import type { ConnEvent } from "../net/gwConnection";

export const TRACK_MAX_POINTS = 3000; // 10Hz 기준 약 5분
// 상한을 넘으면 이만큼 더 지운다. 메모리 상한(도메인)과 함께, 앞쪽 삭제 횟수를 30초(10Hz)에 한 번으로 묶는다
// (앞쪽 삭제는 지도에서 전체 다시 그리기가 된다, map/trackSync.ts)
export const TRACK_TRIM_POINTS = 300;
// GPS_VALID 없는 표본이 이만큼 이어져야 궤적을 끊는다 (10Hz 약 1초). 한두 표본 깜박임으로 끊으면
// 점 하나짜리 구간만 남아 궤적이 그려지지 않는다
export const GPS_GAP_SAMPLES = 10;
export const FRESHNESS_LIMIT_MS = 2000; // ICD 7장, REQ-HMI-006
// "늦게 도착"이 이만큼 이어지면 연결을 다시 맺는다 → 기준선을 새로 잡는다 (시계 계단 변화·drift,
// 비정상적으로 큰 ts 하나, HMI 처리 적체를 모두 같은 경로로 회복. ICD 7장)
export const RESYNC_AFTER_MS = 5000;

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

/** 열린 IF-03 연결 하나에서만 뜻이 있는 값. 연결이 끊기면 통째로 버린다 */
export interface Session {
  openedAt: number; // HMI 시계
  // 기준선(GLOSSARY): 이 연결에서 관측한 (수신 시각 − ts)의 최솟값. 두 PC의 시계 차 + 최소 전달 지연
  baseline: number | null;
  lastStats: { receivedAt: number; gwTs: number } | null;
  lateSince: number | null; // stats가 기준선보다 2초 넘게 늦게 오기 시작한 시각 (제때 오면 null)
}

export interface HmiState {
  link: Record<Source, LinkView>;
  stats: Record<Source, StatsData | null>;
  vehicle: TelemetryData | null; // 마지막 텔레메트리 (위치가 무효일 수 있음)
  fix: Fix | null; // 마지막 유효 위치. GPS_VALID가 없는 텔레메트리는 이것을 바꾸지 않는다
  // 궤적: 끊긴 구간마다 따로 (연결 끊김, 차량 LOST, GPS 무효 GPS_GAP_SAMPLES개 뒤에는 새 구간). 오래된 것부터.
  // 계약(map/trackSync.ts가 의존): 점이 늘 때는 마지막 구간 배열만 새로 만들고 다른 구간·점 배열은 그대로 둔다
  track: LatLon[][];
  // 마지막 유효 위치 뒤 GPS 무효 표본 수. GPS_GAP_SAMPLES 이상이면 다음 유효 위치는 새 구간이다
  // (연결 끊김·차량 LOST·처음에는 GPS_GAP_SAMPLES로 둔다)
  samplesSinceFix: number;
  session: Session | null; // IF-03 연결이 열려 있을 때만
  discarded: number; // 형식·범위가 ICD와 달라 폐기한 IF-03 메시지 수 (누계, GLOSSARY "폐기")
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
    samplesSinceFix: GPS_GAP_SAMPLES,
    session: null,
    discarded: 0,
  };
}

export function gpsValid(v: TelemetryData): boolean {
  return v.flags.includes("GPS_VALID");
}

function appendPoint(track: LatLon[][], open: boolean, p: LatLon): LatLon[][] {
  const out = open && track.length > 0 ? [...track.slice(0, -1), [...track[track.length - 1]!, p]] : [...track, [p]];
  let total = out.reduce((n, seg) => n + seg.length, 0);
  if (total <= TRACK_MAX_POINTS) return out;
  // 상한을 넘으면 가장 오래된 구간의 앞에서부터 (상한 − TRACK_TRIM_POINTS)까지 지운다
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

/** IF-03 연결이 열렸을 때 (at: HMI 시계) */
export function connectionOpened(s: HmiState, at: number): HmiState {
  return { ...s, session: { openedAt: at, baseline: null, lastStats: null, lateSince: null } };
}

/**
 * IF-03 연결이 끊겼을 때. 링크·차량·궤적 값은 "이전 값"으로 남기고, 연결 범위 값(session)은 버린다
 * → 재접속 후에는 기준선을 새로 잡는다.
 */
export function connectionLost(s: HmiState): HmiState {
  return { ...s, session: null, samplesSinceFix: GPS_GAP_SAMPLES };
}

/** 연결 이벤트 반영: OPEN이면 새 연결 범위를 시작하고, 그 밖(접속 중·재접속 대기)이면 연결 범위를 버린다 */
export function onConnEvent(s: HmiState, ev: ConnEvent, at: number): HmiState {
  if (ev.state === "OPEN") return connectionOpened(s, at);
  return s.session === null ? s : connectionLost(s);
}

/** 마지막 stats가 지금 기준선보다 2초 넘게 늦게 도착했는지 (늦음 판정은 여기 하나) */
function isLate(se: Session): boolean {
  return se.lastStats !== null && se.baseline !== null &&
    se.lastStats.receivedAt - se.lastStats.gwTs - se.baseline > FRESHNESS_LIMIT_MS;
}

function updateSession(se0: Session, m: If03Message, receivedAt: number): Session {
  const delay = receivedAt - m.ts;
  const baseline = se0.baseline === null ? delay : Math.min(se0.baseline, delay);
  const se = { ...se0, baseline, lastStats: m.type === "stats" ? { receivedAt, gwTs: m.ts } : se0.lastStats };
  return { ...se, lateSince: isLate(se) ? (se0.lateSince ?? receivedAt) : null };
}

/** 메시지 하나를 반영한 새 상태. receivedAt은 HMI 시계로 받은 시각(ms). */
export function apply(s0: HmiState, m: If03Message, receivedAt: number): HmiState {
  const s = s0.session ? { ...s0, session: updateSession(s0.session, m, receivedAt) } : s0;
  switch (m.type) {
    case "telemetry": {
      const v = m.data;
      if (!gpsValid(v)) {
        // 위치·궤적은 그대로. 무효가 GPS_GAP_SAMPLES개 이어지면 다음 유효 위치는 새 구간
        return { ...s, vehicle: v, samplesSinceFix: Math.min(s.samplesSinceFix + 1, GPS_GAP_SAMPLES) };
      }
      return {
        ...s,
        vehicle: v,
        fix: { lat: v.lat, lon: v.lon, heading_deg: v.heading_deg },
        track: appendPoint(s.track, s.samplesSinceFix < GPS_GAP_SAMPLES, [v.lat, v.lon]),
        samplesSinceFix: 0,
      };
    }
    case "link_status": {
      const link = { ...s.link, [m.data.source]: { state: m.data.state, lastRxTs: m.data.last_rx_ts } };
      const vehicleLost = m.data.source === "VEHICLE" && m.data.state === "LOST";
      return { ...s, link, samplesSinceFix: vehicleLost ? GPS_GAP_SAMPLES : s.samplesSinceFix };
    }
    case "stats":
      return { ...s, stats: { ...s.stats, [m.data.source]: m.data } };
    case "detection":
      return s; // 영상 오버레이(12월 3주)에서 쓴다
  }
}

/** 받은 텍스트 하나: IF-03 형식이면 반영, 아니면 폐기하고 센다 (REQ-HMI-007) */
export function receiveText(s: HmiState, text: string, receivedAt: number): HmiState {
  const m = parseIf03(text);
  return m === null ? { ...s, discarded: s.discarded + 1 } : apply(s, m, receivedAt);
}

export type Freshness = "WAITING" | "FRESH" | "STALE";

/**
 * GW 데이터 신선도 (ICD 7장, REQ-HMI-006). GW는 stats를 송신원마다 1Hz로 보낸다.
 * - 연결이 없음, 또는 연결 후 2초 안이고 아직 stats 없음: WAITING
 * - 연결 후 2초가 지나도록 stats 없음(모두 폐기된 경우 포함), 마지막 stats를 받은 지 2초 넘음,
 *   또는 마지막 stats의 (수신 시각 − ts)가 기준선보다 2초 넘게 큼: STALE
 *   기준선과의 차이만 보므로 GW PC와 HMI PC의 시계 차는 판정에 들어가지 않는다
 */
export function freshness(s: HmiState, now: number): Freshness {
  const se = s.session;
  if (se === null) return "WAITING";
  if (se.lastStats === null || se.baseline === null) {
    return now - se.openedAt > FRESHNESS_LIMIT_MS ? "STALE" : "WAITING";
  }
  if (now - se.lastStats.receivedAt > FRESHNESS_LIMIT_MS) return "STALE";
  if (isLate(se)) return "STALE";
  return "FRESH";
}

/** "늦게 도착"이 RESYNC_AFTER_MS 이상 이어졌으면 true → 호출자가 연결을 다시 맺어 기준선을 새로 잡는다 */
export function needsResync(s: HmiState, now: number): boolean {
  const since = s.session?.lateSince;
  return since !== null && since !== undefined && now - since >= RESYNC_AFTER_MS;
}

/** GW 시각을 HMI 시각으로 (기준선이 없으면 null) */
export function toLocalTime(s: HmiState, gwTs: number): number | null {
  const b = s.session?.baseline;
  return b === null || b === undefined ? null : gwTs + b;
}
