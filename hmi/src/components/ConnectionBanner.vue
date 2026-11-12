<script setup lang="ts">
// GW 연결·데이터 신선도 표시 (ICD 7장). 정상일 때는 얇은 상태 줄, 문제가 있으면 경고 색.
import { computed } from "vue";
import type { ConnectionState } from "../net/gwConnection";
import type { Freshness } from "../state/hmiState";

const props = defineProps<{ connection: ConnectionState; retryInMs?: number; freshness: Freshness; gwUrl: string }>();

const view = computed(() => {
  if (props.connection !== "OPEN") {
    const retry = props.retryInMs !== undefined ? ` · ${props.retryInMs / 1000}s 후 재접속` : " · 접속 중";
    return { level: "bad", text: `GW 연결 끊김${retry}` };
  }
  if (props.freshness === "WAITING") return { level: "warn", text: "GW 연결됨 · 데이터 대기 중" };
  if (props.freshness === "STALE") return { level: "bad", text: "GW 데이터 지연 (2초 이상)" };
  return { level: "ok", text: "GW 연결됨" };
});
</script>

<template>
  <div class="banner" :class="view.level" data-testid="connection-banner" :data-level="view.level">
    <span class="title">UGVC 관제</span>
    <span class="status">{{ view.text }}</span>
    <span class="url">{{ gwUrl }}</span>
  </div>
</template>

<style scoped>
.banner { display: flex; gap: 16px; align-items: center; padding: 6px 12px; border-bottom: 1px solid var(--line); background: var(--panel); }
.title { font-weight: 700; }
.status { flex: 1; }
.url { color: var(--muted); font-size: 12px; }
.ok .status { color: var(--ok); }
.warn { background: #3a2f12; }
.warn .status { color: var(--warn); }
.bad { background: #3d1614; }
.bad .status { color: var(--bad); font-weight: 700; }
</style>
