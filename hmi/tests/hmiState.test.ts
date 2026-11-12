import { describe, expect, it } from "vitest";
import type { If03Message } from "../src/if03/messages";
import { FRESHNESS_LIMIT_MS, TRACK_MAX_POINTS, apply, freshness, initialState } from "../src/state/hmiState";

const telemetry = (seq: number, lat: number, lon: number): If03Message => ({
  type: "telemetry",
  ts: 1000 + seq,
  data: { lat, lon, heading_deg: 90, speed_mps: 4.5, battery_pct: 76, mode: "MISSION", flags: ["GPS_VALID"], seq, rx_ts: 1000 + seq },
});
const stats = (ts: number): If03Message => ({
  type: "stats",
  ts,
  data: { source: "VEHICLE", loss_rate_10s: 0.1, rx_count: 90, lost_count: 10, errors: { crc: 1 } },
});

describe("HMI state", () => {
  it("REQ_HMI_002 starts with both links LOST like the GW", () => {
    const s = initialState();
    expect(s.link.VEHICLE.state).toBe("LOST");
    expect(s.link.DETECTOR.state).toBe("LOST");
    expect(s.vehicle).toBeNull();
  });

  it("REQ_HMI_001 telemetry updates position and appends to the track", () => {
    let s = initialState();
    s = apply(s, telemetry(0, 36.1, 127.1), 0);
    s = apply(s, telemetry(1, 36.2, 127.2), 0);
    expect(s.vehicle?.lat).toBe(36.2);
    expect(s.vehicle?.heading_deg).toBe(90);
    expect(s.track).toEqual([[36.1, 127.1], [36.2, 127.2]]);
  });

  it("REQ_HMI_001 track keeps the latest TRACK_MAX_POINTS points", () => {
    let s = initialState();
    for (let i = 0; i < TRACK_MAX_POINTS + 5; i++) s = apply(s, telemetry(i, i, i), 0);
    expect(s.track.length).toBe(TRACK_MAX_POINTS);
    expect(s.track[0]).toEqual([5, 5]);
  });

  it("REQ_HMI_002 link_status and stats update per source", () => {
    let s = initialState();
    s = apply(s, { type: "link_status", ts: 1, data: { source: "VEHICLE", state: "DEGRADED", last_rx_ts: 7 } }, 0);
    s = apply(s, stats(10), 20);
    expect(s.link.VEHICLE).toEqual({ state: "DEGRADED", lastRxTs: 7 });
    expect(s.link.DETECTOR.state).toBe("LOST");
    expect(s.stats.VEHICLE?.loss_rate_10s).toBe(0.1);
    expect(s.stats.DETECTOR).toBeNull();
  });

  it("does not mutate the previous state", () => {
    const s0 = initialState();
    const s1 = apply(s0, telemetry(0, 1, 2), 0);
    expect(s0.track).toEqual([]);
    expect(s1).not.toBe(s0);
  });
});

describe("GW data freshness (ICD 7장, TBD-04)", () => {
  it("REQ_COM_008 WAITING before the first stats", () => {
    expect(freshness(initialState(), 0)).toBe("WAITING");
  });

  it("REQ_COM_008 FRESH within 2s of the last stats, STALE after", () => {
    const s = apply(initialState(), stats(1000), 1000);
    expect(freshness(s, 1000 + FRESHNESS_LIMIT_MS)).toBe("FRESH");
    expect(freshness(s, 1000 + FRESHNESS_LIMIT_MS + 1)).toBe("STALE");
  });

  it("REQ_COM_008 STALE when stats arrive more than 2s after the GW sent them", () => {
    const s = apply(initialState(), stats(1000), 1000 + FRESHNESS_LIMIT_MS + 1);
    expect(freshness(s, 1000 + FRESHNESS_LIMIT_MS + 1)).toBe("STALE");
  });
});
