// HMI 동작의 한 곳: 연결(GwConnection) + 상태(hmiState) + 주기 판정(tick) + 화면 판정(present).
// 시계·소켓·타이머는 주입받으므로, 늦음 → 기준선 재설정 → 새 기준선으로 FRESH까지의 순환을 가짜 시계로 시험한다.
// 연결 이벤트를 상태에 어떻게 반영할지(열림/끊김/재설정)는 여기서 정한다: state/ 층은 전송 층을 모른다.
// App.vue는 onChange로 받은 View를 그리기만 한다.

import { GwConnection, type ConnEvent, type Deps } from "../net/gwConnection";
import { commandText, type CommandSpec } from "../if03/messages";
import { canRetry, cancelDraft, commandSent, draftCommand, resultDeadlinePassed } from "../state/commands";
import {
  connectionLost, connectionOpened, initialState, needsResync, receiveText, resyncStarted, type HmiState,
} from "../state/hmiState";
import { commandBlock, present, type ConnView, type Presented } from "../state/view";

export interface ControllerDeps extends Deps {
  now(): number; // HMI 벽시계 (ms): 메시지 수신 시각, 신선도
  monotonic(): number; // 단조 시계 (ms): 재접속 카운트다운, 명령 결과 기한
  newReqId(): string; // 명령마다 유일한 req_id (ICD 7.2). 보낼 때마다 새로 발급
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

  /** 주기 판정 (App.vue가 250ms마다 부른다): 늦음이 이어지면 기준선 재설정 (ICD 7장), 명령 결과 기한. 화면도 다시 판정 */
  tick(): void {
    this.state = { ...this.state, commands: resultDeadlinePassed(this.state.commands, this.deps.monotonic()) };
    if (needsResync(this.state, this.deps.now())) {
      this.log("IF-03 데이터 지연이 이어져 기준선 재설정 (재접속)");
      this.connection.resync();
    }
    this.onChange(this.view());
  }

  /** 명령 버튼: 확인 절차를 시작한다. 아직 보내지 않는다 (REQ-CMD-001) */
  requestCommand(spec: CommandSpec): void {
    this.update({ ...this.state, commands: draftCommand(this.state.commands, spec) });
  }

  /**
   * 다시 보내기: 보낸 명령과 같은 내용으로 확인 절차를 다시 연다. 확인하면 새 req_id로 보낸다 (이슈 #27).
   * 다시 보낼 수 없는 명령(결과 대기 중 등, canRetry)이면 아무것도 하지 않는다
   */
  retryCommand(reqId: string): void {
    const c = this.state.commands.sent.find((x) => x.reqId === reqId);
    if (c && canRetry(c)) this.update({ ...this.state, commands: draftCommand(this.state.commands, c.spec, reqId) });
  }

  cancelCommand(): void {
    this.update({ ...this.state, commands: cancelDraft(this.state.commands) });
  }

  /**
   * 운용자 확인: 확인을 기다리던 명령을 새 req_id로 보낸다. 지금 보낼 수 없으면(REQ-CMD-004: GW 연결 없음,
   * 차량 링크 LOST) 보내지 않고 확인 단계에 그대로 둔다. 같은 req_id로 다시 보내는 경로는 없다 (이슈 #27)
   */
  confirmCommand(): void {
    const draft = this.state.commands.draft;
    if (draft === null || commandBlock(this.state, this.conn) !== null) return;
    const reqId = this.deps.newReqId();
    if (!this.connection.send(commandText(reqId, draft.spec))) return;
    this.update({ ...this.state, commands: commandSent(this.state.commands, reqId, this.deps.monotonic()) });
  }

  private update(state: HmiState): void {
    this.state = state;
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

/**
 * 명령 req_id: 128비트 난수의 16진 32자 (ICD 7.2: 1~64바이트, 명령마다 유일).
 * crypto.randomUUID는 보안 맥락(https, localhost)에서만 있으므로, 다른 PC에서 http로 연 HMI에서도 되는
 * getRandomValues를 쓴다
 */
export function randomReqId(): string {
  const b = crypto.getRandomValues(new Uint8Array(16));
  return Array.from(b, (x) => x.toString(16).padStart(2, "0")).join("");
}
