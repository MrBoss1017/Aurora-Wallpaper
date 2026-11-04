(() => {
  'use strict';
  const canvas = document.getElementById('scene');
  const ctx = canvas.getContext('2d', { alpha: false });
  let width = innerWidth, height = innerHeight, ratio = 1;
  let state = window.wallpaperState || { paused: false, muted: true, volume: 0, fps: 60 };
  let last = 0, time = 0, pointer = { x: 0.5, y: 0.5 };
  const ripples = [];
  const stars = Array.from({ length: 100 }, (_, i) => ({
    x: ((i * 7919) % 1000) / 1000,
    y: ((i * 3929) % 1000) / 1000,
    size: 0.4 + (i % 5) * 0.22,
  }));
  function resize() {
    width = innerWidth; height = innerHeight;
    ratio = Math.min(devicePixelRatio || 1, 1.5);
    canvas.width = Math.round(width * ratio); canvas.height = Math.round(height * ratio);
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  }
  addEventListener('resize', resize);
  addEventListener('wallpaperstate', event => { state = event.detail; last = 0; });
  addEventListener('pointermove', event => { pointer = { x: event.clientX / width, y: event.clientY / height }; });
  addEventListener('click', event => {
    if (ripples.length >= 10) ripples.shift();
    ripples.push({ x: event.clientX, y: event.clientY, age: 0 });
  });
  function draw(now) {
    requestAnimationFrame(draw);
    if (state.paused) { last = 0; return; }
    const interval = 1000 / Math.max(1, Math.min(120, state.fps || 60));
    if (last && now - last < interval) return;
    const dt = last ? Math.min(0.1, (now - last) / 1000) : 0;
    last = now; time += dt;
    ctx.fillStyle = '#070d19'; ctx.fillRect(0, 0, width, height);
    const centerX = width * (0.5 + (pointer.x - 0.5) * 0.025);
    const centerY = height * (0.5 + (pointer.y - 0.5) * 0.025);
    const radius = Math.min(width, height) * 0.23;
    const glow = ctx.createRadialGradient(centerX, centerY, 0, centerX, centerY, radius * 2.1);
    glow.addColorStop(0, '#143e4e'); glow.addColorStop(0.55, '#10213a'); glow.addColorStop(1, '#070d19');
    ctx.fillStyle = glow; ctx.fillRect(0, 0, width, height);
    for (const star of stars) {
      ctx.fillStyle = `rgba(210,234,255,${0.18 + 0.17 * (1 + Math.sin(time * 0.4 + star.x * 20))})`;
      ctx.beginPath(); ctx.arc(star.x * width, star.y * height, star.size, 0, Math.PI * 2); ctx.fill();
    }
    for (let ring = 0; ring < 5; ring++) {
      const r = radius * (0.72 + ring * 0.21);
      ctx.save(); ctx.translate(centerX, centerY); ctx.rotate(-0.36 + ring * 0.12);
      ctx.strokeStyle = `rgba(113,219,207,${0.1 + ring * 0.018})`; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.ellipse(0, 0, r * 1.4, r * 0.6, 0, 0, Math.PI * 2); ctx.stroke();
      const angle = time * (0.07 + ring * 0.017) + ring * 1.2;
      const x = Math.cos(angle) * r * 1.4, y = Math.sin(angle) * r * 0.6;
      ctx.shadowColor = ring % 2 ? '#a794ff' : '#71dbcf'; ctx.shadowBlur = 16;
      ctx.fillStyle = ring % 2 ? '#a794ff' : '#a0f1dd';
      ctx.beginPath(); ctx.arc(x, y, 2.6 + ring * 0.25, 0, Math.PI * 2); ctx.fill(); ctx.restore();
    }
    for (let i = ripples.length - 1; i >= 0; i--) {
      const ripple = ripples[i]; ripple.age += dt;
      if (ripple.age >= 2) { ripples.splice(i, 1); continue; }
      ctx.strokeStyle = `rgba(130,232,220,${(1 - ripple.age / 2) * 0.4})`;
      ctx.beginPath(); ctx.arc(ripple.x, ripple.y, 12 + ripple.age * 90, 0, Math.PI * 2); ctx.stroke();
    }
  }
  resize(); requestAnimationFrame(draw);
})();
