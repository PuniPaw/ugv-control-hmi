<script setup lang="ts">
// 상태 패널: 송신원별 링크 상태·10초 유실률, 차량 상태 (REQ-HMI-002). 문구와 판정은 state/view.ts의 present()가
// 정하고 여기서는 그리기만 한다.
import type { LinkState, Source } from "../if03/messages";
import type { Presented } from "../state/view";

defineProps<{ view: Presented }>();

const SOURCE_LABEL: Record<Source, string> = { VEHICLE: "차량", DETECTOR: "탐지 노드" };
const LINK_CLASS: Record<LinkState, string> = { CONNECTED: "ok", DEGRADED: "warn", LOST: "bad" };
</script>

<template>
  <aside class="panel" data-testid="status-panel" :data-stale="view.stale.all">
    <section :class="{ dim: view.stale.all }">
      <h2>링크</h2>
      <div v-for="row in view.links" :key="row.source" class="row" :data-testid="`link-${row.source}`">
        <span class="label">{{ SOURCE_LABEL[row.source] }}</span>
        <span class="badge" :class="LINK_CLASS[row.state]" :data-testid="`link-state-${row.source}`">{{ row.state }}</span>
        <span class="sub">
          유실 <span :data-testid="`loss-${row.source}`">{{ row.loss }}</span> ·
          <span :data-testid="`age-${row.source}`">{{ row.age }}</span>
        </span>
      </div>
    </section>

    <section data-testid="vehicle" :class="{ dim: view.stale.vehicle }" :data-stale="view.stale.vehicle">
      <h2>차량<span v-if="view.vehicle && view.stale.vehicle" class="old"> · 이전 값</span></h2>
      <template v-if="view.vehicle">
        <div class="row"><span class="label">모드</span><span data-testid="mode">{{ view.vehicle.mode }}</span></div>
        <div class="row"><span class="label">배터리</span><span :class="view.vehicle.batteryLevel" data-testid="battery">{{ view.vehicle.battery }}</span></div>
        <div class="row"><span class="label">속도</span><span>{{ view.vehicle.speed }}</span></div>
        <div class="row"><span class="label">방향</span><span>{{ view.vehicle.heading }}</span></div>
        <div class="row"><span class="label">위치</span><span class="mono" data-testid="position">{{ view.vehicle.position }}</span></div>
        <div class="row"><span class="label">상태</span><span class="flags">{{ view.vehicle.flags }}</span></div>
      </template>
      <p v-else class="sub">텔레메트리 수신 전</p>
    </section>

    <section v-if="view.discarded > 0">
      <p class="sub" data-testid="discarded">형식 오류로 폐기한 IF-03 메시지 {{ view.discarded }}개</p>
    </section>
  </aside>
</template>

<style scoped>
.panel { background: var(--panel); border-left: 1px solid var(--line); padding: 12px; overflow-y: auto; }
.dim { opacity: 0.45; }
h2 { font-size: 13px; color: var(--muted); margin: 8px 0; letter-spacing: 0.05em; }
.old { color: var(--warn); }
section + section { border-top: 1px solid var(--line); margin-top: 8px; }
.row { display: flex; flex-wrap: wrap; align-items: baseline; gap: 8px; padding: 4px 0; }
.label { width: 76px; flex-shrink: 0; color: var(--muted); }
.badge { font-weight: 700; padding: 1px 6px; border-radius: 3px; }
.badge.ok { background: var(--ok); color: #08130c; }
.badge.warn { background: var(--warn); color: #1d1503; }
.badge.bad { background: var(--bad); color: #fff; }
.sub { color: var(--muted); font-size: 12px; width: 100%; padding-left: 84px; }
p.sub { padding-left: 0; }
.ok { color: var(--ok); }
.warn { color: var(--warn); }
.bad { color: var(--bad); }
.mono { font-family: ui-monospace, monospace; }
.flags { font-size: 12px; }
</style>
