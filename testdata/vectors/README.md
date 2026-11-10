# 공용 테스트 벡터

GoogleTest(C++)와 pytest(Python)가 같은 파일을 읽어 바이트 단위 일치를 확인한다 (STP 2.4, ADR-002).

- 파일: `<메시지>.json` (예: `telemetry.json`)
- 항목: `name`, 필드 값, 기대 바이트 `hex`, 기대 결과(`accept` 또는 오류 계수 이름)
- STP의 TC는 입력을 벡터 `name`으로 가리킨다.

## 현재 벡터

| 파일 | 내용 | 생성기 |
|---|---|---|
| `frame.json` | 헤더+CRC 프레임 4종(HEARTBEAT 최소·최대값, ACK, TELEMETRY), CRC 표준 벡터 | `tools/vectors/gen_vectors.py` |
| `telemetry.json` | TELEMETRY payload: nominal, 경계 최소·최대, 예약 비트 (항목별 `tc` 표시) | `tools/vectors/gen_vectors.py` |
| `detection.json` | DETECTION payload: nominal, count 0, count 32, 박스가 이미지 끝에 딱 맞는 경우 | `tools/vectors/gen_vectors.py` |
| `ack.json` | ACK payload: result 0·1·2 | `tools/vectors/gen_vectors.py` |
| `validation.json` | 검증 규칙 ①~⑧ case 48개: `source`(수신 포트), 프레임 `hex`, `expect`(accept 또는 첫 실패 규칙의 오류 계수), `tc` | `tools/vectors/gen_vectors.py` |
