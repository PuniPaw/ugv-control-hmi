import { describe, expect, it } from "vitest";
import { GwConnection, RETRY_DELAYS_MS, defaultGwUrl, type ConnectionState, type SocketLike } from "../src/net/gwConnection";

class FakeSocket implements SocketLike {
  onopen: SocketLike["onopen"] = null;
  onclose: SocketLike["onclose"] = null;
  onmessage: SocketLike["onmessage"] = null;
  onerror: SocketLike["onerror"] = null;
  closed = false;
  close() {
    this.closed = true;
  }
}

function harness() {
  const sockets: FakeSocket[] = [];
  const timers: Array<{ fn: () => void; ms: number; cleared: boolean }> = [];
  const states: Array<[ConnectionState, number | undefined]> = [];
  const texts: string[] = [];
  const conn = new GwConnection(
    "ws://gw/ws",
    { onText: (t) => texts.push(t), onState: (s, d) => states.push([s, d.retryInMs]) },
    {
      openSocket: () => {
        const s = new FakeSocket();
        sockets.push(s);
        return s;
      },
      setTimer: (fn, ms) => {
        const t = { fn, ms, cleared: false };
        timers.push(t);
        return t;
      },
      clearTimer: (h) => {
        (h as { cleared: boolean }).cleared = true;
      },
    },
  );
  const fireLastTimer = () => {
    const t = timers[timers.length - 1]!;
    if (!t.cleared) t.fn();
  };
  return { conn, sockets, timers, states, texts, fireLastTimer };
}

describe("GW connection (ICD 7장 재접속 규약)", () => {
  it("REQ_COM_008 retries with 1s, 2s, 4s, 5s, 5s while the GW is unreachable", () => {
    const h = harness();
    h.conn.start();
    for (let i = 0; i < 5; i++) {
      h.sockets[h.sockets.length - 1]!.onclose!({ code: 1006 });
      h.fireLastTimer();
    }
    expect(h.timers.map((t) => t.ms)).toEqual([...RETRY_DELAYS_MS, 5000]);
    expect(h.sockets.length).toBe(6);
  });

  it("REQ_COM_008 resets the delay to 1s after a successful connection", () => {
    const h = harness();
    h.conn.start();
    h.sockets[0]!.onclose!({ code: 1006 });
    h.fireLastTimer();
    h.sockets[1]!.onclose!({ code: 1006 });
    h.fireLastTimer();
    h.sockets[2]!.onopen!({});
    h.sockets[2]!.onclose!({ code: 1006 }); // GW가 뒤처진 HMI를 끊음
    expect(h.timers.map((t) => t.ms)).toEqual([1000, 2000, 1000]);
    expect(h.states.at(-1)).toEqual(["WAITING_RETRY", 1000]);
  });

  it("REQ_COM_008 passes text frames through and ignores binary", () => {
    const h = harness();
    h.conn.start();
    h.sockets[0]!.onopen!({});
    h.sockets[0]!.onmessage!({ data: '{"type":"stats"}' });
    h.sockets[0]!.onmessage!({ data: new ArrayBuffer(4) });
    expect(h.texts).toEqual(['{"type":"stats"}']);
  });

  it("stop() closes without scheduling a retry", () => {
    const h = harness();
    h.conn.start();
    h.sockets[0]!.onopen!({});
    h.conn.stop();
    expect(h.sockets[0]!.closed).toBe(true);
    expect(h.timers.length).toBe(0);
  });

  it("default URL follows the page host, override wins", () => {
    expect(defaultGwUrl("10.0.0.5")).toBe("ws://10.0.0.5:8080/ws");
    expect(defaultGwUrl("", undefined)).toBe("ws://127.0.0.1:8080/ws");
    expect(defaultGwUrl("x", "ws://gw:9000/ws")).toBe("ws://gw:9000/ws");
  });
});
