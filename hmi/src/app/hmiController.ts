// HMI 동작의 한 곳: 연결(GwConnection) + 상태(hmiState) + 주기 판정(tick) + 화면 판정(present).
// 시계·소켓·타이머는 주입받으므로, 늦음 → 기준선 재설정 → 새 기준선으로 FRESH까지의 순환을 가짜 시계로 시험한다.
// 연결 이벤트를 상태에 어떻게 반영할지(열림/끊김/재설정)는 여기서 정한다: state/ 층은 전송 층을 모른다.
// App.vue는 이 객체를 만들고 view()를 그리기만 한다.

import { GwConnection, type ConnEvent, type Deps } from "../net/gwConnection";
import {
  connectionLost, connectionOpened, initialState, needsResync, receiveText, resyncStarted, type HmiState,
} from "../state/hmiState";
import { present, type ConnView, type Presented } from "../state/view";

export interface ControllerDeps extends Deps {
  now(): number; // HMI 벽시계 (ms): 메시지 수신 시각, 신선도
  monotonic(): number; // 단조 시계 (ms): 재접속 카운트다운
}

export interface Snapshot {
  state: HmiState;
  conn: ConnView;
}

export type View = Snapshot & Presented;

export class HmiController {
  private snap: Snapshot = { state: initialState(), conn: { state: "CONNECTING", cause: "first" } };
  private readonly connection: GwConnection;

  constructor(
    url: string,
    private readonly deps: ControllerDeps,
    private readonly onChange: () => void,
    private readonly log: (msg: string) => void = () => {},
  ) {
    this.connection = new GwConnection(
      url,
      { onText: (t) => this.onText(t), onState: (ev) => this.onState(ev) },
      deps,
    );
  }

  get snapshot(): Snapshot {
    return this.snap;
  }

  /** 지금 시각으로 판정한 화면 (App.vue와 시험이 같은 경로를 쓴다) */
  view(): View {
    return { ...this.snap, ...present(this.snap.state, this.snap.conn, { wall: this.deps.now(), mono: this.deps.monotonic() }) };
  }

  start(): void {
    this.connection.start();
  }

  stop(): void {
    this.connection.stop();
  }

  /** 주기 판정 (App.vue가 250ms마다 부른다): 늦음이 이어지면 기준선 재설정 (ICD 7장) */
  tick(): void {
    if (needsResync(this.snap.state, this.deps.now())) {
      this.log("IF-03 데이터 지연이 이어져 기준선 재설정 (재접속)");
      this.connection.resync();
    }
    this.onChange();
  }

  private onText(text: string): void {
    this.set({ ...this.snap, state: receiveText(this.snap.state, text, this.deps.now()) });
  }

  private onState(ev: ConnEvent): void {
    const s = this.snap.state;
    switch (ev.state) {
      case "OPEN":
        this.set({ state: connectionOpened(s, this.deps.now()), conn: ev });
        return;
      case "WAITING_RETRY":
        this.log(`IF-03 연결 끊김 (${ev.reason}), ${ev.retryInMs}ms 후 재접속`);
        this.set({
          state: connectionLost(s),
          conn: { state: ev.state, failures: ev.failures, retryAtMono: this.deps.monotonic() + ev.retryInMs },
        });
        return;
      case "CONNECTING":
        // 재설정은 연결 범위만 버린다(궤적 유지). 처음·재시도 접속은 이미 끊긴 뒤라 끊김으로 반영
        this.set({ state: ev.cause === "resync" ? resyncStarted(s) : connectionLost(s), conn: ev });
        return;
    }
  }

  private set(s: Snapshot): void {
    this.snap = s;
    this.onChange();
  }
}
