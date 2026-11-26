// 경고 체계 (state/alerts.ts). describe 없음: it 제목이 STP의 시험 이름이다.
// 기대값은 SRS 표 3-1(등급)과 표 3-2(수명)의 문구에서 옮겼다 (코드의 KINDS 표를 가져오지 않는다).
import { expect, it } from "vitest";
import type { CmdResultData, If03Message, TelemetryData } from "../src/if03/messages";
import {
  acknowledge, alertConditions, alertRows, emptyAlerts, reconcile, type AlertList, type Condition,
} from "../src/state/alerts";
import { commandSent, connectionEnded, draftCommand, resultReceived } from "../src/state/commands";
import { apply, initialState, type HmiState } from "../src/state/hmiState";
import type { ConnView } from "../src/state/view";

const open: ConnView = { state: "OPEN" };
const telemetry = (mode: TelemetryData["mode"], flags: TelemetryData["flags"], battery_pct = 50): If03Message => ({
  type: "telemetry", ts: 0,
  data: { lat: 36, lon: 127, heading_deg: 0, speed_mps: 0, battery_pct, mode, flags, seq: 0, rx_ts: 0 },
});
const link = (state: "CONNECTED" | "DEGRADED" | "LOST"): If03Message => ({
  type: "link_status", ts: 0, data: { source: "VEHICLE", state, last_rx_ts: 0 },
});
const withMsgs = (...ms: If03Message[]) => ms.reduce((s, m) => apply(s, m, 0), initialState());
/** 조건 종류 → 등급 (화면 행으로 확인) */
const levels = (s: HmiState, conn: ConnView = open, fresh: "FRESH" | "STALE" | "WAITING" = "FRESH") =>
  Object.fromEntries(alertRows(reconcile(emptyAlerts(), alertConditions(s, conn, fresh))).map((r) => [r.kind, r.level]));

const sent = (s: HmiState, reqId: string, spec: Parameters<typeof draftCommand>[1] = { cmd_type: "RTB" }): HmiState => ({
  ...s, commands: commandSent(draftCommand(s.commands, spec), reqId, 0),
});
const result = (s: HmiState, req_id: string, r: CmdResultData["result"]): HmiState => ({
  ...s, commands: resultReceived(s.commands, { req_id, cmd_id: 7, result: r, attempts: r === "TIMEOUT" ? 4 : 1 }),
});

const c = (kind: Condition["kind"], key: string = kind): Condition => ({ key, kind, text: key });
const idByText = (l: AlertList, text: string) => alertRows(l).find((r) => r.text === text)!.id;
const states = (l: AlertList) => alertRows(l).map((r) => [r.kind, r.state]);
const idOf = (l: AlertList, kind: string) => alertRows(l).find((r) => r.kind === kind)!.id;

it("REQ_HMI_004 each table 3-1 condition in scope raises an alert of its level", () => {
  expect(levels(withMsgs(link("LOST")))).toEqual({ VEHICLE_LOST: "WARNING" });
  expect(levels(withMsgs(link("DEGRADED")))).toEqual({ VEHICLE_DEGRADED: "ADVISORY" });
  expect(levels(withMsgs(link("CONNECTED"), telemetry("FAULT", [])))).toEqual({ VEHICLE_FAULT: "WARNING" });
  expect(levels(withMsgs(link("CONNECTED"), telemetry("STOPPED", ["FAILSAFE_ACTIVE"])))).toEqual({ FAILSAFE: "WARNING" });
  expect(levels(withMsgs(link("CONNECTED"), telemetry("MISSION", ["BATTERY_LOW"], 20)))).toEqual({ BATTERY_LOW: "CAUTION" });
  const fine = withMsgs(link("CONNECTED"), telemetry("MISSION", ["GPS_VALID"]));
  expect(levels(fine)).toEqual({});
  // GW 연결: 배너와 같은 판정. 재시도 대기·재시도 접속은 끊김, 기준선 재설정 접속과 STALE은 지연
  expect(levels(fine, { state: "WAITING_RETRY", failures: 1, retryAtMono: 0 })).toEqual({ GW_DISCONNECTED: "WARNING" });
  expect(levels(fine, { state: "CONNECTING", cause: "retry", failures: 2 })).toEqual({ GW_DISCONNECTED: "WARNING" });
  expect(levels(fine, { state: "CONNECTING", cause: "resync" })).toEqual({ GW_DELAY: "CAUTION" });
  expect(levels(fine, open, "STALE")).toEqual({ GW_DELAY: "CAUTION" });
  // 명령 결과: TIMEOUT·REJECTED와 결과 미확인은 명령마다 CAUTION, 수락과 결과 대기는 경고 아님
  let s = sent(sent(sent(sent(fine, "a"), "b"), "c"), "d");
  s = result(result(result(s, "a", "TIMEOUT"), "b", "REJECTED_STATE"), "c", "ACCEPTED");
  const before = alertRows(reconcile(emptyAlerts(), alertConditions(s, open, "FRESH")));
  expect(before.map((r) => r.kind)).toEqual(["CMD_FAILED", "CMD_FAILED"]); // a·b만 (c 수락, d 결과 대기)
  const rows = alertRows(reconcile(emptyAlerts(), alertConditions({ ...s, commands: connectionEnded(s.commands) }, open, "FRESH")));
  expect(rows.map((r) => [r.kind, r.level, r.text])).toEqual([
    ["CMD_UNCONFIRMED", "CAUTION", "명령 결과 미확인: 복귀 (RTB) · 이미 실행됐을 수 있음"],
    ["CMD_FAILED", "CAUTION", "명령 실패: 복귀 (RTB) · 거절 (REJECTED_STATE)"],
    ["CMD_FAILED", "CAUTION", "명령 실패: 복귀 (RTB) · 응답 없음 (TIMEOUT)"],
  ]);
});

it("REQ_HMI_004 no alert comes from values never received or from the first connection attempt", () => {
  const s = initialState(); // link_status·텔레메트리 전
  expect(levels(s, { state: "CONNECTING", cause: "first" }, "WAITING")).toEqual({});
  expect(levels(s, open, "WAITING")).toEqual({});
});

it("REQ_HMI_004 vehicle conditions keep the last received values while the GW is down", () => {
  const s = withMsgs(link("LOST"), telemetry("STOPPED", ["FAILSAFE_ACTIVE", "BATTERY_LOW"], 12));
  expect(levels(s, { state: "WAITING_RETRY", failures: 1, retryAtMono: 0 })).toEqual({
    VEHICLE_LOST: "WARNING", FAILSAFE: "WARNING", GW_DISCONNECTED: "WARNING", BATTERY_LOW: "CAUTION",
  });
});

it("REQ_HMI_003 a warning that clears before acknowledgement stays as cleared until acknowledged", () => {
  let l = reconcile(emptyAlerts(), [c("VEHICLE_LOST")]);
  expect(states(l)).toEqual([["VEHICLE_LOST", "ACTIVE"]]);
  l = reconcile(l, []);
  expect(states(l)).toEqual([["VEHICLE_LOST", "CLEARED"]]);
  expect(alertRows(l)[0]!.ackable).toBe(true);
  l = acknowledge(l, idOf(l, "VEHICLE_LOST"));
  expect(states(l)).toEqual([]);
});

it("REQ_HMI_003 acknowledging while the condition lasts keeps it as acknowledged until the condition clears", () => {
  let l = reconcile(emptyAlerts(), [c("GW_DELAY")]);
  l = acknowledge(l, idOf(l, "GW_DELAY"));
  l = reconcile(l, [c("GW_DELAY")]);
  expect(alertRows(l).map((r) => [r.state, r.ackable])).toEqual([["ACKED", false]]);
  l = reconcile(l, []);
  expect(states(l)).toEqual([]);
});

it("REQ_HMI_003 a condition back while cleared reactivates the same alert, after it is gone it is a new alert", () => {
  let l = reconcile(emptyAlerts(), [c("FAILSAFE")]);
  const first = idOf(l, "FAILSAFE");
  l = reconcile(reconcile(l, []), [c("FAILSAFE")]);
  expect(alertRows(l).map((r) => [r.id, r.state])).toEqual([[first, "ACTIVE"]]);
  l = acknowledge(l, first); // 확인됨
  l = reconcile(reconcile(l, []), [c("FAILSAFE")]); // 사라진 뒤 다시
  expect(alertRows(l).map((r) => r.state)).toEqual(["ACTIVE"]);
  expect(idOf(l, "FAILSAFE")).not.toBe(first);
  expect(reconcile(l, [c("FAILSAFE")])).toBe(l); // 같은 조건이면 그대로 (판정마다 불러도 된다)
});

it("REQ_HMI_003 an advisory needs no acknowledgement and goes away with its condition", () => {
  let l = reconcile(emptyAlerts(), [c("VEHICLE_DEGRADED")]);
  expect(alertRows(l).map((r) => [r.level, r.state, r.ackable])).toEqual([["ADVISORY", "ACTIVE", false]]);
  expect(acknowledge(l, idOf(l, "VEHICLE_DEGRADED"))).toBe(l);
  l = reconcile(l, []);
  expect(states(l)).toEqual([]);
});

it("REQ_HMI_003 command result alerts are one per command, do not clear, and are gone for good once acknowledged", () => {
  const u1 = c("CMD_UNCONFIRMED", "CMD_UNCONFIRMED:r1");
  const u2 = c("CMD_UNCONFIRMED", "CMD_UNCONFIRMED:r2");
  let l = reconcile(emptyAlerts(), [u1, u2]);
  expect(states(l)).toEqual([["CMD_UNCONFIRMED", "ACTIVE"], ["CMD_UNCONFIRMED", "ACTIVE"]]);
  l = reconcile(l, [u2]); // r1의 결과가 늦게 옴: 결과 미확인이었다는 경고는 풀리지 않는다
  expect(states(l)).toEqual([["CMD_UNCONFIRMED", "ACTIVE"], ["CMD_UNCONFIRMED", "ACTIVE"]]);
  l = acknowledge(l, idByText(l, u2.text)); // r2는 계속 결과 미확인
  l = reconcile(reconcile(l, [u2]), [u2]);
  expect(alertRows(l).map((r) => r.text)).toEqual([u1.text]); // 확인한 것은 다시 뜨지 않는다
  l = acknowledge(l, idByText(l, u1.text));
  l = reconcile(l, []);
  expect(states(l)).toEqual([]);
  expect(l.alerts).toEqual([]); // 조건이 사라지면 확인한 기록도 지운다
});

it("REQ_HMI_003 rows are WARNING, then CAUTION, then ADVISORY, newest first within a level", () => {
  let l = reconcile(emptyAlerts(), [c("VEHICLE_DEGRADED"), c("BATTERY_LOW")]);
  l = reconcile(l, [c("VEHICLE_DEGRADED"), c("BATTERY_LOW"), c("GW_DELAY"), c("VEHICLE_LOST"), c("FAILSAFE")]);
  expect(alertRows(l).map((r) => r.kind)).toEqual(["FAILSAFE", "VEHICLE_LOST", "GW_DELAY", "BATTERY_LOW", "VEHICLE_DEGRADED"]);
});
