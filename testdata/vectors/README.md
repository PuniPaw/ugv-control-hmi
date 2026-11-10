# 공용 테스트 벡터

GoogleTest(C++)와 pytest(Python)가 같은 파일을 읽어 바이트 단위 일치를 확인한다 (STP 2.4, ADR-002).

- 파일: `<메시지>.json` (예: `telemetry.json`)
- 항목: `name`, 필드 값, 기대 바이트 `hex`, 기대 결과(`accept` 또는 오류 계수 이름)
- STP의 TC는 입력을 벡터 `name`으로 가리킨다.
