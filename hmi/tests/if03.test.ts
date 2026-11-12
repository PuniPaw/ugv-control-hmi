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
const obj = (patch: Record<string, unknown>) => ({ class: "PERSON", conf: 0.5, x: 1, y: 1, w: 10, h: 10, ...patch });
const detection = (patch: Record<string, unknown>) =>
  JSON.stringify({ type: "detection", ts: 1, data: { frame_id: 7, capture_ts: 1, img_w: 640, img_h: 480, objects: [obj({})], ...patch } });
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
    telemetry({ heading_deg: 360 }),
    telemetry({ speed_mps: 30.01 }),
    telemetry({ speed_mps: 1e308 }),
    telemetry({ battery_pct: 50.5 }),
    telemetry({ seq: 2 ** 32 }),
    detection({ frame_id: 2 ** 32 }),
    detection({ img_w: 0 }),
    detection({ img_w: 4097 }),
    detection({ objects: [obj({ w: 0 })] }),
    detection({ objects: [obj({ x: 640 })] }),
    detection({ objects: [obj({ x: 600, w: 41 })] }),
    detection({ objects: [obj({ y: 470, h: 11 })] }),
    detection({ objects: Array.from({ length: 33 }, () => obj({})) }),
    detection({ objects: [obj({ class: "BANANA" })] }),
    detection({ objects: [obj({ conf: 1.01 })] }),
  ]) {
    expect(parseIf03(bad), bad).toBeNull();
  }
  for (const ok of [
    stats({ loss_rate_10s: null }),
    telemetry({ heading_deg: 359.99, speed_mps: 30, battery_pct: 100, seq: 2 ** 32 - 1 }),
    detection({ objects: [obj({ x: 600, w: 40 }), obj({ y: 479, h: 1 })] }),
    detection({ objects: Array.from({ length: 32 }, () => obj({})) }),
  ]) {
    expect(parseIf03(ok), ok).not.toBeNull();
  }
});
