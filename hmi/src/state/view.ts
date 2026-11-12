// 화면 표시 정책 (순수 함수). 컴포넌트는 이 결과를 그리기만 한다.

import type { Source, StatsData, TelemetryData } from "../if03/messages";
import type { ConnEvent, ConnectionState } from "../net/gwConnection";
import { gpsValid, toLocalTime, type Freshness, type HmiState } from "./hmiState";

export type Level = "ok" | "warn" | "bad";

export interface ConnView {
  state: ConnectionState;
  failures: number; // 마지막 OPEN 이후 연속 실패 횟수 (0이면 첫 접속 시도)
  retryAt: number | null; // WAITING_RETRY일 때 다음 접속 시각 (HMI 시계)
}

/** 연결 이벤트 → 화면용 연결 상태 (now: 이벤트를 받은 HMI 시각) */
export function toConnView(ev: ConnEvent, now: number): ConnView {
  switch (ev.state) {
    case "CONNECTING":
      return { state: ev.state, failures: ev.failures, retryAt: null };
    case "OPEN":
      return { state: ev.state, failures: 0, retryAt: null };
    case "WAITING_RETRY":
      return { state: ev.state, failures: ev.failures, retryAt: now + ev.retryInMs };
  }
}

/** 상단 배너 (ICD 7장, REQ-HMI-006) */
export function banner(conn: ConnView, fresh: Freshness, now: number): { level: Level; text: string } {
  switch (conn.state) {
    case "CONNECTING":
      return conn.failures === 0
        ? { level: "warn", text: "GW 접속 중" }
        : { level: "bad", text: "GW 연결 끊김 · 재접속 중" };
    case "WAITING_RETRY": {
      const sec = conn.retryAt === null ? 0 : Math.max(0, Math.ceil((conn.retryAt - now) / 1000));
      return { level: "bad", text: `GW 연결 끊김 · ${sec}초 후 재접속` };
    }
    case "OPEN":
      if (fresh === "WAITING") return { level: "warn", text: "GW 연결됨 · 데이터 대기 중" };
      if (fresh === "STALE") return { level: "bad", text: "GW 데이터 지연 (2초 이상)" };
      return { level: "ok", text: "GW 연결됨" };
  }
}

/**
 * 마지막 값이 현재 값이 아님을 흐리게 표시할 범위 (REQ-HMI-006).
 * - all: GW 연결이 끊겼거나 데이터가 지연됨 → 패널 전체
 * - vehicle: 위의 경우 또는 차량 링크가 CONNECTED가 아님 → 차량 섹션과 지도 마커
 */
export function staleness(s: HmiState, conn: ConnView, fresh: Freshness): { all: boolean; vehicle: boolean } {
  const all = conn.state !== "OPEN" || fresh !== "FRESH";
  return { all, vehicle: all || s.link.VEHICLE.state !== "CONNECTED" };
}

/** 링크 마지막 수신 후 경과 시간. GW 시각을 기준선(GLOSSARY)으로 HMI 시각으로 바꿔 계산 (시계 차 무관) */
export function linkAgeText(s: HmiState, src: Source, now: number): string {
  const gwTs = s.link[src].lastRxTs;
  if (gwTs === null) return "수신 없음";
  const local = toLocalTime(s, gwTs);
  if (local === null) return "—";
  return `${(Math.max(0, now - local) / 1000).toFixed(1)}s 전`;
}

export function lossText(st: StatsData | null): string {
  return st === null || st.loss_rate_10s === null ? "—" : `${(st.loss_rate_10s * 100).toFixed(1)}%`;
}

/** 배터리 색: SIM이 정한 BATTERY_LOW(20% 이하, ICD 표 6-2)를 그대로 따른다 */
export function batteryLevel(v: TelemetryData): Level {
  return v.flags.includes("BATTERY_LOW") ? "bad" : "ok";
}

export function positionText(v: TelemetryData): string {
  return gpsValid(v) ? `${v.lat.toFixed(6)}, ${v.lon.toFixed(6)}` : "위치 무효 (GPS_VALID 없음)";
}
