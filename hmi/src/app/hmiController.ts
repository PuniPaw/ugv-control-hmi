// HMI 동작의 한 곳: 연결(GwConnection) + 상태(hmiState) + 주기 판정(tick) + 화면 판정(present).
// 시계·소켓·타이머는 주입받으므로, 늦음 → 기준선 재설정 → 새 기준선으로 FRESH까지의 순환을 가짜 시계로 시험한다.
// 연결 이벤트를 상태에 어떻게 반영할지(열림/끊김/재설정)는 여기서 정한다: state/ 층은 전송 층을 모른다.
// App.vue는 onChange로 받은 View를 그리기만 한다.

import { GwConnection, type ConnEvent, type Deps } from "../net/gwConnection";
import {
  connectionLost, connectionOpened, initialState, needsResync, receiveText, resyncStarted, type HmiState,
} from "../state/hmiState";
import { present, type ConnView, type Presented } from "../state/view";

export interface ControllerDeps extends Deps {
  now(): number; // HMI 벽시계 (ms): 메시지 수신 시각, 신선도
  monotonic(): number; // 단조 시계 (ms): 재접속 카운트다운
}

export type View = { state: HmiState; conn: ConnView } & Presented;

export class HmiController {
  private state: HmiState = initialState();
  private conn: ConnView = { state: "CONNECTING", cause: "first" };
  private readonly connection: GwConnection;

  constructor(
    url: string,
    private readonly deps: ControllerDeps,
    private readonly onChange: (v: View) => void,
    private readonly log: (msg: string) => void = () => {},
  ) {
    this.connection = new GwConnection(
      url,
      { onText: (t) => this.onText(t), onState: (ev) => this.onState(ev) },
      deps,
    );
  }

  /** 지금 시각으로 판정한 화면 (App.vue와 시험이 같은 경로를 쓴다) */
  view(): View {
    const clocks = { wall: this.deps.now(), mono: this.deps.monotonic() };
    return { state: this.state, conn: this.conn, ...present(this.state, this.conn, clocks) };
  }

  start(): void {
    this.connection.start();
  }

  stop(): void {
    this.connection.stop();
  }

  /** 주기 판정 (App.vue가 250ms마다 부른다): 늦음이 이어지면 기준선 재설정 (ICD 7장). 화면도 다시 판정 */
  tick(): void {
    if (needsResync(this.state, this.deps.now())) {
      this.log("IF-03 데이터 지연이 이어져 기준선 재설정 (재접속)");
      this.connection.resync();
    }
    this.onChange(this.view());
  }

  private onText(text: string): void {
    this.state = receiveText(this.state, text, this.deps.now());
    this.onChange(this.view());
  }

  private onState(ev: ConnEvent): void {
    switch (ev.state) {
      case "OPEN": {
        this.state = connectionOpened(this.state, this.deps.now());
        this.conn = ev;
        break;
      }
      case "WAITING_RETRY":
        this.log(`IF-03 연결 끊김 (${ev.reason}), ${ev.retryInMs}ms 후 재접속`);
        this.state = connectionLost(this.state);
        this.conn = { state: ev.state, failures: ev.failures, retryAtMono: this.deps.monotonic() + ev.retryInMs };
        break;
      case "CONNECTING":
        switch (ev.cause) {
          case "resync":
            // 재설정은 연결 끊김이 아니다: 연결 범위만 버린다 (궤적은 텔레메트리 seq 간격으로 apply()가 정한다)
            this.state = resyncStarted(this.state);
            break;
          case "first":
          case "retry":
            this.state = connectionLost(this.state);
            break;
        }
        this.conn = ev;
        break;
    }
    this.onChange(this.view());
  }
}
