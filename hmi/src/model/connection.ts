// 전송 층(net/)과 상태·화면 층(state/)이 함께 쓰는 연결 관련 타입. 둘 다 이 파일만 가져오고 서로는 모른다.

/** 접속 시도의 이유: 처음, 실패 뒤 재시도(failures: 마지막 OPEN 이후 연속 실패 횟수), 기준선 재설정 */
export type ConnectAttempt = { cause: "first" } | { cause: "retry"; failures: number } | { cause: "resync" };
export type ConnectCause = ConnectAttempt["cause"];
