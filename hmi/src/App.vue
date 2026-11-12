<script setup lang="ts">
// HMI 최상위: IF-03 연결 → parseIf03(경계 검증) → apply(순수 상태 갱신) → view(표시 정책) → 화면.
import { computed, onBeforeUnmount, onMounted, shallowRef } from "vue";
import { parseIf03 } from "./if03/messages";
import { apply, connectionLost, freshness, gpsValid, initialState, rejectedMessage, type HmiState } from "./state/hmiState";
import { banner, staleness, type ConnView } from "./state/view";
import { GwConnection, browserSocket, defaultGwUrl } from "./net/gwConnection";
import ConnectionBanner from "./components/ConnectionBanner.vue";
import MapView from "./components/MapView.vue";
import StatusPanel from "./components/StatusPanel.vue";

const gwUrl = defaultGwUrl(window.location.hostname, import.meta.env.VITE_GW_URL);

// HmiState는 apply()가 매번 새 객체로 바꾼다 → 깊은 반응성 대신 shallowRef로 통째 교체
const state = shallowRef<HmiState>(initialState());
const conn = shallowRef<ConnView>({ state: "CONNECTING", failures: 0, retryAt: null });
const now = shallowRef(Date.now()); // 신선도·경과 시간 판정용 (메시지가 끊겨도 STALE로 넘어가야 한다)

const fresh = computed(() => freshness(state.value, now.value));
const bannerView = computed(() => banner(conn.value, fresh.value, now.value));
const stale = computed(() => staleness(state.value, conn.value, fresh.value));
const fixValid = computed(() => state.value.vehicle !== null && gpsValid(state.value.vehicle));

const connection = new GwConnection(
  gwUrl,
  {
    onText(text) {
      const msg = parseIf03(text);
      state.value = msg === null ? rejectedMessage(state.value) : apply(state.value, msg, Date.now());
    },
    onState(s, d) {
      if (s === "WAITING_RETRY") {
        console.warn(`IF-03 연결 끊김 (${d.reason}), ${d.retryInMs}ms 후 재접속`);
        state.value = connectionLost(state.value);
      }
      conn.value = { state: s, failures: d.failures, retryAt: d.retryInMs === undefined ? null : Date.now() + d.retryInMs };
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
    <ConnectionBanner class="banner" :level="bannerView.level" :text="bannerView.text" :gw-url="gwUrl" />
    <MapView class="map" :fix="state.fix" :fix-valid="fixValid" :track="state.track" :stale="stale.vehicle" />
    <StatusPanel class="panel" :state="state" :now="now" :stale="stale" />
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
