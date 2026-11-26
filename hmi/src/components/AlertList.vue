<script setup lang="ts">
// 경고 목록 (REQ-HMI-003). 어떤 경고가 어떤 상태인지는 state/alerts.ts가 정하고, 여기서는 등급별 색으로 그리기만 한다.
// App.vue가 등급마다 놓을 자리를 정한다: WARNING·CAUTION은 배너 아래 전체 폭, ADVISORY는 오른쪽 패널 위.
import type { AlertRow, AlertState } from "../state/alerts";

defineProps<{ rows: AlertRow[] }>();
const emit = defineEmits<{ ack: [id: number] }>();

const STATE_TEXT: Record<AlertState, string> = { ACTIVE: "", CLEARED: "해제됨", ACKED: "확인됨" };
</script>

<template>
  <ul v-if="rows.length" class="alerts">
    <li
      v-for="a in rows"
      :key="a.id"
      :class="[a.level, a.state]"
      data-testid="alert"
      :data-kind="a.kind"
      :data-level="a.level"
      :data-state="a.state"
    >
      <span class="level">{{ a.level }}</span>
      <span class="text">{{ a.text }}</span>
      <span v-if="STATE_TEXT[a.state]" class="state">{{ STATE_TEXT[a.state] }}</span>
      <button v-if="a.ackable" data-testid="alert-ack" @click="emit('ack', a.id)">확인</button>
    </li>
  </ul>
</template>

<style scoped>
.alerts { list-style: none; margin: 0; padding: 0; }
li { display: flex; align-items: center; gap: 10px; padding: 4px 12px; border-bottom: 1px solid var(--line); font-size: 13px; }
.level { font-weight: 700; font-size: 11px; padding: 1px 6px; border-radius: 3px; min-width: 72px; text-align: center; }
.text { flex: 1; }
.state { color: var(--muted); font-size: 12px; }
button { background: #2b3540; color: var(--text); border: 1px solid var(--line); padding: 2px 10px; border-radius: 3px; cursor: pointer; }
.WARNING { background: #3d1614; }
.WARNING .level { background: var(--bad); color: #fff; }
.WARNING.ACTIVE .text { color: #fff; font-weight: 700; }
.CAUTION { background: #3a2f12; }
.CAUTION .level { background: var(--warn); color: #1d1503; }
.CAUTION.ACTIVE .text { color: var(--warn); }
.ADVISORY { background: var(--panel); }
.ADVISORY .level { background: var(--advisory); color: #06121c; }
.CLEARED .text, .ACKED .text { opacity: 0.7; }
</style>
