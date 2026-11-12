import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { parseIf03 } from "../src/if03/messages";

const gwLines = readFileSync(new URL("./fixtures/gw_if03_sample.jsonl", import.meta.url), "utf8")
  .split("\n")
  .filter((l) => l.trim() !== "");

describe("IF-03 parser (ICD 7.1)", () => {
  it("REQ_COM_008 accepts every message the real GW printed", () => {
    expect(gwLines.length).toBeGreaterThan(5);
    for (const line of gwLines) expect(parseIf03(line), line).not.toBeNull();
    const types = new Set(gwLines.map((l) => parseIf03(l)?.type));
    expect(types).toEqual(new Set(["telemetry", "stats", "link_status"]));
  });

  it("REQ_COM_008 rejects malformed or unknown messages", () => {
    const bad = [
      "not json",
      "[]",
      '{"type":"telemetry","ts":1}', // data 없음
      '{"type":"telemetry","ts":1,"data":{"lat":"36"}}', // 형식이 다른 필드
      '{"type":"link_status","ts":1,"data":{"source":"VEHICLE","state":"UP","last_rx_ts":null}}', // 정의 밖 상태
      '{"type":"stats","ts":1,"data":{"source":"X","loss_rate_10s":null,"rx_count":0,"lost_count":0,"errors":{}}}',
      '{"type":"cmd_result","ts":1,"data":{}}', // 아직 다루지 않는 type
    ];
    for (const b of bad) expect(parseIf03(b), b).toBeNull();
  });

  it("keeps loss_rate_10s null (no data in window)", () => {
    const m = parseIf03(
      '{"type":"stats","ts":5,"data":{"source":"DETECTOR","loss_rate_10s":null,"rx_count":0,"lost_count":0,"errors":{"crc":0}}}',
    );
    expect(m?.type === "stats" && m.data.loss_rate_10s).toBeNull();
  });
});
