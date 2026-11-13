// HMI가 화면에 그리는 상태 (순수 함수, 시각은 인자). 받은 텍스트는 receiveText()로 반영한다.
// 연결 이벤트를 어느 함수로 반영할지는 app/hmiController.ts가 정한다: 열림 connectionOpened(),
// IF-03 연결 끊김 connectionLost(), 기준선 재설정 resyncStarted(). 이 층은 전송 층(net/)을 모른다.

import { parseIf03, type If03Message, type LinkState, type Source, type StatsData, type TelemetryData } from "../if03/messages";

export const TRACK_MAX_POINTS = 3000; // 10Hz 기준 약 5분
// 상한을 넘으면 이만큼 더 지운다. 메모리 상한(도메인)과 함께, 앞쪽 삭제 횟수를 30초(10Hz)에 한 번으로 묶는다
// (앞쪽 삭제는 지도에서 전체 다시 그리기가 된다, map/trackSync.ts)
export const TRACK_TRIM_POINTS = 300;
// 마지막 유효 위치 뒤 이만큼 표본(10Hz 약 1초)이 위치 없이 지나면 궤적을 끊는다. 표본은 텔레메트리 seq로 센다:
// GPS_VALID 없는 표본과 받지 못한 표본(seq가 건너뜀: 유실, 기준선 재설정으로 버린 적체·접속 대기 등)을 같이 센다.
// 한두 표본 깜박임으로 끊으면 점 하나짜리 구간만 남아 궤적이 그려지지 않는다
export const NO_FIX_GAP_SAMPLES = 10;
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
  // 궤적: 끊긴 구간마다 따로 (IF-03 연결 끊김, 차량 LOST, 위치 없이 NO_FIX_GAP_SAMPLES 표본 뒤에는 새 구간). 오래된 것부터.
  // 계약(map/trackSync.ts가 의존): 점이 늘 때는 마지막 구간 배열만 새로 만들고 다른 구간·점 배열은 그대로 둔다
  track: LatLon[][];
  // 마지막 유효 위치의 텔레메트리 seq. 다음 유효 위치와의 seq 차이로 그 사이 위치 없이 지난 표본(GPS 무효 +
  // 받지 못한 것)을 센다. null이면 다음 유효 위치는 새 구간 (처음, IF-03 연결 끊김, 차량 LOST)
  lastFixSeq: number | null;
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
    lastFixSeq: null,
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
  return { ...s, session: null, lastFixSeq: null };
}

/**
 * 기준선 재설정을 위한 재접속이 시작될 때: 연결 범위(session)만 버린다. IF-03 연결 끊김이 아니므로
 * 여기서는 궤적을 끊지 않는다. 재설정 동안 버린 적체·접속 시간만큼 텔레메트리 seq가 건너뛰므로,
 * 그 간격이 NO_FIX_GAP_SAMPLES 이상이면 다음 유효 위치에서 apply()가 끊는다.
 */
export function resyncStarted(s: HmiState): HmiState {
  return { ...s, session: null };
}

/** 마지막 stats가 지금 기준선보다 2초 넘게 늦게 도착했는지 (늦음 판정은 여기 하나) */
function isLate(se: Session): boolean {
  return se.lastStats !== null && se.baseline !== null &&
    se.lastStats.receivedAt - se.lastStats.gwTs - se.baseline > FRESHNESS_LIMIT_MS;
}

function updateSession(se0: Session, m: If03Message, receivedAt: number): Session {
  const delay = receivedAt - m.ts;
  const baseline = se0.baseline === null ? delay : Math.min(se0.baseline, delay);
  if (m.type !== "stats") {
    // stats가 아닌 메시지는 늦음 시작 시각을 정하지 않는다 (ICD 7장: "기준선보다 늦은 첫 stats부터").
    // 기준선이 낮아져 이미 받은 stats가 늦은 것이 되면 freshness는 바로 STALE이지만, 재설정 시계는 다음 stats부터 잰다
    return { ...se0, baseline };
  }
  const se = { ...se0, baseline, lastStats: { receivedAt, gwTs: m.ts } };
  return { ...se, lateSince: isLate(se) ? (se0.lateSince ?? receivedAt) : null };
}

/** 메시지 하나를 반영한 새 상태. receivedAt은 HMI 시계로 받은 시각(ms). */
export function apply(s0: HmiState, m: If03Message, receivedAt: number): HmiState {
  const s = s0.session ? { ...s0, session: updateSession(s0.session, m, receivedAt) } : s0;
  switch (m.type) {
    case "telemetry": {
      const v = m.data;
      if (!gpsValid(v)) return { ...s, vehicle: v }; // 위치·궤적은 그대로 (위치 없는 표본은 seq 차이로 센다)
      // 마지막 유효 위치와의 seq 차이(u32 순환). 차이 d면 그 사이 위치 없이 지난 표본은 d−1개.
      // 차량 LOST 뒤 GW의 seq 기준값 재설정(ICD 9장)이나 SIM 재시작으로 seq가 뒤로 가면 순환해 큰 값이 되어 끊긴다
      const d = s.lastFixSeq === null ? null : (v.seq - s.lastFixSeq) >>> 0;
      if (d === 0) return { ...s, vehicle: v }; // 같은 seq: 같은 위치를 다시 붙이지 않는다
      const open = d !== null && d - 1 < NO_FIX_GAP_SAMPLES;
      return {
        ...s,
        vehicle: v,
        fix: { lat: v.lat, lon: v.lon, heading_deg: v.heading_deg },
        track: appendPoint(s.track, open, [v.lat, v.lon]),
        lastFixSeq: v.seq,
      };
    }
    case "link_status": {
      const link = { ...s.link, [m.data.source]: { state: m.data.state, lastRxTs: m.data.last_rx_ts } };
      const vehicleLost = m.data.source === "VEHICLE" && m.data.state === "LOST";
      return { ...s, link, lastFixSeq: vehicleLost ? null : s.lastFixSeq };
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
