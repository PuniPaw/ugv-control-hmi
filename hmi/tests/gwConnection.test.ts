// IF-03 재접속 상태 기계 (가짜 소켓·가짜 타이머). describe 없음: it 제목이 STP의 시험 이름이다.
import { expect, it } from "vitest";
import {
  CONNECT_TIMEOUT_MS, GwConnection, IDLE_TIMEOUT_MS, RETRY_DELAYS_MS, defaultGwUrl,
  type ConnectionState, type SocketLike,
} from "../src/net/gwConnection";

class FakeSocket implements SocketLike {
  onopen: SocketLike["onopen"] = null;
  onclose: SocketLike["onclose"] = null;
  onmessage: SocketLike["onmessage"] = null;
  onerror: SocketLike["onerror"] = null;
  closed = false;
  close() {
    this.closed = true;
  }
  // 소켓 쪽 이벤트 (처리기가 떼어졌으면 전달되지 않는다)
  open() { this.onopen?.({}); }
  drop(code = 1006) { this.onclose?.({ code }); }
  send(data: unknown) { this.onmessage?.({ data }); }
}

interface Timer { fn: () => void; ms: number; cleared: boolean; fired: boolean }

function harness(opts: { throwOn?: number[] } = {}) {
  const sockets: FakeSocket[] = [];
  const timers: Timer[] = [];
  const states: Array<[ConnectionState, number | undefined, string | undefined]> = [];
  const texts: string[] = [];
  let opens = 0;
  const conn = new GwConnection(
    "ws://gw/ws",
    { onText: (t) => texts.push(t), onState: (s, d) => states.push([s, d.retryInMs, d.reason]) },
    {
      openSocket: () => {
        opens += 1;
        if (opts.throwOn?.includes(opens)) throw new SyntaxError("bad url");
        const s = new FakeSocket();
        sockets.push(s);
        return s;
      },
      setTimer: (fn, ms) => {
        const t = { fn, ms, cleared: false, fired: false };
        timers.push(t);
        return t;
      },
      clearTimer: (h) => {
        (h as Timer).cleared = true;
      },
    },
  );
  const pending = () => timers.filter((t) => !t.cleared && !t.fired);
  /** 대기 중인 타이머(항상 하나)를 만료시킨다 */
  const fire = () => {
    const p = pending();
    expect(p.length).toBe(1);
    p[0]!.fired = true;
    p[0]!.fn();
  };
  const last = () => sockets[sockets.length - 1]!;
  const retryDelays = () => states.filter(([s]) => s === "WAITING_RETRY").map(([, ms]) => ms);
  return { conn, sockets, pending, fire, last, states, texts, retryDelays };
}

it("REQ_HMI_006 retries with 1s, 2s, 4s, 5s, 5s while the GW is unreachable", () => {
  const h = harness();
  h.conn.start();
  for (let i = 0; i < 5; i++) {
    h.last().drop();
    h.fire();
  }
  expect(h.retryDelays()).toEqual([...RETRY_DELAYS_MS, 5000]);
  expect(h.sockets.length).toBe(6);
});

it("REQ_HMI_006 resets the delay to 1s after a successful connection", () => {
  const h = harness();
  h.conn.start();
  h.last().drop();
  h.fire();
  h.last().drop();
  h.fire();
  h.last().open();
  h.last().drop(); // GW가 뒤처진 HMI를 끊음
  expect(h.retryDelays()).toEqual([1000, 2000, 1000]);
});

it("REQ_HMI_006 an open connection that receives nothing for 4s is closed and retried", () => {
  const h = harness();
  h.conn.start();
  const s = h.last();
  s.open();
  expect(h.pending().map((t) => t.ms)).toEqual([IDLE_TIMEOUT_MS]); // 열린 직후부터 잰다
  s.send("a"); // 받을 때마다 다시 잰다
  expect(h.pending().map((t) => t.ms)).toEqual([IDLE_TIMEOUT_MS]);
  h.fire(); // FIN/RST 없이 GW가 사라짐
  expect(s.closed).toBe(true);
  expect(h.states.at(-1)).toEqual(["WAITING_RETRY", 1000, "idle timeout"]);
  s.send("late"); // 버린 소켓의 이벤트는 전달되지 않는다
  expect(h.texts).toEqual(["a"]);
});

it("REQ_HMI_006 a connect attempt that does not open within 5s is closed and retried", () => {
  const h = harness();
  h.conn.start();
  expect(h.pending().map((t) => t.ms)).toEqual([CONNECT_TIMEOUT_MS]);
  h.fire();
  expect(h.sockets[0]!.closed).toBe(true);
  expect(h.states.at(-1)).toEqual(["WAITING_RETRY", 1000, "connect timeout"]);
});

it("REQ_HMI_006 a socket constructor that throws goes through the same retry path", () => {
  const h = harness({ throwOn: [2, 3] });
  h.conn.start();
  h.last().drop();
  h.fire(); // 두 번째 시도: throw
  expect(h.states.at(-1)).toEqual(["WAITING_RETRY", 2000, "bad url"]);
  h.fire(); // 세 번째 시도: throw
  h.fire(); // 네 번째 시도: 성공
  expect(h.retryDelays()).toEqual([1000, 2000, 4000]);
  expect(h.sockets.length).toBe(2);
});

it("REQ_HMI_007 passes text frames through and ignores binary", () => {
  const h = harness();
  h.conn.start();
  h.last().open();
  h.last().send('{"type":"stats"}');
  h.last().send(new ArrayBuffer(4));
  expect(h.texts).toEqual(['{"type":"stats"}']);
});

it("REQ_HMI_006 stop() closes the socket, stops all events and schedules no retry", () => {
  const h = harness();
  h.conn.start();
  const s = h.last();
  s.open();
  h.conn.stop();
  expect(s.closed).toBe(true);
  s.send("after-stop");
  s.drop();
  expect(h.texts).toEqual([]);
  expect(h.pending()).toEqual([]);
});

it("REQ_HMI_006 calling start() twice keeps a single live socket", () => {
  const h = harness();
  h.conn.start();
  h.conn.start();
  expect(h.sockets[0]!.closed).toBe(true);
  h.sockets[0]!.send("from-first-socket");
  h.sockets[1]!.open();
  h.sockets[1]!.send("from-second");
  expect(h.texts).toEqual(["from-second"]);
  expect(h.pending().length).toBe(1);
});

it("REQ_HMI_006 default URL follows the page host, override wins", () => {
  expect(defaultGwUrl("10.0.0.5")).toBe("ws://10.0.0.5:8080/ws");
  expect(defaultGwUrl("", undefined)).toBe("ws://127.0.0.1:8080/ws");
  expect(defaultGwUrl("x", "ws://gw:9000/ws")).toBe("ws://gw:9000/ws");
});
