<script setup lang="ts">
// 상태 패널: 송신원별 링크 상태·10초 유실률, 차량 상태 (REQ-HMI-002). 표시 정책은 state/view.ts.
import { computed } from "vue";
import { SOURCES, type LinkState, type Source } from "../if03/messages";
import type { HmiState } from "../state/hmiState";
import { batteryLevel, linkAgeText, lossText, positionText } from "../state/view";

const props = defineProps<{ state: HmiState; now: number; stale: { all: boolean; vehicle: boolean } }>();

const SOURCE_LABEL: Record<Source, string> = { VEHICLE: "차량", DETECTOR: "탐지 노드" };
const LINK_CLASS: Record<LinkState, string> = { CONNECTED: "ok", DEGRADED: "warn", LOST: "bad" };

const v = computed(() => props.state.vehicle);
</script>

<template>
  <aside class="panel" data-testid="status-panel" :data-stale="stale.all">
    <section :class="{ dim: stale.all }">
      <h2>링크</h2>
      <div v-for="src in SOURCES" :key="src" class="row" :data-testid="`link-${src}`">
        <span class="label">{{ SOURCE_LABEL[src] }}</span>
        <span class="badge" :class="LINK_CLASS[state.link[src].state]" :data-testid="`link-state-${src}`">{{ state.link[src].state }}</span>
        <span class="sub">
          유실 <span :data-testid="`loss-${src}`">{{ lossText(state.stats[src]) }}</span> ·
          <span :data-testid="`age-${src}`">{{ linkAgeText(state, src, now) }}</span>
        </span>
      </div>
    </section>

    <section data-testid="vehicle" :class="{ dim: stale.vehicle }" :data-stale="stale.vehicle">
      <h2>차량<span v-if="v && stale.vehicle" class="old"> · 이전 값</span></h2>
      <template v-if="v">
        <div class="row"><span class="label">모드</span><span data-testid="mode">{{ v.mode }}</span></div>
        <div class="row"><span class="label">배터리</span><span :class="batteryLevel(v)" data-testid="battery">{{ v.battery_pct }}%</span></div>
        <div class="row"><span class="label">속도</span><span>{{ v.speed_mps.toFixed(1) }} m/s</span></div>
        <div class="row"><span class="label">방향</span><span>{{ v.heading_deg.toFixed(1) }}°</span></div>
        <div class="row"><span class="label">위치</span><span class="mono" data-testid="position">{{ positionText(v) }}</span></div>
        <div class="row"><span class="label">상태</span><span class="flags">{{ v.flags.join(" · ") || "—" }}</span></div>
      </template>
      <p v-else class="sub">텔레메트리 수신 전</p>
    </section>

    <section v-if="state.rejected > 0">
      <p class="sub" data-testid="rejected">형식 오류로 버린 IF-03 메시지 {{ state.rejected }}개</p>
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
