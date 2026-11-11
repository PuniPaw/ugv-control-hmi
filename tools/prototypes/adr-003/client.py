"""시제품 공통 확인: 클라이언트 2개가 1초 동안 받은 메시지 수와 간격, 서버로 명령 1개 송신."""
import asyncio, json, sys, time
import websockets

async def one(url, idx, sink):
    async with websockets.connect(url) as ws:
        if idx == 0:
            await ws.send(json.dumps({"type": "command", "data": {"req_id": "r1", "cmd_type": "STOP"}}))
        t0 = time.monotonic(); seqs = []; ts = []
        while time.monotonic() - t0 < 1.0:
            m = json.loads(await asyncio.wait_for(ws.recv(), 1.0)); seqs.append(m["seq"]); ts.append(time.monotonic())
        gaps = [round((b - a) * 1000) for a, b in zip(ts, ts[1:])]
        sink.append((idx, len(seqs), min(gaps), max(gaps), seqs == sorted(seqs)))

async def main(url):
    sink = []
    await asyncio.gather(one(url, 0, sink), one(url, 1, sink))
    for r in sorted(sink): print("client %d: %d msgs, gap %d-%dms, in order %s" % r)

asyncio.run(main(sys.argv[1]))
