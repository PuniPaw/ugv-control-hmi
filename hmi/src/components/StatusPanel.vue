<script setup lang="ts">
// 상태 패널: 송신원별 링크 상태·10초 유실률, 차량 상태 (REQ-HMI-002).
import { computed } from "vue";
import type { LinkState, Source } from "../if03/messages";
import type { HmiState } from "../state/hmiState";

// stale: GW 연결이 끊겼거나 데이터가 지연됨 → 마지막 값이 현재 값이 아니므로 흐리게 표시
const props = defineProps<{ state: HmiState; now: number; stale: boolean }>();

const SOURCES: Source[] = ["VEHICLE", "DETECTOR"];
const SOURCE_LABEL: Record<Source, string> = { VEHICLE: "차량", DETECTOR: "탐지 노드" };
const LINK_CLASS: Record<LinkState, string> = { CONNECTED: "ok", DEGRADED: "warn", LOST: "bad" };

function lossText(src: Source): string {
  const r = props.state.stats[src]?.loss_rate_10s;
  return r === undefined || r === null ? "—" : `${(r * 100).toFixed(1)}%`;
}

function ageText(ts: number | null): string {
  return ts === null ? "수신 없음" : `${((props.now - ts) / 1000).toFixed(1)}s 전`;
}

const v = computed(() => props.state.vehicle);
const batteryClass = computed(() => {
  const b = v.value?.battery_pct;
  if (b === undefined) return "";
  return b <= 20 ? "bad" : b <= 40 ? "warn" : "ok";
});
</script>

<template>
  <aside class="panel" :class="{ stale }" :data-stale="stale">
    <section>
      <h2>링크</h2>
      <div v-for="src in SOURCES" :key="src" class="row" :data-testid="`link-${src}`">
        <span class="label">{{ SOURCE_LABEL[src] }}</span>
        <span class="badge" :class="LINK_CLASS[state.link[src].state]" data-testid="link-state">{{ state.link[src].state }}</span>
        <span class="sub">유실 {{ lossText(src) }} · {{ ageText(state.link[src].lastRxTs) }}</span>
      </div>
    </section>

    <section data-testid="vehicle">
      <h2>차량</h2>
      <template v-if="v">
        <div class="row"><span class="label">모드</span><span data-testid="mode">{{ v.mode }}</span></div>
        <div class="row"><span class="label">배터리</span><span :class="batteryClass" data-testid="battery">{{ v.battery_pct }}%</span></div>
        <div class="row"><span class="label">속도</span><span>{{ v.speed_mps.toFixed(1) }} m/s</span></div>
        <div class="row"><span class="label">방향</span><span>{{ v.heading_deg.toFixed(1) }}°</span></div>
        <div class="row"><span class="label">위치</span><span class="mono">{{ v.lat.toFixed(6) }}, {{ v.lon.toFixed(6) }}</span></div>
        <div class="row"><span class="label">상태</span><span class="flags">{{ v.flags.join(" · ") || "—" }}</span></div>
      </template>
      <p v-else class="sub">텔레메트리 수신 전</p>
    </section>

    <section v-if="state.rejected > 0">
      <p class="sub">형식 오류로 버린 IF-03 메시지 {{ state.rejected }}개</p>
    </section>
  </aside>
</template>

<style scoped>
.panel.stale section { opacity: 0.45; }
.panel { background: var(--panel); border-left: 1px solid var(--line); padding: 12px; overflow-y: auto; }
h2 { font-size: 13px; color: var(--muted); margin: 8px 0; letter-spacing: 0.05em; }
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
