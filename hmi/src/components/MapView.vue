<script setup lang="ts">
// 지도: 차량의 마지막 유효 위치(진행 방향 화살표)와 궤적 구간들 (REQ-HMI-001).
// 타일은 VITE_TILE_URL이 있을 때만 쓴다. 없으면 격자 배경만 (오프라인 타일은 REQ-HMI-005 고도화).
import { onBeforeUnmount, onMounted, ref, watch } from "vue";
import L from "leaflet";
import type { Fix, LatLon } from "../state/hmiState";
import { EMPTY_DRAWN, planDraw, type Drawn } from "../map/trackSync";

const props = defineProps<{ fix: Fix | null; fixValid: boolean; track: LatLon[][]; stale: boolean }>();

const el = ref<HTMLDivElement | null>(null);
const follow = ref(true);
// 시연 점검용: Leaflet 궤적 레이어에 실제로 들어 있는 점 수 (조각 이음점 포함, 렌더러와 무관).
// 그릴 때마다 세지 않고, 개발 서버에서만 점검 스크립트가 부를 수 있게 둔다 (e2e/smoke.mjs)
function countLayerPoints(): number {
  let n = 0;
  trackLayer?.eachLayer((l) => {
    if (l instanceof L.Polyline) n += l.getLatLngs().flat(2).length;
  });
  return n;
}
let map: L.Map | null = null;
let marker: L.Marker | null = null;
let trackLayer: L.LayerGroup | null = null;
let lastLine: L.Polyline | null = null; // 점을 덧붙일 마지막 선 조각
let drawn: Drawn = EMPTY_DRAWN;
const LINE_STYLE: L.PolylineOptions = { color: "#4aa3ff", weight: 3, interactive: false };

const ICON = L.divIcon({ className: "vehicle-icon", html: '<div class="arrow"></div>', iconSize: [24, 24], iconAnchor: [12, 12] });

onMounted(() => {
  map = L.map(el.value!, { zoomControl: true, attributionControl: false }).setView([36.1234567, 127.7654321], 17);
  const tiles = import.meta.env.VITE_TILE_URL;
  if (tiles) L.tileLayer(tiles, { maxZoom: 19 }).addTo(map);
  trackLayer = L.layerGroup().addTo(map);
  map.on("dragstart", () => (follow.value = false)); // 운용자가 지도를 옮기면 따라가기를 끈다
  if (import.meta.env.DEV) window.__ugvcTrackPoints = countLayerPoints;
});

onBeforeUnmount(() => {
  if (import.meta.env.DEV && window.__ugvcTrackPoints === countLayerPoints) delete window.__ugvcTrackPoints;
  map?.remove();
});

// 구간마다 따로 그린다 (끊긴 곳을 직선으로 잇지 않음). 무엇을 그릴지는 trackSync.planDraw가 정하고 여기서는 실행만
watch(
  () => props.track,
  (track) => {
    if (!trackLayer) return;
    const plan = planDraw(drawn, track);
    drawn = plan.drawn;
    for (const op of plan.ops) {
      switch (op.op) {
        case "clear":
          trackLayer.clearLayers();
          lastLine = null;
          break;
        case "newLine":
          lastLine = L.polyline(op.points, LINE_STYLE).addTo(trackLayer);
          break;
        case "extend":
          if (!lastLine) throw new Error("trackSync: extend without a line"); // 계획과 실행이 어긋남 (결함)
          lastLine.addLatLng(op.point);
          break;
      }
    }
  },
);

watch(
  () => [props.fix, props.fixValid, props.stale] as const,
  ([fix, valid, stale]) => {
    if (!map || !fix) return;
    const pos: L.LatLngExpression = [fix.lat, fix.lon];
    if (!marker) marker = L.marker(pos, { icon: ICON, interactive: false }).addTo(map);
    else marker.setLatLng(pos);
    const node = marker.getElement();
    if (node) {
      // 아이콘을 다시 만들지 않고 회전·상태 클래스만 바꾼다
      (node.firstElementChild as HTMLElement | null)?.style.setProperty("transform", `rotate(${fix.heading_deg}deg)`);
      node.classList.toggle("stale", stale);
      node.classList.toggle("no-fix", !valid);
    }
    if (follow.value) map.panTo(pos, { animate: false });
  },
);

function recenter() {
  follow.value = true;
  if (map && props.fix) map.panTo([props.fix.lat, props.fix.lon]);
}
</script>

<template>
  <div class="wrap">
    <div ref="el" class="map-canvas" data-testid="map" :data-heading="fix?.heading_deg ?? ''"></div>
    <div v-if="fix && !fixValid" class="note" data-testid="no-fix">위치 무효 · 마지막 유효 위치 표시 중</div>
    <button v-if="!follow" class="follow" @click="recenter">차량 따라가기</button>
  </div>
</template>

<style scoped>
.wrap { position: relative; min-height: 300px; }
.map-canvas {
  position: absolute; inset: 0;
  background-color: #1b232b;
  background-image:
    linear-gradient(#26313b 1px, transparent 1px),
    linear-gradient(90deg, #26313b 1px, transparent 1px);
  background-size: 40px 40px;
}
.follow { position: absolute; z-index: 1000; right: 12px; bottom: 12px; padding: 6px 10px; }
.note {
  position: absolute; z-index: 1000; left: 50%; top: 12px; transform: translateX(-50%);
  background: #3a2f12; color: var(--warn); padding: 4px 10px; border-radius: 3px; font-size: 13px;
}
:deep(.vehicle-icon .arrow) {
  width: 24px; height: 24px;
  background: #ffd23f;
  clip-path: polygon(50% 0, 90% 100%, 50% 75%, 10% 100%);
}
:deep(.vehicle-icon.stale) { opacity: 0.4; }
:deep(.vehicle-icon.no-fix .arrow) { background: #8a97a5; }
</style>
