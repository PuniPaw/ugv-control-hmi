// 경고 체계 (REQ-HMI-003·004, SRS 표 3-1 발생 조건, 표 3-2 수명). 순수 함수.
//
// 두 단계로 나눈다.
// 1. alertConditions(state, conn, fresh): 지금 성립한 조건의 집합. 이미 있는 판정(연결 상태, 신선도, 차량 링크,
//    텔레메트리 mode·flags, 명령 상태)을 그대로 입력으로 쓴다. 이력이 없다.
// 2. reconcile(이전 경고 목록, 조건 집합) → 새 목록, acknowledge(목록, id) → 새 목록. 수명 규칙(표 3-2)은 여기 하나에 있다.
//
// 경고 하나의 상태:
//   ACTIVE  조건이 성립하고 운용자가 아직 확인하지 않음
//   CLEARED 확인 전에 조건이 풀림 ("해제됨"으로 남는다)
//   ACKED   조건이 이어지는 중에 확인함 ("확인됨"으로 남는다). 명령 결과 경고는 확인하면 사라지므로 화면에 보이지 않는다
//
// 경고는 조건의 key로 맞춘다: 조건마다 1개(key = 종류), 명령 결과 조건은 명령마다 1개(key = 종류:req_id).
//
// 이번 범위 밖(DET 연결 주에 추가): 탐지 노드 링크 LOST·DEGRADED, 신규 표적 탐지 (SDD 8.7).

import type { Freshness, HmiState } from "./hmiState";
import type { SentCommand } from "./commands";
import { commandLabel, gwHealth, type ConnView } from "./view";

export const ALERT_LEVELS = ["WARNING", "CAUTION", "ADVISORY"] as const; // 화면 순서
export type AlertLevel = (typeof ALERT_LEVELS)[number];

/** 조건 종류 (SRS 표 3-1에서 이번 범위). 등급·문구·수명은 아래 KINDS 표 하나가 정한다 */
export type AlertKind =
  | "VEHICLE_LOST"
  | "VEHICLE_FAULT"
  | "FAILSAFE"
  | "GW_DISCONNECTED"
  | "GW_DELAY"
  | "CMD_FAILED"
  | "CMD_UNCONFIRMED"
  | "BATTERY_LOW"
  | "VEHICLE_DEGRADED";

interface KindSpec {
  level: AlertLevel;
  // 명령 결과 조건: 명령의 결과라 풀리지 않는다 (표 3-2). 조건이 입력에서 사라져도(결과가 늦게 옴, 기록에서 밀림)
  // 경고는 확인할 때까지 그대로다
  latched: boolean;
}

const KINDS: Record<AlertKind, KindSpec> = {
  VEHICLE_LOST: { level: "WARNING", latched: false },
  VEHICLE_FAULT: { level: "WARNING", latched: false },
  FAILSAFE: { level: "WARNING", latched: false },
  GW_DISCONNECTED: { level: "WARNING", latched: false },
  GW_DELAY: { level: "CAUTION", latched: false },
  CMD_FAILED: { level: "CAUTION", latched: true },
  CMD_UNCONFIRMED: { level: "CAUTION", latched: true },
  BATTERY_LOW: { level: "CAUTION", latched: false },
  VEHICLE_DEGRADED: { level: "ADVISORY", latched: false },
};

export interface Condition {
  key: string; // 같은 경고를 맞추는 열쇠
  kind: AlertKind;
  text: string;
}

export type AlertState = "ACTIVE" | "CLEARED" | "ACKED";

export interface Alert {
  id: number; // 화면 확인 버튼이 가리키는 번호. 사라진 뒤 다시 생기면 새 번호(새 경고)
  key: string;
  kind: AlertKind;
  level: AlertLevel;
  text: string;
  state: AlertState;
}

export interface AlertList {
  alerts: Alert[]; // 생긴 순서
  nextId: number;
}

export const emptyAlerts = (): AlertList => ({ alerts: [], nextId: 1 });

const cond = (kind: AlertKind, text: string, key: string = kind): Condition => ({ key, kind, text });

function commandConditions(c: SentCommand): Condition[] {
  const label = commandLabel(c.spec);
  const st = c.status;
  if (st.kind === "UNCONFIRMED") {
    return [cond("CMD_UNCONFIRMED", `명령 결과 미확인: ${label} · 이미 실행됐을 수 있음`, `CMD_UNCONFIRMED:${c.reqId}`)];
  }
  if (st.kind === "DONE" && st.result.result !== "ACCEPTED") {
    const why = st.result.result === "TIMEOUT" ? "응답 없음 (TIMEOUT)" : `거절 (${st.result.result})`;
    return [cond("CMD_FAILED", `명령 실패: ${label} · ${why}`, `CMD_FAILED:${c.reqId}`)];
  }
  return [];
}

/**
 * 지금 성립한 조건 (SRS 표 3-1). GW 연결이 끊겼거나 데이터가 지연된 동안 차량 조건은 마지막으로 받은 값으로
 * 판정한다(표 3-1 아래 문단). 받은 적 없는 값(link_status 전, 텔레메트리 전)으로는 경고하지 않는다.
 * GW 조건은 배너(REQ-HMI-006)와 같은 분류 gwHealth()를 쓴다: 첫 접속 중은 끊김이 아니고, 기준선 재설정 접속은 끊김이 아니라 지연이다
 */
export function alertConditions(s: HmiState, conn: ConnView, fresh: Freshness): Condition[] {
  const out: Condition[] = [];
  const link = s.link.VEHICLE?.state;
  if (link === "LOST") out.push(cond("VEHICLE_LOST", "차량 링크 LOST"));
  if (link === "DEGRADED") out.push(cond("VEHICLE_DEGRADED", "차량 링크 DEGRADED"));
  const v = s.vehicle;
  if (v?.mode === "FAULT") out.push(cond("VEHICLE_FAULT", "차량 모드 FAULT"));
  if (v?.flags.includes("FAILSAFE_ACTIVE")) out.push(cond("FAILSAFE", "차량 Failsafe 정지 (HEARTBEAT 두절)"));
  if (v?.flags.includes("BATTERY_LOW")) out.push(cond("BATTERY_LOW", `배터리 부족 (${v.battery_pct}%)`));
  const gw = gwHealth(conn, fresh); // 배너와 같은 분류
  if (gw === "DISCONNECTED") out.push(cond("GW_DISCONNECTED", "GW 연결 끊김"));
  if (gw === "DELAYED") out.push(cond("GW_DELAY", "GW 데이터 지연 (2초 이상)"));
  // 보낸 순서대로(sent는 최근 것부터): 한 번에 여러 경고가 생겨도 번호가 보낸 순서를 따른다
  for (const c of [...s.commands.sent].reverse()) out.push(...commandConditions(c));
  return out;
}

/**
 * 조건 집합을 반영한 새 경고 목록 (표 3-2). 같은 조건 집합으로 다시 불러도 결과가 같다(멱등): 화면을 판정할 때마다 부른다.
 *
 * | 이전 \ 조건 | 성립 | 풀림 |
 * |---|---|---|
 * | 없음 | 새 경고 ACTIVE | — |
 * | ACTIVE | ACTIVE | CLEARED (명령 결과는 ACTIVE 유지, ADVISORY는 사라짐) |
 * | CLEARED | ACTIVE (같은 경고가 다시 활성) | CLEARED |
 * | ACKED | ACKED | 사라짐 |
 */
export function reconcile(list: AlertList, conditions: readonly Condition[]): AlertList {
  const now = new Map(conditions.map((c) => [c.key, c]));
  let nextId = list.nextId;
  let changed = false;
  const alerts: Alert[] = [];
  for (const a of list.alerts) {
    const c = now.get(a.key);
    now.delete(a.key);
    const next = c ? whilePresent(a, c) : whileAbsent(a);
    if (next !== a) changed = true;
    if (next) alerts.push(next);
  }
  for (const c of now.values()) {
    const { level } = KINDS[c.kind];
    alerts.push({ id: nextId++, key: c.key, kind: c.kind, level, text: c.text, state: "ACTIVE" });
    changed = true;
  }
  return changed ? { alerts, nextId } : list;
}

function whilePresent(a: Alert, c: Condition): Alert {
  const state = a.state === "CLEARED" ? "ACTIVE" : a.state;
  return state === a.state && c.text === a.text ? a : { ...a, state, text: c.text };
}

function whileAbsent(a: Alert): Alert | null {
  const spec = KINDS[a.kind];
  switch (a.state) {
    case "ACTIVE":
      if (spec.latched) return a;
      return spec.level === "ADVISORY" ? null : { ...a, state: "CLEARED" };
    case "CLEARED":
      return a;
    case "ACKED":
      return null;
  }
}

/** 확인 버튼을 둘 경고: WARNING·CAUTION이고 아직 확인하지 않음 (ADVISORY는 확인이 없다) */
const canAcknowledge = (a: Alert): boolean => a.level !== "ADVISORY" && a.state !== "ACKED";

/**
 * 운용자 확인 (WARNING·CAUTION만, ADVISORY는 확인이 없다). 해제됨이면 사라지고, 조건이 이어지는 중이면 확인됨.
 * 명령 결과 경고는 확인됨이 되어 화면에서 사라지고, 그 명령의 조건이 입력에서 사라질 때 목록에서도 지운다
 * (확인한 결과 미확인 명령이 계속 결과 미확인이어도 새 경고로 다시 뜨지 않게)
 */
export function acknowledge(list: AlertList, id: number): AlertList {
  const a = list.alerts.find((x) => x.id === id);
  if (!a || !canAcknowledge(a)) return list;
  const alerts =
    a.state === "CLEARED" ? list.alerts.filter((x) => x !== a) : list.alerts.map((x) => (x === a ? { ...a, state: "ACKED" as const } : x));
  return { ...list, alerts };
}

/** 화면 한 줄 */
export interface AlertRow {
  id: number;
  kind: AlertKind;
  level: AlertLevel;
  state: AlertState;
  text: string;
  ackable: boolean; // 확인 버튼을 보일지
}

/** 화면에 보일 경고: 등급별로(WARNING → CAUTION → ADVISORY), 같은 등급은 최근 것부터. 확인한 명령 결과 경고는 보이지 않는다 */
export function alertRows(list: AlertList): AlertRow[] {
  const visible = list.alerts.filter((a) => !(a.state === "ACKED" && KINDS[a.kind].latched));
  const rank = (a: Alert) => ALERT_LEVELS.indexOf(a.level);
  return visible
    .slice()
    .sort((x, y) => rank(x) - rank(y) || y.id - x.id)
    .map((a) => ({
      id: a.id, kind: a.kind, level: a.level, state: a.state, text: a.text,
      ackable: canAcknowledge(a),
    }));
}
