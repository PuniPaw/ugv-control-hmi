// 명령 패널 상태 (REQ-CMD-001·004, ICD 7.2): 운용자 확인 절차와 보낸 명령의 결과. 순수 함수, 시각은 인자.
//
// 확인 절차: draftCommand()로 확인을 받을 명령을 정하고, 운용자가 확인하면 호출자(HmiController)가 보내고
// commandSent()로 기록한다. draft 없이 보낸 기록은 만들 수 없다 (commandSent는 draft를 소비한다).
//
// 보낸 명령은 결과를 기다리거나(PENDING), 결과를 받았거나(DONE), IF-03 연결이 끊겨 결과를 받지 못했다
// (UNCONFIRMED: 결과를 놓쳤을 수 있고, GW가 다시 시작돼 req_id 기억이 사라졌을 수도 있다. ICD 7.2 한계).
// 상태는 메시지와 연결 이벤트로만 바뀌고 시간으로는 바뀌지 않는다. GW가 결과를 낼 때까지의 시간은
// 정해져 있지 않기 때문이다(ICD 7.3 늦은 판정, IF-03 전달 지연). 대기 시간은 화면에 경과 초로만 보인다.
//
// HMI는 명령을 스스로 다시 보내지 않고, 보낸 명령을 다시 보내는 기능도 두지 않는다. 운용자가 다시 시도하려면
// 패널에서 새 명령으로 확인 절차를 거치고(새 req_id, 이슈 #27), 같은 종류의 명령이 결과 대기 중이거나
// 결과 미확인이면 확인 단계가 그 사실을 경고한다(sameKindUnresolved).

import { LAT_LIMIT, LON_LIMIT, type CmdResultData, type CommandSpec } from "../if03/messages";

export const COMMAND_LOG_MAX = 20; // 기억하는 최근 명령 수 (결과를 아직 받지 못한 것은 밀어내지 않는다)

export type CommandStatus =
  | { kind: "PENDING" } // 결과 대기
  | { kind: "UNCONFIRMED" } // 결과 전에 IF-03 연결이 끊김
  | { kind: "DONE"; result: CmdResultData };

export interface SentCommand {
  reqId: string;
  spec: CommandSpec;
  sentAtMono: number; // 단조 시계 ms: 결과 대기 경과 시간 표시
  status: CommandStatus;
}

export interface CommandLog {
  draft: CommandSpec | null; // 운용자 확인을 기다리는 명령
  sent: SentCommand[]; // 최근 것부터
}

export const emptyCommandLog = (): CommandLog => ({ draft: null, sent: [] });

/** 확인 절차 시작: 운용자에게 확인받을 명령을 정한다 (이미 있던 draft는 바꾼다) */
export function draftCommand(log: CommandLog, spec: CommandSpec): CommandLog {
  return { ...log, draft: spec };
}

export function cancelDraft(log: CommandLog): CommandLog {
  return { ...log, draft: null };
}

const unresolved = (c: SentCommand) => c.status.kind !== "DONE";

/** COMMAND_LOG_MAX를 넘으면 결과를 받은 오래된 것부터 지운다. 결과를 아직 받지 못한 것은 늦은 결과를 맞출 수 있게 남긴다 */
function trim(sent: SentCommand[]): SentCommand[] {
  let excess = sent.length - COMMAND_LOG_MAX;
  const out: SentCommand[] = [];
  for (let i = sent.length - 1; i >= 0; i--) {
    const c = sent[i]!;
    if (excess > 0 && !unresolved(c)) excess--;
    else out.unshift(c);
  }
  return out;
}

/** 확인한 draft를 reqId로 보냈다. draft가 없으면 보낸 것이 아니므로 그대로 둔다 */
export function commandSent(log: CommandLog, reqId: string, atMono: number): CommandLog {
  if (log.draft === null) return log;
  const entry: SentCommand = { reqId, spec: log.draft, sentAtMono: atMono, status: { kind: "PENDING" } };
  return { draft: null, sent: trim([entry, ...log.sent]) };
}

/**
 * cmd_result 하나. 결과를 아직 받지 못한(PENDING, UNCONFIRMED) 우리 명령만 바꾼다. 끊긴 뒤 GW가 살아 있으면
 * 새 연결로 결과가 올 수 있다(ICD 7.1: 모든 HMI에 보냄). 다른 HMI의 명령(모르는 req_id)과 이미 받은 결과의
 * 재전달은 무시한다.
 * 확인 단계가 그 명령에 대한 경고(sameKindUnresolved)를 띄우고 있었으면 확인 단계를 닫는다: 운용자가 확인하던
 * 상황이 바뀌었으므로(예: 결과 미확인이던 복귀가 수락됨) 바뀐 결과를 보고 다시 판단하게 한다
 */
export function resultReceived(log: CommandLog, r: CmdResultData): CommandLog {
  const i = log.sent.findIndex((c) => c.reqId === r.req_id && unresolved(c));
  if (i < 0) return log;
  const warned = log.draft !== null && sameKindUnresolved(log, log.draft)?.reqId === r.req_id;
  const sent = log.sent.map((c, k) => (k === i ? { ...c, status: { kind: "DONE" as const, result: r } } : c));
  return { draft: warned ? null : log.draft, sent };
}

/**
 * IF-03 연결 끊김: 결과를 기다리던 명령은 결과 미확인, 확인 단계도 닫는다(끊기기 전 상황에서 연 확인으로
 * 재접속 뒤에 보내지 않도록). 기준선 재설정은 끊김이 아니므로 이것을 부르지 않는다(명령은 결과 대기 그대로)
 */
export function disconnected(log: CommandLog): CommandLog {
  const sent = log.sent.map((c) => (c.status.kind === "PENDING" ? { ...c, status: { kind: "UNCONFIRMED" as const } } : c));
  return { draft: null, sent };
}

/**
 * 확인할 명령과 같은 종류인데 결과를 아직 받지 못한 명령 (확인 단계의 경고). 그 명령은 이미 실행됐거나
 * 실행 중일 수 있으므로 새로 보내면 두 번 실행될 수 있다. STOP은 두 번 실행돼도 정지 상태 그대로라 경고하지 않는다
 */
export function sameKindUnresolved(log: CommandLog, spec: CommandSpec): SentCommand | null {
  if (spec.cmd_type === "STOP") return null;
  return log.sent.find((c) => c.spec.cmd_type === spec.cmd_type && unresolved(c)) ?? null;
}

// 소수 7자리까지: COMMAND는 1e-7 deg 정수로 싣는다(ICD 6.3). 더 긴 입력은 확인 화면과 실제 값이 1 LSB 다를 수 있다
const DECIMAL = /^[+-]?(\d+(\.\d{0,7})?|\.\d{1,7})$/;

/** 운용자가 입력한 경로점 (화면 입력 경계). ICD 7.2 범위(위도 ±90, 경도 ±180) 안의 소수 7자리 이하 십진수만 */
export function waypointFromInput(latText: string, lonText: string): CommandSpec | { error: string } {
  const lat = latText.trim();
  const lon = lonText.trim();
  if (!DECIMAL.test(lat) || !DECIMAL.test(lon)) return { error: "위도·경도를 소수 7자리 이하 십진수(도)로 입력" };
  const la = Number(lat);
  const lo = Number(lon);
  if (Math.abs(la) > LAT_LIMIT) return { error: "위도는 −90 ~ 90" };
  if (Math.abs(lo) > LON_LIMIT) return { error: "경도는 −180 ~ 180" };
  return { cmd_type: "WAYPOINT", lat: la, lon: lo };
}
