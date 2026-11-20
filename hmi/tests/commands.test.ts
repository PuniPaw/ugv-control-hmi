// 명령 패널 상태와 IF-03 command 텍스트 (state/commands.ts, ICD 7.2). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import { commandText, type CmdResultData } from "../src/if03/messages";
import {
  COMMAND_LOG_MAX, cancelDraft, commandSent, connectionEnded, draftCommand, emptyCommandLog, resultReceived, waypointFromInput,
} from "../src/state/commands";

const accepted = (req_id: string): CmdResultData => ({ req_id, cmd_id: 9, result: "ACCEPTED", attempts: 1 });

it("REQ_CMD_001 command text follows ICD 7.2 with coordinates only for WAYPOINT", () => {
  expect(JSON.parse(commandText("a1", { cmd_type: "STOP" }))).toEqual({ type: "command", data: { req_id: "a1", cmd_type: "STOP" } });
  expect(JSON.parse(commandText("a2", { cmd_type: "RTB" }))).toEqual({ type: "command", data: { req_id: "a2", cmd_type: "RTB" } });
  expect(JSON.parse(commandText("a3", { cmd_type: "WAYPOINT", lat: 36.1234567, lon: -127.5 }))).toEqual({
    type: "command", data: { req_id: "a3", cmd_type: "WAYPOINT", lat: 36.1234567, lon: -127.5 },
  });
});

it("REQ_CMD_001 a sent record exists only for a confirmed draft", () => {
  const log = emptyCommandLog();
  expect(commandSent(log, "x", 0)).toBe(log); // 확인할 명령이 없으면 보낸 기록도 없다
  const drafted = draftCommand(log, { cmd_type: "STOP" });
  expect(cancelDraft(drafted)).toEqual(log);
  const sent = commandSent(drafted, "x", 5);
  expect(sent).toEqual({ draft: null, sent: [{ reqId: "x", spec: { cmd_type: "STOP" }, sentAt: 5, status: { kind: "PENDING" } }] });
  let many = sent;
  for (let i = 0; i < COMMAND_LOG_MAX + 3; i++) many = commandSent(draftCommand(many, { cmd_type: "RTB" }), `n${i}`, i);
  expect(many.sent).toHaveLength(COMMAND_LOG_MAX);
  expect(many.sent[0]!.reqId).toBe(`n${COMMAND_LOG_MAX + 2}`); // 최근 것부터
});

it("REQ_CMD_001 results match our req_id once, and a connection end leaves pending commands unconfirmed", () => {
  let log = commandSent(draftCommand(emptyCommandLog(), { cmd_type: "STOP" }), "a", 0);
  log = commandSent(draftCommand(log, { cmd_type: "RTB" }), "b", 1);
  expect(resultReceived(log, accepted("other-hmi"))).toBe(log); // 다른 HMI의 명령 결과
  log = resultReceived(log, accepted("a"));
  const again = resultReceived(log, { req_id: "a", cmd_id: 9, result: "TIMEOUT", attempts: 4 });
  expect(again).toBe(log); // 이미 받은 결과는 바뀌지 않는다
  log = connectionEnded(log);
  expect(log.sent.map((c) => c.status.kind)).toEqual(["UNCONFIRMED", "DONE"]);
  // 새 연결에서 결과가 오면(GW가 살아 있어 아직 판정 전이었음) 결과로 바뀐다
  expect(resultReceived(log, accepted("b")).sent[0]!.status).toEqual({ kind: "DONE", result: accepted("b") });
});

it("REQ_CMD_001 waypoint input accepts decimal degrees within ICD 7.2 ranges only", () => {
  expect(waypointFromInput(" 36.5 ", "-127.25")).toEqual({ cmd_type: "WAYPOINT", lat: 36.5, lon: -127.25 });
  expect(waypointFromInput("90", "-180")).toEqual({ cmd_type: "WAYPOINT", lat: 90, lon: -180 });
  expect(waypointFromInput(".5", "+1.")).toEqual({ cmd_type: "WAYPOINT", lat: 0.5, lon: 1 });
  for (const [la, lo] of [["", "1"], ["1", " "], ["1e1", "1"], ["0x10", "1"], ["NaN", "1"], ["Infinity", "1"], ["1,5", "1"], ["90.0000001", "0"], ["0", "180.5"], ["-91", "0"]]) {
    expect(waypointFromInput(la!, lo!), `${la} ${lo}`).toHaveProperty("error");
  }
});
