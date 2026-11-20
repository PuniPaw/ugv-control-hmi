// 명령 패널 상태 (REQ-CMD-001·004, ICD 7.2): 운용자 확인 절차와 보낸 명령의 결과. 순수 함수, 시각은 인자.
//
// 확인 절차: draftCommand()로 확인을 받을 명령을 정하고, 운용자가 확인하면 호출자(HmiController)가 보내고
// commandSent()로 기록한다. draft 없이 보낸 기록은 만들 수 없다 (commandSent는 draft를 소비한다).
//
// 보낸 명령은 결과를 기다리거나(PENDING), 결과를 받았거나(DONE), 결과를 받지 못했다(UNCONFIRMED).
// GW는 첫 송신 4초 뒤에는 TIMEOUT으로라도 결과를 낸다(ICD 7.3). 결과를 받지 못하는 경우는 둘이다.
// - IF-03 연결 끊김: 결과를 놓쳤을 수 있고, GW가 다시 시작돼 req_id 기억이 사라졌을 수도 있다(ICD 7.2 한계).
//   곧바로 UNCONFIRMED.
// - 기준선 재설정 재접속 사이에 GW가 결과를 보냄: GW는 살아 있고 명령은 진행 중이므로 상태를 바꾸지 않는다.
//   결과 기한(RESULT_DEADLINE_MS)이 지나도록 결과가 없을 때만 UNCONFIRMED (resultDeadlinePassed).
// HMI는 어떤 경우에도 스스로 다시 보내지 않는다. 다시 보내려면 운용자가 같은 내용을 다시 확인하고,
// 그때는 새 req_id로 보낸다(이슈 #27). 결과 미확인 명령을 다시 보낼 때는 두 번 실행될 수 있음을 알린다.

import type { CmdResultData, CommandSpec } from "../if03/messages";

export const COMMAND_LOG_MAX = 20; // 기억하는 최근 명령 수 (결과 대기 중인 것은 밀어내지 않는다)
// 첫 송신부터 결과를 기다리는 시간 (단조 시계). GW TIMEOUT 4.0초(ICD 7.3) + 판정 주기·전달 여유 1.0초
export const RESULT_DEADLINE_MS = 5000;
export const LAT_LIMIT = 90; // ICD 7.2 (if03/messages.ts의 cmd_result·telemetry 검사와 같은 범위)
export const LON_LIMIT = 180;

export type CommandStatus =
  | { kind: "PENDING" } // 결과 대기 (첫 송신 후 RESULT_DEADLINE_MS까지)
  | { kind: "UNCONFIRMED" } // 결과를 받지 못함 (연결 끊김, 또는 결과 기한이 지남)
  | { kind: "DONE"; result: CmdResultData };

export interface SentCommand {
  reqId: string;
  spec: CommandSpec;
  sentAtMono: number; // 단조 시계 ms: 결과 기한 판정 (기준선 재설정의 원인인 벽시계 계단 변화와 무관)
  status: CommandStatus;
  retried: boolean; // 운용자가 이 명령을 다시 보냈음 (다시 보내기를 더 보이지 않는다)
}

/** 확인을 기다리는 명령. retryOf: 다시 보내기로 연 확인이면 원래 명령의 req_id */
export interface Draft {
  spec: CommandSpec;
  retryOf: string | null;
}

export interface CommandLog {
  draft: Draft | null;
  sent: SentCommand[]; // 최근 것부터
}

export const emptyCommandLog = (): CommandLog => ({ draft: null, sent: [] });

/**
 * 운용자가 다시 보낼 수 있는 명령: 결과 미확인, 응답 없음(TIMEOUT), 상태 거절(REJECTED_STATE)이고 아직 다시 보내지 않음.
 * 결과 대기 중(PENDING)인 명령은 GW가 아직 처리 중이므로 다시 보내지 않는다
 */
export function canRetry(c: SentCommand): boolean {
  if (c.retried) return false;
  const st = c.status;
  return st.kind === "UNCONFIRMED" || (st.kind === "DONE" && (st.result.result === "TIMEOUT" || st.result.result === "REJECTED_STATE"));
}

/** 확인 절차 시작: 운용자에게 확인받을 명령을 정한다 (이미 있던 draft는 바꾼다) */
export function draftCommand(log: CommandLog, spec: CommandSpec, retryOf: string | null = null): CommandLog {
  return { ...log, draft: { spec, retryOf } };
}

export function cancelDraft(log: CommandLog): CommandLog {
  return { ...log, draft: null };
}

/** COMMAND_LOG_MAX를 넘으면 오래된 것부터 지우되, 결과 대기 중인 것은 남긴다 (결과가 와도 맞출 곳이 없어지므로) */
function trim(sent: SentCommand[]): SentCommand[] {
  let excess = sent.length - COMMAND_LOG_MAX;
  if (excess <= 0) return sent;
  const out: SentCommand[] = [];
  for (let i = sent.length - 1; i >= 0; i--) {
    const c = sent[i]!;
    if (excess > 0 && c.status.kind !== "PENDING") excess--;
    else out.unshift(c);
  }
  return out;
}

/** 확인한 draft를 reqId로 보냈다. draft가 없으면 보낸 것이 아니므로 그대로 둔다 */
export function commandSent(log: CommandLog, reqId: string, atMono: number): CommandLog {
  const d = log.draft;
  if (d === null) return log;
  const entry: SentCommand = { reqId, spec: d.spec, sentAtMono: atMono, status: { kind: "PENDING" }, retried: false };
  const rest = log.sent.map((c) => (c.reqId === d.retryOf ? { ...c, retried: true } : c));
  return { draft: null, sent: trim([entry, ...rest]) };
}

/**
 * cmd_result 하나. 결과를 기다리던(PENDING, 또는 결과를 받지 못한 UNCONFIRMED) 우리 명령만 바꾼다.
 * 다른 HMI의 명령(모르는 req_id)과 이미 받은 결과의 재전달은 무시한다
 */
export function resultReceived(log: CommandLog, r: CmdResultData): CommandLog {
  const i = log.sent.findIndex((c) => c.reqId === r.req_id && c.status.kind !== "DONE");
  if (i < 0) return log;
  const sent = log.sent.map((c, k) => (k === i ? { ...c, status: { kind: "DONE" as const, result: r } } : c));
  return { ...log, sent };
}

function markUnconfirmed(sent: SentCommand[], expired: (c: SentCommand) => boolean): SentCommand[] {
  if (!sent.some((c) => c.status.kind === "PENDING" && expired(c))) return sent;
  return sent.map((c) => (c.status.kind === "PENDING" && expired(c) ? { ...c, status: { kind: "UNCONFIRMED" as const } } : c));
}

/**
 * IF-03 연결 끊김: 결과를 기다리던 명령은 결과 미확인, 확인 단계도 닫는다(끊기기 전 상황에서 연 확인으로
 * 재접속 뒤에 보내지 않도록). 다시 보내지 않는다. 기준선 재설정은 끊김이 아니므로 이것을 부르지 않는다
 */
export function connectionLost(log: CommandLog): CommandLog {
  return { draft: null, sent: markUnconfirmed(log.sent, () => true) };
}

/** 결과 기한이 지나도록 결과가 없는 명령은 결과 미확인 (주기 판정에서 부른다) */
export function resultDeadlinePassed(log: CommandLog, nowMono: number): CommandLog {
  const sent = markUnconfirmed(log.sent, (c) => nowMono - c.sentAtMono >= RESULT_DEADLINE_MS);
  return sent === log.sent ? log : { ...log, sent };
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
