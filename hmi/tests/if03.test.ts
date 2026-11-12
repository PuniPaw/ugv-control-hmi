// IF-03 경계 파서 (ICD 7.1). describe를 쓰지 않는다: it 제목이 곧 STP의 시험 이름이다.
import { readFileSync } from "node:fs";
import { expect, it } from "vitest";
import { parseIf03 } from "../src/if03/messages";

const gwLines = readFileSync(new URL("./fixtures/gw_if03_sample.jsonl", import.meta.url), "utf8")
  .split("\n")
  .filter((l) => l.trim() !== "");

const telemetry = (patch: Record<string, unknown>) =>
  JSON.stringify({
    type: "telemetry",
    ts: 1,
    data: { lat: 36, lon: 127, heading_deg: 0, speed_mps: 0, battery_pct: 50, mode: "MISSION", flags: ["GPS_VALID"], seq: 0, rx_ts: 1, ...patch },
  });
const ERRORS = { sync: 0, version: 0, length: 0, crc: 0, type: 0, range: 0, order: 0 };
const stats = (patch: Record<string, unknown>) =>
  JSON.stringify({ type: "stats", ts: 1, data: { source: "VEHICLE", loss_rate_10s: null, rx_count: 0, lost_count: 0, errors: ERRORS, ...patch } });

it("REQ_HMI_007 accepts every message type the real GW printed", () => {
  for (const line of gwLines) expect(parseIf03(line), line).not.toBeNull();
  const types = new Set(gwLines.map((l) => parseIf03(l)?.type));
  expect(types).toEqual(new Set(["telemetry", "detection", "stats", "link_status"]));
});

it("REQ_HMI_007 keeps exactly the fields the ICD defines", () => {
  const m = parseIf03(telemetry({ extra: "x" }));
  expect(m?.type === "telemetry" && Object.keys(m.data).sort()).toEqual(
    ["battery_pct", "flags", "heading_deg", "lat", "lon", "mode", "rx_ts", "seq", "speed_mps"],
  );
});

it("REQ_HMI_007 rejects malformed JSON, unknown types and missing fields", () => {
  for (const bad of [
    "not json",
    "[]",
    '{"type":"telemetry","ts":1}',
    '{"type":"cmd_result","ts":1,"data":{}}',
    telemetry({ lat: "36" }),
    stats({ source: "X" }),
    '{"type":"link_status","ts":1,"data":{"source":"VEHICLE","state":"UP","last_rx_ts":null}}',
  ]) {
    expect(parseIf03(bad), bad).toBeNull();
  }
});

it("REQ_HMI_007 rejects values outside the ICD ranges and enums", () => {
  for (const bad of [
    telemetry({ battery_pct: 101 }),
    telemetry({ lat: 91 }),
    telemetry({ heading_deg: -1 }),
    telemetry({ mode: "PATROL" }),
    telemetry({ flags: ["GPS_VALID", "TURBO"] }),
    stats({ loss_rate_10s: 1.5 }),
    stats({ errors: {} }),
    stats({ errors: { ...ERRORS, crc: -1 } }),
    stats({ errors: { ...ERRORS, extra: 0 } }),
  ]) {
    expect(parseIf03(bad), bad).toBeNull();
  }
  expect(parseIf03(stats({ loss_rate_10s: null }))).not.toBeNull();
});
