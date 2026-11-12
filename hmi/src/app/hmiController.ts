// HMI 동작의 한 곳: 연결(GwConnection) + 상태(hmiState) + 주기 판정(tick).
// 시계·소켓·타이머는 주입받으므로, 늦음 → 기준선 재설정 → 새 기준선으로 FRESH까지의 순환을 가짜 시계로 시험한다.
// App.vue는 이 객체를 만들고 상태가 바뀔 때 화면을 다시 그리기만 한다.

import { GwConnection, type ConnEvent, type Deps } from "../net/gwConnection";
import { initialState, needsResync, onConnEvent, receiveText, type HmiState } from "../state/hmiState";
import { toConnView, type ConnView } from "../state/view";

export interface ControllerDeps extends Deps {
  now(): number; // HMI 시계 (ms)
}

export interface Snapshot {
  state: HmiState;
  conn: ConnView;
}

export class HmiController {
  private snap: Snapshot = { state: initialState(), conn: { state: "CONNECTING", failures: 0, resync: false } };
  private readonly connection: GwConnection;

  constructor(
    url: string,
    private readonly deps: ControllerDeps,
    private readonly onChange: (s: Snapshot) => void,
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
  }

  private onText(text: string): void {
    this.set({ ...this.snap, state: receiveText(this.snap.state, text, this.deps.now()) });
  }

  private onState(ev: ConnEvent): void {
    const now = this.deps.now();
    if (ev.state === "WAITING_RETRY") this.log(`IF-03 연결 끊김 (${ev.reason}), ${ev.retryInMs}ms 후 재접속`);
    this.set({ state: onConnEvent(this.snap.state, ev, now), conn: toConnView(ev, now) });
  }

  private set(s: Snapshot): void {
    this.snap = s;
    this.onChange(s);
  }
}
