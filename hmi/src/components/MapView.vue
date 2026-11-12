<script setup lang="ts">
// 지도: 차량의 마지막 유효 위치(진행 방향 화살표)와 궤적 구간들 (REQ-HMI-001).
// 타일은 VITE_TILE_URL이 있을 때만 쓴다. 없으면 격자 배경만 (오프라인 타일은 REQ-HMI-005 고도화).
import { onBeforeUnmount, onMounted, ref, watch } from "vue";
import L from "leaflet";
import type { Fix, LatLon } from "../state/hmiState";
import { CHUNK_POINTS, chunk, planTrackUpdate } from "../map/trackSync";

const props = defineProps<{ fix: Fix | null; fixValid: boolean; track: LatLon[][]; stale: boolean }>();

const el = ref<HTMLDivElement | null>(null);
const follow = ref(true);
let map: L.Map | null = null;
let marker: L.Marker | null = null;
let trackLayer: L.LayerGroup | null = null;
let lines: L.Polyline[] = []; // 궤적 조각들 (CHUNK_POINTS점 이하). 마지막 것에만 점을 덧붙인다
let lastChunkPoints = 0;
let drawnTrack: LatLon[][] = [];
const LINE_STYLE: L.PolylineOptions = { color: "#4aa3ff", weight: 3, interactive: false };

const ICON = L.divIcon({ className: "vehicle-icon", html: '<div class="arrow"></div>', iconSize: [24, 24], iconAnchor: [12, 12] });

onMounted(() => {
  map = L.map(el.value!, { zoomControl: true, attributionControl: false }).setView([36.1234567, 127.7654321], 17);
  const tiles = import.meta.env.VITE_TILE_URL;
  if (tiles) L.tileLayer(tiles, { maxZoom: 19 }).addTo(map);
  trackLayer = L.layerGroup().addTo(map);
  map.on("dragstart", () => (follow.value = false)); // 운용자가 지도를 옮기면 따라가기를 끈다
});

onBeforeUnmount(() => map?.remove());

function newLine(points: LatLon[]): void {
  lines.push(L.polyline(points, LINE_STYLE).addTo(trackLayer!));
  lastChunkPoints = points.length;
}

// 구간마다 따로 그린다 (끊긴 곳을 직선으로 잇지 않음). 할 일은 trackSync가 정한다
watch(
  () => props.track,
  (track) => {
    if (!trackLayer) return;
    const plan = planTrackUpdate(drawnTrack, track);
    drawnTrack = track;
    switch (plan.kind) {
      case "none":
        return;
      case "append":
        if (lastChunkPoints < CHUNK_POINTS) {
          lines[lines.length - 1]!.addLatLng(plan.point);
          lastChunkPoints += 1;
        } else {
          const prev = lines[lines.length - 1]!.getLatLngs() as L.LatLng[];
          const end = prev[prev.length - 1]!;
          newLine([[end.lat, end.lng], plan.point]); // 앞 조각의 끝점부터 이어서
        }
        return;
      case "newSegment":
        newLine([plan.point]);
        return;
      case "rebuild":
        trackLayer.clearLayers();
        lines = [];
        lastChunkPoints = 0;
        for (const seg of track) for (const part of chunk(seg)) newLine(part);
        return;
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
    <div ref="el" class="map-canvas" data-testid="map"></div>
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
