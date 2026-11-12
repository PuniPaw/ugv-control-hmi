<script setup lang="ts">
// 지도: 차량 위치(진행 방향 화살표)와 궤적 (REQ-HMI-001).
// 타일은 VITE_TILE_URL이 있을 때만 쓴다. 없으면 격자 배경만 (오프라인 타일은 REQ-HMI-005 고도화).
import { onBeforeUnmount, onMounted, ref, watch } from "vue";
import L from "leaflet";
import type { TelemetryData } from "../if03/messages";

const props = defineProps<{ vehicle: TelemetryData | null; track: Array<[number, number]> }>();

const el = ref<HTMLDivElement | null>(null);
const follow = ref(true);
let map: L.Map | null = null;
let marker: L.Marker | null = null;
let line: L.Polyline | null = null;

function arrowIcon(headingDeg: number): L.DivIcon {
  return L.divIcon({
    className: "vehicle-icon",
    html: `<div class="arrow" style="transform: rotate(${headingDeg}deg)"></div>`,
    iconSize: [24, 24],
    iconAnchor: [12, 12],
  });
}

onMounted(() => {
  map = L.map(el.value!, { zoomControl: true, attributionControl: false }).setView([36.1234567, 127.7654321], 17);
  const tiles = import.meta.env.VITE_TILE_URL;
  if (tiles) L.tileLayer(tiles, { maxZoom: 19 }).addTo(map);
  line = L.polyline([], { color: "#4aa3ff", weight: 3 }).addTo(map);
  map.on("dragstart", () => (follow.value = false)); // 운용자가 지도를 옮기면 따라가기를 끈다
});

onBeforeUnmount(() => map?.remove());

watch(
  () => props.track,
  (track) => line?.setLatLngs(track),
);

watch(
  () => props.vehicle,
  (v) => {
    if (!map || !v) return;
    const pos: L.LatLngExpression = [v.lat, v.lon];
    if (!marker) marker = L.marker(pos, { icon: arrowIcon(v.heading_deg) }).addTo(map);
    else {
      marker.setLatLng(pos);
      marker.setIcon(arrowIcon(v.heading_deg));
    }
    if (follow.value) map.panTo(pos, { animate: false });
  },
);

function recenter() {
  follow.value = true;
  if (map && props.vehicle) map.panTo([props.vehicle.lat, props.vehicle.lon]);
}
</script>

<template>
  <div class="wrap">
    <div ref="el" class="map-canvas" data-testid="map"></div>
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
:deep(.vehicle-icon .arrow) {
  width: 24px; height: 24px;
  background: #ffd23f;
  clip-path: polygon(50% 0, 90% 100%, 50% 75%, 10% 100%);
}
</style>
