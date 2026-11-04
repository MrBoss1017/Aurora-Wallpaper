// Optional authoring smoke test: npm install --no-save playwright
// Then: npx playwright install chromium && node tests/web_smoke.cjs
// This checks the sample in Chromium, not WebView2/WorkerW integration.
const { chromium } = require('playwright');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const assert = require('node:assert/strict');
(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.addInitScript(() => {
      window.__paintCount = 0;
      const fill = CanvasRenderingContext2D.prototype.fillRect;
      CanvasRenderingContext2D.prototype.fillRect = function(...args) {
        window.__paintCount++; return fill.apply(this, args);
      };
    });
    await page.goto(pathToFileURL(path.resolve(__dirname, '../samples/web-orbit/index.html')).href);
    await page.waitForFunction(() => window.__paintCount >= 4);
    await page.mouse.move(850, 300);
    await page.mouse.click(850, 300);
    await page.evaluate(() => window.dispatchEvent(new CustomEvent('wallpaperstate', {
      detail: { paused: true, muted: true, volume: 0, fps: 15 },
    })));
    const paused = await page.locator('canvas').evaluate(canvas => canvas.toDataURL());
    await page.waitForTimeout(250);
    assert.equal(await page.locator('canvas').evaluate(canvas => canvas.toDataURL()), paused, 'paused sample must stop painting');
    await page.evaluate(() => {
      window.__paintCount = 0;
      window.dispatchEvent(new CustomEvent('wallpaperstate', {
        detail: { paused: false, muted: true, volume: 0, fps: 15 },
      }));
    });
    await page.waitForTimeout(1000);
    const paints = await page.evaluate(() => window.__paintCount);
    assert(paints >= 4 && paints <= 34, `15 FPS sample painted ${paints / 2} frames in approximately one second`);
    await page.setViewportSize({ width: 800, height: 600 });
    await page.waitForFunction(() => document.querySelector('canvas').width === Math.round(800 * Math.min(devicePixelRatio || 1, 1.5)));
    assert.deepEqual(errors, [], 'no JavaScript exceptions');
    if (process.env.AURORA_QA_SCREENSHOT) await page.screenshot({ path: process.env.AURORA_QA_SCREENSHOT });
    console.log(`Web sample passed: render, input, pause, resume, FPS (${paints / 2} frames), resize, no JS errors.`);
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
