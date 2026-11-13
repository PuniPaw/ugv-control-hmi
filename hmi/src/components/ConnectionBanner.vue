<script setup lang="ts">
// GW 연결·데이터 신선도 표시. 문구와 등급은 state/view.ts의 banner()가 정한다.
import type { Level } from "../state/view";

defineProps<{ level: Level; text: string; gwUrl: string }>();
</script>

<template>
  <div class="banner" :class="level" data-testid="connection-banner" :data-level="level">
    <span class="title">UGVC 관제</span>
    <span class="status" data-testid="banner-text">{{ text }}</span>
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
