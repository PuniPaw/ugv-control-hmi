// IF-03 GW → HMI 메시지 (ICD 7.1). GW가 보낸 JSON은 이 파일에서만 검증한다 (경계에서 한 번).
//
// 열거값은 아래 `as const` 배열 하나에서 타입과 검사를 함께 만든다. 결과 객체는 검사한 필드만으로
// 새로 조립한다 (캐스트 없음 → 검사한 목록과 타입이 어긋나면 컴파일 오류).
// 형식·범위가 ICD와 다른 메시지는 통째로 버린다(호출자가 센다). GW는 검증 규칙 ⑧로 범위 밖 값을
// 이미 걸러 내므로, 여기서 걸리는 것은 GW·HMI 버전 불일치 같은 결함이다.

export const SOURCES = ["VEHICLE", "DETECTOR"] as const;
export const LINK_STATES = ["CONNECTED", "DEGRADED", "LOST"] as const;
export const MODES = ["STANDBY", "MISSION", "STOPPED", "RTB", "FAULT"] as const; // ICD 표 6-1
export const FLAGS = ["GPS_VALID", "CAMERA_OK", "BATTERY_LOW", "FAILSAFE_ACTIVE"] as const; // ICD 표 6-2
export const ERROR_KEYS = ["sync", "version", "length", "crc", "type", "range", "order"] as const; // ICD 7.1

export type Source = (typeof SOURCES)[number];
export type LinkState = (typeof LINK_STATES)[number];
export type Mode = (typeof MODES)[number];
export type Flag = (typeof FLAGS)[number];
export type ErrorKey = (typeof ERROR_KEYS)[number];

export interface TelemetryData {
  lat: number; // deg (GPS_VALID가 없으면 의미 없음)
  lon: number; // deg
  heading_deg: number;
  speed_mps: number;
  battery_pct: number;
  mode: Mode;
  flags: Flag[];
  seq: number;
  rx_ts: number; // GW 수신 시각, GW 시계 epoch ms
}

export interface DetectedObject {
  class: string; // TBD-02
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
  errors: Record<ErrorKey, number>;
}

export interface LinkStatusData {
  source: Source;
  state: LinkState;
  last_rx_ts: number | null; // GW 시계 epoch ms
}

interface Envelope<T extends string, D> {
  type: T;
  ts: number; // GW 시계 epoch ms
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
const inRange = (v: unknown, lo: number, hi: number): v is number => isNum(v) && v >= lo && v <= hi;
const isCount = (v: unknown): v is number => Number.isSafeInteger(v) && (v as number) >= 0;
const oneOf =
  <T extends string>(values: readonly T[]) =>
  (v: unknown): v is T =>
    isStr(v) && (values as readonly string[]).includes(v);

const isSource = oneOf(SOURCES);
const isLinkState = oneOf(LINK_STATES);
const isMode = oneOf(MODES);
const isFlag = oneOf(FLAGS);

function telemetry(d: Obj): TelemetryData | null {
  const { lat, lon, heading_deg, speed_mps, battery_pct, mode, flags, seq, rx_ts } = d;
  if (
    inRange(lat, -90, 90) && inRange(lon, -180, 180) && inRange(heading_deg, 0, 360) &&
    inRange(speed_mps, 0, Infinity) && inRange(battery_pct, 0, 100) && isMode(mode) &&
    Array.isArray(flags) && flags.every(isFlag) && isCount(seq) && isCount(rx_ts)
  ) {
    return { lat, lon, heading_deg, speed_mps, battery_pct, mode, flags: [...flags], seq, rx_ts };
  }
  return null;
}

function detectedObject(o: unknown): DetectedObject | null {
  if (!isObj(o)) return null;
  const { conf, x, y, w, h } = o;
  if (isStr(o.class) && inRange(conf, 0, 1) && isCount(x) && isCount(y) && isCount(w) && isCount(h)) {
    return { class: o.class, conf, x, y, w, h };
  }
  return null;
}

function detection(d: Obj): DetectionData | null {
  const { frame_id, capture_ts, img_w, img_h, objects } = d;
  if (!(isCount(frame_id) && isCount(capture_ts) && isCount(img_w) && isCount(img_h) && Array.isArray(objects))) {
    return null;
  }
  const parsed: DetectedObject[] = [];
  for (const o of objects) {
    const p = detectedObject(o);
    if (!p) return null;
    parsed.push(p);
  }
  return { frame_id, capture_ts, img_w, img_h, objects: parsed };
}

function errors(v: unknown): Record<ErrorKey, number> | null {
  if (!isObj(v) || Object.keys(v).length !== ERROR_KEYS.length) return null;
  const out = {} as Record<ErrorKey, number>;
  for (const k of ERROR_KEYS) {
    const n = v[k];
    if (!isCount(n)) return null;
    out[k] = n;
  }
  return out;
}

function stats(d: Obj): StatsData | null {
  const { source, loss_rate_10s, rx_count, lost_count } = d;
  const errs = errors(d.errors);
  if (
    isSource(source) && (loss_rate_10s === null || inRange(loss_rate_10s, 0, 1)) &&
    isCount(rx_count) && isCount(lost_count) && errs
  ) {
    return { source, loss_rate_10s, rx_count, lost_count, errors: errs };
  }
  return null;
}

function linkStatus(d: Obj): LinkStatusData | null {
  const { source, state, last_rx_ts } = d;
  if (isSource(source) && isLinkState(state) && (last_rx_ts === null || isCount(last_rx_ts))) {
    return { source, state, last_rx_ts };
  }
  return null;
}

/** GW가 보낸 텍스트 한 개를 IF-03 메시지로. 형식·범위가 맞지 않거나 모르는 type이면 null. */
export function parseIf03(text: string): If03Message | null {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return null;
  }
  if (!isObj(raw) || !isCount(raw.ts) || !isObj(raw.data)) return null;
  const { ts, data } = raw;
  switch (raw.type) {
    case "telemetry": {
      const d = telemetry(data);
      return d && { type: "telemetry", ts, data: d };
    }
    case "detection": {
      const d = detection(data);
      return d && { type: "detection", ts, data: d };
    }
    case "stats": {
      const d = stats(data);
      return d && { type: "stats", ts, data: d };
    }
    case "link_status": {
      const d = linkStatus(data);
      return d && { type: "link_status", ts, data: d };
    }
    default:
      return null; // cmd_result 등은 명령 기능(12월)에서 추가
  }
}
