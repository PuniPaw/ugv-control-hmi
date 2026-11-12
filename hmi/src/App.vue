<script setup lang="ts">
// HMI 최상위: HmiController(연결 + 상태 + 주기 판정)를 만들고, 상태가 바뀌면 화면을 다시 그린다.
// 실제 시계(Date.now)와 WebSocket은 이 파일에서 주입한다.
import { computed, onBeforeUnmount, onMounted, shallowRef } from "vue";
import { HmiController, type Snapshot } from "./app/hmiController";
import { freshness, gpsValid } from "./state/hmiState";
import { banner, staleness } from "./state/view";
import { browserSocket, defaultGwUrl } from "./net/gwConnection";
import ConnectionBanner from "./components/ConnectionBanner.vue";
import MapView from "./components/MapView.vue";
import StatusPanel from "./components/StatusPanel.vue";

const gwUrl = defaultGwUrl(window.location.hostname, import.meta.env.VITE_GW_URL);

const controller = new HmiController(
  gwUrl,
  {
    now: () => Date.now(),
    openSocket: browserSocket,
    setTimer: (fn, ms) => window.setTimeout(fn, ms),
    clearTimer: (h) => window.clearTimeout(h as number),
  },
  (s) => (snap.value = s),
  (msg) => console.warn(msg),
);

// 상태는 매번 새 객체로 바뀐다 → 깊은 반응성 대신 shallowRef로 통째 교체
const snap = shallowRef<Snapshot>(controller.snapshot);
const now = shallowRef(Date.now()); // 신선도·경과 시간 판정용 (메시지가 끊겨도 STALE로 넘어가야 한다)

const state = computed(() => snap.value.state);
const fresh = computed(() => freshness(state.value, now.value));
const bannerView = computed(() => banner(snap.value.conn, fresh.value, now.value));
const stale = computed(() => staleness(state.value, snap.value.conn, fresh.value));
const fixValid = computed(() => state.value.vehicle !== null && gpsValid(state.value.vehicle));

let clock = 0;
onMounted(() => {
  controller.start();
  clock = window.setInterval(() => {
    now.value = Date.now();
    controller.tick();
  }, 250);
});
onBeforeUnmount(() => {
  controller.stop();
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
