<script setup lang="ts">
// HMI 최상위: HmiController(연결 + 상태 + 주기 판정 + 화면 판정)를 만들고 view()를 그리기만 한다.
// 실제 시계(Date.now, performance.now)와 WebSocket은 이 파일에서 주입한다.
import { computed, onBeforeUnmount, onMounted, shallowRef } from "vue";
import { HmiController, type View } from "./app/hmiController";
import { browserSocket, defaultGwUrl } from "./net/gwConnection";
import ConnectionBanner from "./components/ConnectionBanner.vue";
import MapView from "./components/MapView.vue";
import StatusPanel from "./components/StatusPanel.vue";

const gwUrl = defaultGwUrl(window.location.hostname, import.meta.env.VITE_GW_URL);

const controller = new HmiController(
  gwUrl,
  {
    now: () => Date.now(),
    monotonic: () => performance.now(),
    openSocket: browserSocket,
    setTimer: (fn, ms) => window.setTimeout(fn, ms),
    clearTimer: (h) => window.clearTimeout(h as number),
  },
  () => {
    view.value = controller.view();
    now.value = Date.now();
  },
  (msg) => console.warn(msg),
);

// 매번 새 객체 → 깊은 반응성 대신 shallowRef로 통째 교체
const view = shallowRef<View>(controller.view());
const now = shallowRef(Date.now()); // 경과 시간 표시용 (view와 같은 시각)
const state = computed(() => view.value.state);

let clock = 0;
onMounted(() => {
  controller.start();
  clock = window.setInterval(() => controller.tick(), 250); // tick이 판정 후 view를 갱신한다
});
onBeforeUnmount(() => {
  controller.stop();
  window.clearInterval(clock);
});
</script>

<template>
  <div class="layout">
    <ConnectionBanner class="banner" :level="view.banner.level" :text="view.banner.text" :gw-url="gwUrl" />
    <MapView class="map" :fix="state.fix" :fix-valid="view.fixValid" :track="state.track" :stale="view.stale.vehicle" />
    <StatusPanel class="panel" :state="state" :now="now" :stale="view.stale" />
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
