// 시험용 가짜 WebSocket (gwConnection·hmiController 시험이 함께 쓴다). 시험 파일이 아니다.
import type { SocketLike } from "../src/net/gwConnection";

export class FakeSocket implements SocketLike {
  onopen: SocketLike["onopen"] = null;
  onclose: SocketLike["onclose"] = null;
  onmessage: SocketLike["onmessage"] = null;
  onerror: SocketLike["onerror"] = null;
  closed = false;
  sent: string[] = []; // HMI가 보낸 텍스트
  send(text: string) {
    this.sent.push(text);
  }
  close() {
    this.closed = true;
  }
  // 소켓 쪽 이벤트 (처리기가 떼어졌으면 전달되지 않는다)
  open() { this.onopen?.({}); }
  drop(code = 1006) { this.onclose?.({ code }); }
  deliver(data: unknown) { this.onmessage?.({ data }); }
}
