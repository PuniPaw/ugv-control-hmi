/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** IF-03 주소. 기본 ws://<페이지 호스트>:8080/ws */
  readonly VITE_GW_URL?: string;
  /** 지도 타일 URL 템플릿. 비우면 타일 없이 격자만 (오프라인 타일은 REQ-HMI-005, 고도화) */
  readonly VITE_TILE_URL?: string;
}
interface ImportMeta {
  readonly env: ImportMetaEnv;
}

declare module "*.vue" {
  import type { DefineComponent } from "vue";
  const component: DefineComponent<object, object, unknown>;
  export default component;
}

// 개발 서버 전용 시연 점검 훅 (components/MapView.vue, e2e/smoke.mjs)
interface Window {
  __ugvcTrackPoints?: () => number;
}
