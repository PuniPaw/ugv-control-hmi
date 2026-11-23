// 명령 패널 상태와 IF-03 command 텍스트 (state/commands.ts, ICD 7.2). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import { commandText, type CmdResultData } from "../src/if03/messages";
import {
  cancelDraft, commandSent, connectionEnded, draftCommand, emptyCommandLog, resultReceived, sameKindUnresolved,
  waypointFromInput, type CommandLog,
} from "../src/state/commands";

const accepted = (req_id: string): CmdResultData => ({ req_id, cmd_id: 9, result: "ACCEPTED", attempts: 1 });
const send = (log: CommandLog, reqId: string, cmd_type: "STOP" | "RTB" = "RTB") => commandSent(draftCommand(log, { cmd_type }), reqId, 0);
const statuses = (log: CommandLog) => log.sent.map((c) => [c.reqId, c.status.kind]);

it("REQ_CMD_001 command text follows ICD 7.2 with coordinates only for WAYPOINT", () => {
  expect(JSON.parse(commandText("a1", { cmd_type: "STOP" }))).toEqual({ type: "command", data: { req_id: "a1", cmd_type: "STOP" } });
  expect(JSON.parse(commandText("a2", { cmd_type: "RTB" }))).toEqual({ type: "command", data: { req_id: "a2", cmd_type: "RTB" } });
  expect(JSON.parse(commandText("a3", { cmd_type: "WAYPOINT", lat: 36.1234567, lon: -127.5 }))).toEqual({
    type: "command", data: { req_id: "a3", cmd_type: "WAYPOINT", lat: 36.1234567, lon: -127.5 },
  });
});

it("REQ_CMD_001 a sent record exists only for a confirmed draft and commands without a result are never pushed out", () => {
  const log = emptyCommandLog();
  expect(commandSent(log, "x", 0)).toBe(log); // 확인할 명령이 없으면 보낸 기록도 없다
  expect(commandSent(cancelDraft(draftCommand(log, { cmd_type: "STOP" })), "x", 0)).toEqual(log);
  // 결과 전에 25개를 보내고 끊겨도(결과 미확인) 첫 명령의 늦은 결과를 받을 수 있다
  let many = log;
  for (let i = 1; i <= 25; i++) many = send(many, `n${i}`);
  many = connectionEnded(many);
  expect(many.sent).toHaveLength(25);
  many = resultReceived(many, accepted("n1"));
  expect(many.sent.at(-1)).toMatchObject({ reqId: "n1", status: { kind: "DONE" } });
  // 결과를 받은 것부터 지워져 다시 20개 이하로
  for (let i = 2; i <= 25; i++) many = resultReceived(many, accepted(`n${i}`));
  many = send(many, "last");
  expect(many.sent).toHaveLength(20);
  expect(many.sent[0]!.reqId).toBe("last"); // 최근 것부터
});

it("REQ_CMD_001 results match our req_id once, a disconnect leaves pending commands unconfirmed and closes the confirmation", () => {
  let log = send(send(emptyCommandLog(), "a"), "b");
  expect(resultReceived(log, accepted("other-hmi"))).toBe(log); // 다른 HMI의 명령 결과
  log = resultReceived(log, accepted("a"));
  expect(resultReceived(log, { req_id: "a", cmd_id: 9, result: "TIMEOUT", attempts: 4 })).toBe(log); // 이미 받은 결과
  log = connectionEnded(draftCommand(log, { cmd_type: "STOP" }));
  expect(statuses(log)).toEqual([["b", "UNCONFIRMED"], ["a", "DONE"]]);
  expect(log.draft).toBeNull(); // 끊기기 전에 연 확인으로 재접속 뒤에 보내지 않는다
  // 새 연결에서 결과가 오면(GW가 살아 있어 아직 판정 전이었음) 결과로 바뀐다
  expect(statuses(resultReceived(log, accepted("b")))).toEqual([["b", "DONE"], ["a", "DONE"]]);
});

it("REQ_CMD_001 confirmation warns about an unresolved command of the same kind except STOP", () => {
  let log = send(send(emptyCommandLog(), "s1", "STOP"), "r1");
  expect(sameKindUnresolved(log, { cmd_type: "RTB" })?.reqId).toBe("r1"); // 결과 대기 중
  expect(sameKindUnresolved(log, { cmd_type: "WAYPOINT", lat: 1, lon: 2 })).toBeNull(); // 다른 종류
  expect(sameKindUnresolved(log, { cmd_type: "STOP" })).toBeNull(); // 정지는 두 번 실행돼도 정지
  log = connectionEnded(log);
  expect(sameKindUnresolved(log, { cmd_type: "RTB" })?.reqId).toBe("r1"); // 결과 미확인
  log = resultReceived(log, accepted("r1"));
  expect(sameKindUnresolved(log, { cmd_type: "RTB" })).toBeNull(); // 결과를 받으면 경고 없음
  // 가장 최근 같은 종류만 본다: 결과가 영영 오지 않는 오래된 r2 뒤에 r3가 결과를 받으면 경고 없음
  log = connectionEnded(send(log, "r2"));
  expect(sameKindUnresolved(log, { cmd_type: "RTB" })?.reqId).toBe("r2");
  log = resultReceived(send(log, "r3"), accepted("r3"));
  expect(sameKindUnresolved(log, { cmd_type: "RTB" })).toBeNull();
  // 숨기는 것은 오래된 결과 미확인뿐: 오래된 명령이 아직 결과 대기(GW 재전송 중)면 더 새 명령이 결과를 받아도 경고
  let pending = send(emptyCommandLog(), "p1");
  pending = resultReceived(send(pending, "p2"), accepted("p2"));
  expect(sameKindUnresolved(pending, { cmd_type: "RTB" })?.reqId).toBe("p1");
});

it("REQ_CMD_001 waypoint input accepts decimal degrees within ICD 7.2 ranges only", () => {
  expect(waypointFromInput(" 36.5 ", "-127.25")).toEqual({ cmd_type: "WAYPOINT", lat: 36.5, lon: -127.25 });
  expect(waypointFromInput("90", "-180")).toEqual({ cmd_type: "WAYPOINT", lat: 90, lon: -180 });
  expect(waypointFromInput(".5", "+1.")).toEqual({ cmd_type: "WAYPOINT", lat: 0.5, lon: 1 });
  expect(waypointFromInput("36.0000001", "-0.0000001")).toEqual({ cmd_type: "WAYPOINT", lat: 36.0000001, lon: -0.0000001 });
  for (const [la, lo] of [
    ["", "1"], ["1", " "], ["1e1", "1"], ["0x10", "1"], ["NaN", "1"], ["Infinity", "1"], ["1,5", "1"],
    ["90.0000001", "0"], ["0", "180.5"], ["-91", "0"],
    ["36.00000015", "0"], // 소수 8자리: 확인 화면(7자리)과 GW가 싣는 1e-7 정수가 다를 수 있다
  ]) {
    expect(waypointFromInput(la!, lo!), `${la} ${lo}`).toHaveProperty("error");
  }
});
