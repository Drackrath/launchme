import { GamepadLayer } from "./components/GamepadLayer";
import { LowMemProvider } from "./lib/lowmem";
import { makePage, PageProvider, type PageInfo } from "./lib/page";
import { useLiveRelease, type Release } from "./lib/release";
import { Bento } from "./sections/Bento";
import { Companion } from "./sections/Companion";
import { Faq, FinalCta, Footer, Install } from "./sections/Closing";
import { Gallery } from "./sections/Gallery";
import { Hero } from "./sections/Hero";
import { LayoutPicker } from "./sections/LayoutPicker";
import { LibraryViews } from "./sections/LibraryViews";
import { Nav } from "./sections/Nav";
import { Stats } from "./sections/Stats";
import { ChapterScroller, HeroV2, VideoStage } from "./sections/V2";

export type AppProps = { page: PageInfo; release: Release; dev?: boolean };

export function App({ page, release: baked, dev = false }: AppProps) {
  const release = useLiveRelease(baked);
  const value = makePage(page, release, dev);
  return (
    <PageProvider value={value}>
      <LowMemProvider>
        <div id="top" className="grain relative min-h-screen">
          <Nav />
          <main>
            {page.layout === "v1" ? (
              <>
                <Hero />
                <Stats />
                <LayoutPicker />
                <LibraryViews />
                <Bento />
                <Companion />
                <Gallery />
                <Install />
                <Faq />
                <FinalCta />
              </>
            ) : (
              <>
                <HeroV2 />
                <VideoStage />
                <Stats />
                <ChapterScroller />
                <LibraryViews />
                <Bento />
                <Companion />
                <Gallery />
                <Install />
                <Faq />
                <FinalCta />
              </>
            )}
          </main>
          <Footer />
          <GamepadLayer />
        </div>
      </LowMemProvider>
    </PageProvider>
  );
}
