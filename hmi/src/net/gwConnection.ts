// IF-03 WebSocket 연결과 재접속 (ICD 7장).
//
// GW가 끊으면(뒤처짐·대기열 초과로 TCP 종료 → close code 1006 등) 1s부터 두 배씩 최대 5s 간격으로
// 다시 접속한다. 접속되면 간격을 1s로 되돌린다. GW는 접속 직후 현재 stats·link_status를 보내므로
// 재접속하면 화면이 곧바로 현재 상태로 돌아온다.
//
// WebSocket과 타이머는 주입한다: 단위 시험은 가짜 소켓과 가짜 타이머로 재접속 간격을 확인한다.

export type ConnectionState = "CONNECTING" | "OPEN" | "WAITING_RETRY";

export const RETRY_DELAYS_MS = [1000, 2000, 4000, 5000] as const; // 이후는 마지막 값 반복

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

export interface Handlers {
  onText(text: string): void;
  onState(state: ConnectionState, detail: { closeCode?: number; retryInMs?: number }): void;
}

export class GwConnection {
  private socket: SocketLike | null = null;
  private retryTimer: unknown = null;
  private failures = 0; // 접속 이후 연속으로 끊긴 횟수 → 다음 대기 간격
  private stopped = false;

  constructor(
    private readonly url: string,
    private readonly handlers: Handlers,
    private readonly deps: Deps,
  ) {}

  start(): void {
    this.stopped = false;
    this.connect();
  }

  stop(): void {
    this.stopped = true;
    if (this.retryTimer !== null) this.deps.clearTimer(this.retryTimer);
    this.retryTimer = null;
    const s = this.socket;
    this.socket = null;
    if (s) {
      s.onclose = null; // 스스로 닫는 것은 재접속하지 않는다
      s.close();
    }
  }

  private connect(): void {
    this.handlers.onState("CONNECTING", {});
    const s = this.deps.openSocket(this.url);
    this.socket = s;
    s.onopen = () => {
      this.failures = 0;
      this.handlers.onState("OPEN", {});
    };
    s.onmessage = (ev) => {
      if (typeof ev.data === "string") this.handlers.onText(ev.data);
    };
    s.onerror = () => {}; // 이어서 onclose가 온다
    s.onclose = (ev) => {
      if (this.socket !== s || this.stopped) return;
      this.socket = null;
      const delay = RETRY_DELAYS_MS[Math.min(this.failures, RETRY_DELAYS_MS.length - 1)] ?? 5000;
      this.failures += 1;
      this.handlers.onState("WAITING_RETRY", { closeCode: ev.code, retryInMs: delay });
      this.retryTimer = this.deps.setTimer(() => {
        this.retryTimer = null;
        if (!this.stopped) this.connect();
      }, delay);
    };
  }
}

/** 페이지 호스트 기준 기본 주소: ws://<호스트>:8080/ws (VITE_GW_URL로 바꿀 수 있다) */
export function defaultGwUrl(pageHost: string, override?: string): string {
  if (override) return override;
  return `ws://${pageHost || "127.0.0.1"}:8080/ws`;
}

/** 브라우저 WebSocket을 SocketLike로 감싼다 (GwConnection은 브라우저 타입에 매이지 않는다). */
export function browserSocket(url: string): SocketLike {
  const ws = new WebSocket(url);
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
