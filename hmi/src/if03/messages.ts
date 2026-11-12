// IF-03 GW → HMI 메시지 (ICD 7.1). GW가 보낸 JSON을 이 타입으로 검증해서만 들여온다 (경계에서 한 번).
//
// 검증에 실패한 메시지는 버리고 센다. 화면 코드는 여기서 나온 타입만 믿는다.

export type Source = "VEHICLE" | "DETECTOR";
export type LinkState = "CONNECTED" | "DEGRADED" | "LOST";
export type Mode = "STANDBY" | "MISSION" | "STOPPED" | "RTB" | "FAULT";

export interface TelemetryData {
  lat: number; // deg
  lon: number; // deg
  heading_deg: number;
  speed_mps: number;
  battery_pct: number;
  mode: Mode;
  flags: string[];
  seq: number;
  rx_ts: number; // GW 수신 시각, epoch ms
}

export interface DetectedObject {
  class: string;
  conf: number; // 0~1
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface DetectionData {
  frame_id: number;
  capture_ts: number;
  img_w: number;
  img_h: number;
  objects: DetectedObject[];
}

export interface StatsData {
  source: Source;
  loss_rate_10s: number | null; // 0~1, 최근 10초에 수신·유실이 없으면 null
  rx_count: number;
  lost_count: number;
  errors: Record<string, number>; // sync, version, length, crc, type, range, order
}

export interface LinkStatusData {
  source: Source;
  state: LinkState;
  last_rx_ts: number | null;
}

interface Envelope<T extends string, D> {
  type: T;
  ts: number; // GW 시각, epoch ms
  data: D;
}

export type If03Message =
  | Envelope<"telemetry", TelemetryData>
  | Envelope<"detection", DetectionData>
  | Envelope<"stats", StatsData>
  | Envelope<"link_status", LinkStatusData>;

// ---- 검증 ----

type Obj = Record<string, unknown>;

const isObj = (v: unknown): v is Obj => typeof v === "object" && v !== null && !Array.isArray(v);
const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
const isStr = (v: unknown): v is string => typeof v === "string";
const isSource = (v: unknown): v is Source => v === "VEHICLE" || v === "DETECTOR";
const isLinkState = (v: unknown): v is LinkState => v === "CONNECTED" || v === "DEGRADED" || v === "LOST";
const MODES: readonly string[] = ["STANDBY", "MISSION", "STOPPED", "RTB", "FAULT"];
const isMode = (v: unknown): v is Mode => isStr(v) && MODES.includes(v);
const isNumOrNull = (v: unknown): v is number | null => v === null || isNum(v);

function telemetry(d: Obj): TelemetryData | null {
  const ok =
    isNum(d.lat) && isNum(d.lon) && isNum(d.heading_deg) && isNum(d.speed_mps) && isNum(d.battery_pct) &&
    isMode(d.mode) && Array.isArray(d.flags) && d.flags.every(isStr) && isNum(d.seq) && isNum(d.rx_ts);
  return ok ? (d as unknown as TelemetryData) : null;
}

function detection(d: Obj): DetectionData | null {
  const objOk = (o: unknown) =>
    isObj(o) && isStr(o.class) && isNum(o.conf) && isNum(o.x) && isNum(o.y) && isNum(o.w) && isNum(o.h);
  const ok =
    isNum(d.frame_id) && isNum(d.capture_ts) && isNum(d.img_w) && isNum(d.img_h) &&
    Array.isArray(d.objects) && d.objects.every(objOk);
  return ok ? (d as unknown as DetectionData) : null;
}

function stats(d: Obj): StatsData | null {
  const ok =
    isSource(d.source) && isNumOrNull(d.loss_rate_10s) && isNum(d.rx_count) && isNum(d.lost_count) &&
    isObj(d.errors) && Object.values(d.errors).every(isNum);
  return ok ? (d as unknown as StatsData) : null;
}

function linkStatus(d: Obj): LinkStatusData | null {
  const ok = isSource(d.source) && isLinkState(d.state) && isNumOrNull(d.last_rx_ts);
  return ok ? (d as unknown as LinkStatusData) : null;
}

/** GW가 보낸 텍스트 한 개를 IF-03 메시지로. 형식이 맞지 않거나 모르는 type이면 null. */
export function parseIf03(text: string): If03Message | null {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return null;
  }
  if (!isObj(raw) || !isStr(raw.type) || !isNum(raw.ts) || !isObj(raw.data)) return null;
  const { type, ts, data } = raw;
  switch (type) {
    case "telemetry": {
      const d = telemetry(data);
      return d && { type, ts, data: d };
    }
    case "detection": {
      const d = detection(data);
      return d && { type, ts, data: d };
    }
    case "stats": {
      const d = stats(data);
      return d && { type, ts, data: d };
    }
    case "link_status": {
      const d = linkStatus(data);
      return d && { type, ts, data: d };
    }
    default:
      return null; // cmd_result 등은 명령 기능(12월)에서 추가
  }
}
