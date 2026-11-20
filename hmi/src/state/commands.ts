// 명령 패널 상태 (REQ-CMD-001·004, ICD 7.2): 운용자 확인 절차와 보낸 명령의 결과. 순수 함수, 시각은 인자.
//
// 확인 절차: draftCommand()로 확인을 받을 명령을 정하고, 운용자가 확인하면 호출자(HmiController)가 보내고
// commandSent()로 기록한다. draft 없이 보낸 기록은 만들 수 없다 (commandSent는 draft를 소비한다).
//
// 보낸 명령은 이 연결에서 결과를 기다리거나(PENDING), 결과를 받았거나(DONE), 결과를 받기 전에 연결이 끝났다
// (UNCONFIRMED). GW는 연결된 HMI에 빈틈 없이 보내므로(ICD 7장) 같은 연결에서는 결과가 반드시 온다.
// 연결이 끝나면 결과를 놓쳤을 수 있고 GW가 다시 시작돼 req_id 기억이 사라졌을 수도 있다(ICD 7.2 한계).
// 그래서 HMI는 어떤 경우에도 스스로 다시 보내지 않는다. 다시 보내려면 운용자가 같은 내용을 다시 확인하고,
// 그때는 새 req_id로 보낸다(이슈 #27).

import type { CmdResultData, CommandSpec } from "../if03/messages";

export const COMMAND_LOG_MAX = 20; // 기억하는 최근 명령 수 (화면은 그중 일부를 보인다)
const LAT_LIMIT = 90; // ICD 7.2
const LON_LIMIT = 180;

export type CommandStatus =
  | { kind: "PENDING" } // 이 연결에서 보냄, 결과 대기
  | { kind: "UNCONFIRMED" } // 결과를 받기 전에 IF-03 연결이 끝남
  | { kind: "DONE"; result: CmdResultData };

export interface SentCommand {
  reqId: string;
  spec: CommandSpec;
  sentAt: number; // HMI 벽시계 ms
  status: CommandStatus;
}

export interface CommandLog {
  draft: CommandSpec | null; // 운용자 확인을 기다리는 명령
  sent: SentCommand[]; // 최근 것부터, COMMAND_LOG_MAX개까지
}

export const emptyCommandLog = (): CommandLog => ({ draft: null, sent: [] });

/** 확인 절차 시작: 운용자에게 확인받을 명령을 정한다 (이미 있던 draft는 바꾼다) */
export function draftCommand(log: CommandLog, spec: CommandSpec): CommandLog {
  return { ...log, draft: spec };
}

export function cancelDraft(log: CommandLog): CommandLog {
  return { ...log, draft: null };
}

/** 확인한 draft를 reqId로 보냈다. draft가 없으면 보낸 것이 아니므로 그대로 둔다 */
export function commandSent(log: CommandLog, reqId: string, at: number): CommandLog {
  if (log.draft === null) return log;
  const entry: SentCommand = { reqId, spec: log.draft, sentAt: at, status: { kind: "PENDING" } };
  return { draft: null, sent: [entry, ...log.sent].slice(0, COMMAND_LOG_MAX) };
}

/**
 * cmd_result 하나. 결과를 기다리던(PENDING, 또는 연결이 바뀌기 전에 보낸 UNCONFIRMED) 우리 명령만 바꾼다.
 * 다른 HMI의 명령(모르는 req_id)과 이미 받은 결과의 재전달은 무시한다
 */
export function resultReceived(log: CommandLog, r: CmdResultData): CommandLog {
  const i = log.sent.findIndex((c) => c.reqId === r.req_id && c.status.kind !== "DONE");
  if (i < 0) return log;
  const sent = log.sent.map((c, k) => (k === i ? { ...c, status: { kind: "DONE" as const, result: r } } : c));
  return { ...log, sent };
}

/** IF-03 연결이 끝남(끊김, 기준선 재설정): 결과를 기다리던 명령은 결과 미확인. 다시 보내지 않는다 */
export function connectionEnded(log: CommandLog): CommandLog {
  if (!log.sent.some((c) => c.status.kind === "PENDING")) return log;
  const sent = log.sent.map((c) => (c.status.kind === "PENDING" ? { ...c, status: { kind: "UNCONFIRMED" as const } } : c));
  return { ...log, sent };
}

const DECIMAL = /^[+-]?(\d+(\.\d*)?|\.\d+)$/;

/** 운용자가 입력한 경로점 (화면 입력 경계). ICD 7.2 범위(위도 ±90, 경도 ±180) 안의 십진수만 */
export function waypointFromInput(latText: string, lonText: string): CommandSpec | { error: string } {
  const lat = latText.trim();
  const lon = lonText.trim();
  if (!DECIMAL.test(lat) || !DECIMAL.test(lon)) return { error: "위도·경도를 십진수(도)로 입력" };
  const la = Number(lat);
  const lo = Number(lon);
  if (Math.abs(la) > LAT_LIMIT) return { error: "위도는 −90 ~ 90" };
  if (Math.abs(lo) > LON_LIMIT) return { error: "경도는 −180 ~ 180" };
  return { cmd_type: "WAYPOINT", lat: la, lon: lo };
}
