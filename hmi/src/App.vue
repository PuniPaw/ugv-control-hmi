<script setup lang="ts">
// HMI 최상위: IF-03 연결 → parseIf03(경계 검증) → apply(순수 상태 갱신) → 화면.
import { computed, onBeforeUnmount, onMounted, shallowRef } from "vue";
import { parseIf03 } from "./if03/messages";
import { apply, freshness, initialState, type HmiState } from "./state/hmiState";
import { GwConnection, browserSocket, defaultGwUrl, type ConnectionState } from "./net/gwConnection";
import ConnectionBanner from "./components/ConnectionBanner.vue";
import MapView from "./components/MapView.vue";
import StatusPanel from "./components/StatusPanel.vue";

const gwUrl = defaultGwUrl(window.location.hostname, import.meta.env.VITE_GW_URL);

// HmiState는 apply()가 매번 새 객체로 바꾼다 → 깊은 반응성 대신 shallowRef로 통째 교체
const state = shallowRef<HmiState>(initialState());
const conn = shallowRef<{ state: ConnectionState; retryInMs?: number }>({ state: "CONNECTING" });
const now = shallowRef(Date.now()); // 신선도 판정용 시계 (메시지가 끊겨도 STALE로 넘어가야 한다)

const fresh = computed(() => freshness(state.value, now.value));

const connection = new GwConnection(
  gwUrl,
  {
    onText(text) {
      const msg = parseIf03(text);
      if (msg === null) {
        state.value = { ...state.value, rejected: state.value.rejected + 1 };
        return;
      }
      state.value = apply(state.value, msg, Date.now());
    },
    onState(s, detail) {
      conn.value = { state: s, retryInMs: detail.retryInMs };
    },
  },
  {
    openSocket: browserSocket,
    setTimer: (fn, ms) => window.setTimeout(fn, ms),
    clearTimer: (h) => window.clearTimeout(h as number),
  },
);

let clock = 0;
onMounted(() => {
  connection.start();
  clock = window.setInterval(() => (now.value = Date.now()), 250);
});
onBeforeUnmount(() => {
  connection.stop();
  window.clearInterval(clock);
});
</script>

<template>
  <div class="layout">
    <ConnectionBanner class="banner" :connection="conn.state" :retry-in-ms="conn.retryInMs" :freshness="fresh" :gw-url="gwUrl" />
    <MapView class="map" :vehicle="state.vehicle" :track="state.track" />
    <StatusPanel class="panel" :state="state" :now="now" :stale="conn.state !== 'OPEN' || fresh !== 'FRESH'" />
  </div>
</template>

<style scoped>
.layout {
  display: grid;
  grid-template: "banner banner" auto "map panel" 1fr / 1fr 320px;
  height: 100%;
}
.banner { grid-area: banner; }
.map { grid-area: map; }
.panel { grid-area: panel; }
@media (max-width: 800px) {
  .layout { grid-template: "banner" auto "map" 55vh "panel" auto / 1fr; height: auto; min-height: 100%; }
}
</style>
