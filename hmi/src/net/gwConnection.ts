// IF-03 WebSocket 연결과 재접속 (ICD 7장, REQ-HMI-006).
//
// 끊김으로 보는 경우: onclose(GW 종료, 뒤처져서 끊김 1006 등), 5초 안에 열리지 않는 접속 시도,
// 열린 뒤 4초 동안 아무것도 받지 못한 연결(GW는 stats를 송신원마다 1Hz로 보내므로 정상이면 1초 안에
// 뭔가 온다. FIN/RST 없이 GW PC가 사라진 경우를 TCP keepalive보다 빨리 알아챈다).
// 끊기면 1s부터 두 배씩 최대 5s 간격으로 다시 접속하고, 접속되면 간격을 1s로 되돌린다.
// 호출자가 resync()를 부르면(데이터가 계속 늦게 옴 → 기준선 재설정) 지금 연결을 닫고 곧바로 다시 접속한다.
// 이것은 연결 실패가 아니다: 실패 횟수를 올리지 않고 기다리지도 않으며, 이벤트의 cause가 "resync"다.
//
// WebSocket과 타이머는 주입한다: 단위 시험은 가짜 소켓과 가짜 타이머로 확인한다.

export const RETRY_DELAYS_MS = [1000, 2000, 4000] as const; // 1s부터 두 배씩
export const MAX_RETRY_DELAY_MS = 5000; // 그 뒤로는 이 값 (ICD 7장 "최대 5초")
export const CONNECT_TIMEOUT_MS = 5000;
export const IDLE_TIMEOUT_MS = 4000;

export interface SocketLike {
  onopen: ((ev: unknown) => void) | null;
  onclose: ((ev: { code: number }) => void) | null;
  onmessage: ((ev: { data: unknown }) => void) | null;
  onerror: ((ev: unknown) => void) | null;
  close(): void;
}

export interface Deps {
  openSocket(url: string): SocketLike;
  setTimer(fn: () => void, ms: number): unknown;
  clearTimer(handle: unknown): void;
}

/** 연결 상태가 바뀔 때마다 하나. 상태마다 뜻이 있는 값만 가진다 */
export type ConnEvent =
  // 접속 시도의 이유: 처음, 실패 뒤 재시도(failures: 마지막 OPEN 이후 연속 실패 횟수), 기준선 재설정
  | { state: "CONNECTING"; cause: "first" }
  | { state: "CONNECTING"; cause: "retry"; failures: number }
  | { state: "CONNECTING"; cause: "resync" }
  | { state: "OPEN" }
  | { state: "WAITING_RETRY"; failures: number; retryInMs: number; reason: string }; // reason: "close 1006" 등

export interface Handlers {
  onText(text: string): void;
  onState(ev: ConnEvent): void;
}

export class GwConnection {
  private socket: SocketLike | null = null;
  private timer: unknown = null; // 한 번에 하나: 접속 제한 시간, 무수신 제한 시간, 재접속 대기
  private failures = 0;
  private stopped = true;

  constructor(
    private readonly url: string,
    private readonly handlers: Handlers,
    private readonly deps: Deps,
  ) {}

  start(): void {
    this.stop(); // 두 번 불러도 소켓은 하나
    this.stopped = false;
    this.failures = 0;
    this.connect("first");
  }

  /** 기준선 재설정: 지금 연결을 버리고 곧바로 다시 접속한다 (실패로 세지 않음). 정지 상태면 아무것도 하지 않는다 */
  resync(): void {
    if (this.stopped) return;
    this.disarm();
    this.dropSocket();
    this.connect("resync");
  }

  stop(): void {
    this.stopped = true;
    this.disarm();
    this.dropSocket();
  }

  private arm(ms: number, fn: () => void): void {
    this.disarm();
    this.timer = this.deps.setTimer(() => {
      this.timer = null;
      fn();
    }, ms);
  }

  private disarm(): void {
    if (this.timer !== null) this.deps.clearTimer(this.timer);
    this.timer = null;
  }

  /** 소켓의 처리기를 모두 떼고 닫는다. 이후 그 소켓의 이벤트는 전달되지 않는다 */
  private dropSocket(): void {
    const s = this.socket;
    this.socket = null;
    if (!s) return;
    s.onopen = s.onclose = s.onmessage = s.onerror = null;
    try {
      s.close();
    } catch {
      // 이미 닫힘
    }
  }

  private connect(cause: "first" | "retry" | "resync"): void {
    this.handlers.onState(
      cause === "retry" ? { state: "CONNECTING", cause, failures: this.failures } : { state: "CONNECTING", cause },
    );
    let s: SocketLike;
    try {
      s = this.deps.openSocket(this.url);
    } catch (e) {
      this.fail(e instanceof Error ? e.message : String(e)); // 잘못된 주소 등: 같은 재시도 경로
      return;
    }
    this.socket = s;
    this.arm(CONNECT_TIMEOUT_MS, () => this.fail("connect timeout"));
    s.onopen = () => {
      this.failures = 0;
      this.handlers.onState({ state: "OPEN" });
      this.arm(IDLE_TIMEOUT_MS, () => this.fail("idle timeout"));
    };
    s.onmessage = (ev) => {
      this.arm(IDLE_TIMEOUT_MS, () => this.fail("idle timeout"));
      if (typeof ev.data === "string") this.handlers.onText(ev.data);
    };
    s.onerror = () => {}; // 이어서 onclose가 온다
    s.onclose = (ev) => this.fail(`close ${ev.code}`);
  }

  private fail(reason: string): void {
    if (this.stopped) return;
    this.dropSocket();
    const delay = retryDelay(this.failures);
    this.failures += 1;
    this.handlers.onState({ state: "WAITING_RETRY", failures: this.failures, retryInMs: delay, reason });
    this.arm(delay, () => this.connect("retry"));
  }
}

/** 연속 실패 n번째(0부터)의 재접속 대기 */
export function retryDelay(failures: number): number {
  return RETRY_DELAYS_MS[failures] ?? MAX_RETRY_DELAY_MS;
}

/** 페이지 호스트 기준 기본 주소: ws://<호스트>:8080/ws (VITE_GW_URL로 바꿀 수 있다) */
export function defaultGwUrl(pageHost: string, override?: string): string {
  if (override) return override;
  return `ws://${pageHost || "127.0.0.1"}:8080/ws`;
}

/** 브라우저 WebSocket을 SocketLike로 감싼다 (GwConnection은 브라우저 타입에 매이지 않는다). */
export function browserSocket(url: string): SocketLike {
  const ws = new WebSocket(url); // 주소가 틀리면 여기서 throw → GwConnection이 재시도 경로로
  const s: SocketLike = {
    onopen: null,
    onclose: null,
    onmessage: null,
    onerror: null,
    close: () => ws.close(),
  };
  ws.onopen = (ev) => s.onopen?.(ev);
  ws.onclose = (ev) => s.onclose?.({ code: ev.code });
  ws.onmessage = (ev) => s.onmessage?.({ data: ev.data });
  ws.onerror = (ev) => s.onerror?.(ev);
  return s;
}
