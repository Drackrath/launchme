// Optional videos. Put the files into public/media/ and rebuild; vite.config.ts checks which exist.
//   hero.mp4 (+ hero.webm)           a Play button over the main layout's screenshot
//   interface.mp4 (+ interface.webm) the big screen of layout v2 (a slideshow of screenshots until then)
declare const __MEDIA__: { heroMp4: boolean; heroWebm: boolean; interfaceMp4: boolean; interfaceWebm: boolean };

export type Video = { mp4?: string; webm?: string; poster: string };

export const HERO_VIDEO: Video | null =
  __MEDIA__.heroMp4 || __MEDIA__.heroWebm
    ? { mp4: __MEDIA__.heroMp4 ? "media/hero.mp4" : undefined, webm: __MEDIA__.heroWebm ? "media/hero.webm" : undefined, poster: "img/shots/shelves.webp" }
    : null;

export const INTERFACE_VIDEO: Video | null =
  __MEDIA__.interfaceMp4 || __MEDIA__.interfaceWebm
    ? { mp4: __MEDIA__.interfaceMp4 ? "media/interface.mp4" : undefined, webm: __MEDIA__.interfaceWebm ? "media/interface.webm" : undefined, poster: "img/shots/shelves-xl.webp" }
    : null;
