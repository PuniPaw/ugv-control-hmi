// 화면 표시 정책 (순수 함수). 컴포넌트는 이 결과를 그리기만 한다.

import { SOURCES, type LinkState, type Mode, type Source, type StatsData, type TelemetryData } from "../if03/messages";
import type { ConnectAttempt, ConnectCause } from "../model/connection";
import { freshness, gpsValid, toLocalTime, type Freshness, type HmiState } from "./hmiState";

export type Level = "ok" | "warn" | "bad";
export interface Banner {
  level: Level;
  text: string;
}

/**
 * 화면용 연결 상태. 재접속 대기는 남은 시간 대신 예정 시각을 가진다. 예정 시각은 **단조 시계**(벽시계가
 * 바뀌어도 흔들리지 않음) 기준이다: 실제 재시도 타이머도 경과 시간으로 동작하기 때문이다.
 */
export type ConnView =
  | ({ state: "CONNECTING" } & ConnectAttempt)
  | { state: "OPEN" }
  | { state: "WAITING_RETRY"; failures: number; retryAtMono: number };

export interface Clocks {
  wall: number; // HMI 벽시계 ms (GW ts·기준선과 비교)
  mono: number; // 단조 시계 ms (재접속 카운트다운)
}

function connectingBanner(cause: ConnectCause): Banner {
  switch (cause) {
    case "first":
      return { level: "warn", text: "GW 접속 중" };
    case "retry":
      return { level: "bad", text: "GW 연결 끊김 · 재접속 중" };
    case "resync":
      // 기준선 재설정은 IF-03 연결 끊김이 아니다 (GLOSSARY): 데이터 지연의 회복 과정으로 표시
      return { level: "warn", text: "GW 데이터 지연 · 기준선 재설정 중" };
  }
}

/** 상단 배너 (ICD 7장, REQ-HMI-006) */
export function banner(conn: ConnView, fresh: Freshness, monoNow: number): Banner {
  switch (conn.state) {
    case "CONNECTING":
      return connectingBanner(conn.cause);
    case "WAITING_RETRY": {
      const sec = Math.max(0, Math.ceil((conn.retryAtMono - monoNow) / 1000));
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

/** 상태 패널 한 줄: 송신원 링크 */
export interface LinkRow {
  source: Source;
  state: LinkState;
  loss: string;
  age: string;
}

/** 상태 패널: 차량 값 (텔레메트리 수신 전이면 null) */
export interface VehicleRows {
  mode: Mode;
  battery: string;
  batteryLevel: Level;
  speed: string;
  heading: string;
  position: string;
  flags: string;
}

export interface Presented {
  fresh: Freshness;
  banner: Banner;
  stale: { all: boolean; vehicle: boolean };
  fixValid: boolean;
  links: LinkRow[];
  vehicle: VehicleRows | null;
  discarded: number; // 폐기한 IF-03 메시지 수 (0이면 표시 안 함)
}

/** 화면 판정을 한 번에, 같은 시각으로. 컴포넌트는 이 결과를 그리기만 한다 (App.vue와 시험이 같은 함수를 쓴다) */
export function present(s: HmiState, conn: ConnView, clocks: Clocks): Presented {
  const fresh = freshness(s, clocks.wall);
  const v = s.vehicle;
  return {
    fresh,
    banner: banner(conn, fresh, clocks.mono),
    stale: staleness(s, conn, fresh),
    fixValid: v !== null && gpsValid(v),
    links: SOURCES.map((source) => ({
      source,
      state: s.link[source].state,
      loss: lossText(s.stats[source]),
      age: linkAgeText(s, source, clocks.wall),
    })),
    vehicle: v && {
      mode: v.mode,
      battery: `${v.battery_pct}%`,
      batteryLevel: batteryLevel(v),
      speed: `${v.speed_mps.toFixed(1)} m/s`,
      heading: `${v.heading_deg.toFixed(1)}°`,
      position: positionText(v),
      flags: v.flags.join(" · ") || "—",
    },
    discarded: s.discarded,
  };
}
