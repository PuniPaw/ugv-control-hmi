<script setup lang="ts">
// 명령 패널 (REQ-CMD-001·004): 정지·복귀·경로점 이동. 버튼은 확인 절차만 시작하고, "보내기"를 눌러야 보낸다.
// 보낼 수 있는지, 문구, 결과 표시는 state/view.ts의 commandView()가 정한다. 여기서는 그리기와 경로점 입력만 한다.
import { ref } from "vue";
import type { CommandSpec } from "../if03/messages";
import { waypointFromInput } from "../state/commands";
import type { CommandView } from "../state/view";

const props = defineProps<{ view: CommandView }>();
const emit = defineEmits<{ request: [spec: CommandSpec]; retry: [reqId: string]; cancel: []; confirm: [] }>();

const lat = ref("");
const lon = ref("");
const inputError = ref<string | null>(null);

function requestWaypoint(): void {
  const r = waypointFromInput(lat.value, lon.value);
  if ("error" in r) {
    inputError.value = r.error;
    return;
  }
  inputError.value = null;
  emit("request", r);
}

const disabled = () => props.view.blocked !== null;
</script>

<template>
  <section class="cmd" data-testid="cmd-panel" :data-blocked="view.blocked !== null">
    <h2>명령</h2>
    <p v-if="view.blocked" class="blocked" data-testid="cmd-blocked">{{ view.blocked }}</p>

    <div v-if="view.confirm" class="confirm" data-testid="cmd-confirm">
      <p>다음 명령을 보낼까요?</p>
      <p class="what" data-testid="cmd-confirm-text">{{ view.confirm.text }}</p>
      <p v-if="view.confirm.warning" class="blocked" data-testid="cmd-confirm-warning">{{ view.confirm.warning }}</p>
      <div class="buttons">
        <button class="send" data-testid="cmd-send" :disabled="disabled()" @click="emit('confirm')">보내기</button>
        <button data-testid="cmd-cancel" @click="emit('cancel')">취소</button>
      </div>
    </div>
    <template v-else>
      <div class="buttons">
        <button class="stop" data-testid="cmd-STOP" :disabled="disabled()" @click="emit('request', { cmd_type: 'STOP' })">정지</button>
        <button data-testid="cmd-RTB" :disabled="disabled()" @click="emit('request', { cmd_type: 'RTB' })">복귀</button>
      </div>
      <div class="wp">
        <input v-model="lat" data-testid="wp-lat" placeholder="위도 (도)" inputmode="decimal" />
        <input v-model="lon" data-testid="wp-lon" placeholder="경도 (도)" inputmode="decimal" />
        <button data-testid="cmd-WAYPOINT" :disabled="disabled()" @click="requestWaypoint">경로점 이동</button>
      </div>
      <p v-if="inputError" class="blocked" data-testid="wp-error">{{ inputError }}</p>
    </template>

    <ul class="log">
      <li v-for="row in view.rows" :key="row.reqId" data-testid="cmd-row" :data-status="row.status" :data-req-id="row.reqId">
        <span class="label">{{ row.label }}</span>
        <span :class="row.level" data-testid="cmd-row-text">{{ row.text }}</span>
        <button
          v-if="row.retryable && !view.confirm"
          data-testid="cmd-retry"
          :disabled="disabled()"
          @click="emit('retry', row.reqId)"
        >다시 보내기</button>
      </li>
    </ul>
  </section>
</template>

<style scoped>
.cmd { background: var(--panel); border-left: 1px solid var(--line); border-top: 1px solid var(--line); padding: 12px; }
h2 { font-size: 13px; color: var(--muted); margin: 0 0 8px; letter-spacing: 0.05em; }
.blocked { color: var(--warn); font-size: 12px; margin: 4px 0 8px; }
.buttons, .wp { display: flex; gap: 8px; margin-bottom: 8px; }
.wp input { width: 0; flex: 1; background: var(--bg); color: var(--text); border: 1px solid var(--line); padding: 4px 6px; }
button { background: #2b3540; color: var(--text); border: 1px solid var(--line); padding: 5px 10px; border-radius: 3px; cursor: pointer; }
button:disabled { opacity: 0.4; cursor: not-allowed; }
button.stop { background: var(--bad); color: #fff; }
button.send { background: var(--warn); color: #1d1503; font-weight: 700; }
.confirm { border: 1px solid var(--warn); padding: 8px; margin-bottom: 8px; }
.confirm p { margin: 0 0 6px; }
.what { font-weight: 700; }
.log { list-style: none; padding: 0; margin: 0; font-size: 12px; }
.log li { display: flex; flex-wrap: wrap; gap: 6px; align-items: baseline; padding: 4px 0; border-top: 1px solid var(--line); }
.log .label { width: 100%; }
.log button { padding: 1px 6px; font-size: 12px; }
.ok { color: var(--ok); }
.warn { color: var(--warn); }
.bad { color: var(--bad); }
</style>
