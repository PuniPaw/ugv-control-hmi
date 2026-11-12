<script setup lang="ts">
// HMI 최상위: IF-03 연결 → receiveText(경계 검증 + 순수 상태 갱신) → view(표시 정책) → 화면.
// 실제 시계(Date.now)와 WebSocket은 이 파일에만 있다.
import { computed, onBeforeUnmount, onMounted, shallowRef } from "vue";
import {
  connectionLost, connectionOpened, freshness, gpsValid, initialState, needsResync, receiveText, type HmiState,
} from "./state/hmiState";
import { banner, staleness, toConnView, type ConnView } from "./state/view";
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
      state.value = receiveText(state.value, text, Date.now());
    },
    onState(ev) {
      if (ev.state === "OPEN") state.value = connectionOpened(state.value, Date.now());
      if (ev.state === "WAITING_RETRY") {
        console.warn(`IF-03 연결 끊김 (${ev.reason}), ${ev.retryInMs}ms 후 재접속`);
        state.value = connectionLost(state.value);
      }
      conn.value = toConnView(ev, Date.now());
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
  clock = window.setInterval(() => {
    now.value = Date.now();
    // 데이터가 기준선보다 계속 늦게 옴(시계 계단 변화, HMI 적체 등) → 다시 접속해 기준선을 새로 잡는다 (ICD 7장)
    if (needsResync(state.value, now.value)) connection.reconnect("data late (resync)");
  }, 250);
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
