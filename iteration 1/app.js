/**
 * CAPPY // THE MAKINGS OF A PERVERT DETECTOR
 * Interactive Companion Simulator & Critical Making Showcase Engine
 * Inspired by Moulik Nanda's Srishti A3 Presentation
 */

(function () {
  'use strict';

  // =========================================================================
  // AUDIO & HAPTIC SYNTHESIZER (Web Audio API)
  // =========================================================================
  let audioCtx = null;
  let audioEnabled = true;

  function initAudio() {
    if (!audioCtx) {
      const AudioContext = window.AudioContext || window.webkitAudioContext;
      audioCtx = new AudioContext();
    }
    if (audioCtx.state === 'suspended') {
      audioCtx.resume();
    }
  }

  function playHapticBuzz(pattern) {
    if (!audioEnabled) return;
    try {
      initAudio();
      if (!audioCtx) return;

      const now = audioCtx.currentTime;

      function createBuzz(startTime, duration, freq, gainVal) {
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'sawtooth';
        osc.frequency.setValueAtTime(freq, startTime);
        
        // Lowpass filter to simulate physical vibration motor
        const filter = audioCtx.createBiquadFilter();
        filter.type = 'lowpass';
        filter.frequency.setValueAtTime(150, startTime);

        gain.gain.setValueAtTime(0.001, startTime);
        gain.gain.linearRampToValueAtTime(gainVal, startTime + 0.015);
        gain.gain.setValueAtTime(gainVal, startTime + duration - 0.02);
        gain.gain.linearRampToValueAtTime(0.001, startTime + duration);

        osc.connect(filter);
        filter.connect(gain);
        gain.connect(audioCtx.destination);

        osc.start(startTime);
        osc.stop(startTime + duration);
      }

      if (pattern === 'single') {
        createBuzz(now, 0.12, 110, 0.25);
      } else if (pattern === 'double') {
        createBuzz(now, 0.075, 125, 0.3);
        createBuzz(now + 0.14, 0.075, 125, 0.3);
      } else if (pattern === 'rage') {
        createBuzz(now, 0.06, 140, 0.4);
        createBuzz(now + 0.10, 0.06, 145, 0.4);
        createBuzz(now + 0.20, 0.09, 150, 0.45);
      } else if (pattern === 'knock') {
        // Physical hollow wooden / 3D-printed enclosure knock sound
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'triangle';
        osc.frequency.setValueAtTime(240, now);
        osc.frequency.exponentialRampToValueAtTime(50, now + 0.08);
        gain.gain.setValueAtTime(0.35, now);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.08);
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.start(now);
        osc.stop(now + 0.08);
      }
    } catch (e) {
      console.warn('Audio synthesis issue:', e);
    }
  }

  // =========================================================================
  // CAPPY GEOMETRY & STATE MACHINE
  // =========================================================================
  const CX = 120;
  const CY = 120;

  const EMOTIONS = {
    DEFAULT: 'DEFAULT',   // Happy / Safe (White pill eyes)
    CURIOUS: 'CURIOUS',   // Looking around for pervs (Yellow angled eyes)
    ANGRY: 'ANGRY',       // Gets angry at the pervs for you (Red inward slashes)
    RAGE: 'RAGE',         // Blood-red background + high-frequency eye tremor
    SLEEPY: 'SLEEPY',     // Drowsy after 30s safe
    SLEEPING: 'SLEEPING', // Deep sleep with floating vector Zzz
    DAZED: 'DAZED'        // Groggy knock wake-up sequence
  };

  function getTargetsForEmotion(emo) {
    switch (emo) {
      case EMOTIONS.RAGE:
        return {
          left:  { x: CX - 35, y: CY + 2,  w: 28, h: 58, r: 14, angle: 40,  color: '#610000' },
          right: { x: CX + 35, y: CY + 2,  w: 28, h: 58, r: 14, angle: -40, color: '#610000' },
          bgColor: '#ba3636'
        };
      case EMOTIONS.ANGRY:
        return {
          left:  { x: CX - 38, y: CY + 6,  w: 30, h: 60, r: 15, angle: 45,  color: '#eb2632' },
          right: { x: CX + 38, y: CY + 6,  w: 30, h: 60, r: 15, angle: -45, color: '#eb2632' },
          bgColor: '#000000'
        };
      case EMOTIONS.CURIOUS:
        return {
          left:  { x: CX + 4,  y: CY + 24, w: 26, h: 50, r: 13, angle: -30, color: '#ffd000' },
          right: { x: CX + 52, y: CY + 32, w: 22, h: 50, r: 11, angle: -30, color: '#ffd000' },
          bgColor: '#000000'
        };
      case EMOTIONS.DEFAULT:
        return {
          left:  { x: CX - 36, y: CY + 14, w: 32, h: 64, r: 16, angle: -5, color: '#ffffff' },
          right: { x: CX + 36, y: CY + 14, w: 32, h: 64, r: 16, angle: -5, color: '#ffffff' },
          bgColor: '#000000'
        };
      case EMOTIONS.SLEEPY:
        return {
          left:  { x: CX - 40, y: CY - 18, w: 52, h: 22, r: 11, angle: 0, color: '#ffffff' },
          right: { x: CX + 40, y: CY - 18, w: 52, h: 22, r: 11, angle: 0, color: '#ffffff' },
          bgColor: '#000000'
        };
      case EMOTIONS.SLEEPING:
        return {
          left:  { x: CX - 44, y: CY - 28, w: 64, h: 8,  r: 4,  angle: 0, color: '#ffffff' },
          right: { x: CX + 44, y: CY - 28, w: 64, h: 8,  r: 4,  angle: 0, color: '#ffffff' },
          bgColor: '#000000'
        };
      case EMOTIONS.DAZED:
        return {
          left:  { x: CX - 40, y: CY - 12, w: 48, h: 22, r: 11, angle: -3, color: '#ffffff' },
          right: { x: CX + 40, y: CY - 12, w: 48, h: 22, r: 11, angle: 3,  color: '#ffffff' },
          bgColor: '#000000'
        };
      default:
        return getTargetsForEmotion(EMOTIONS.DEFAULT);
    }
  }

  function hexToRgb(hex) {
    const c = hex.replace('#', '');
    const num = parseInt(c, 16);
    return { r: (num >> 16) & 255, g: (num >> 8) & 255, b: num & 255 };
  }

  function rgbToHex(r, g, b) {
    return '#' + ((1 << 24) + (Math.round(r) << 16) + (Math.round(g) << 8) + Math.round(b)).toString(16).slice(1);
  }

  function lerpColor(c1, c2, factor) {
    const rgb1 = hexToRgb(c1);
    const rgb2 = hexToRgb(c2);
    return rgbToHex(
      rgb1.r + (rgb2.r - rgb1.r) * factor,
      rgb1.g + (rgb2.g - rgb1.g) * factor,
      rgb1.b + (rgb2.b - rgb1.b) * factor
    );
  }

  // =========================================================================
  // SIMULATOR RUNTIME
  // =========================================================================
  const canvas = document.getElementById('companionCanvas');
  const ctx = canvas ? canvas.getContext('2d') : null;

  let currentEmotion = EMOTIONS.DEFAULT;
  let curL = Object.assign({}, getTargetsForEmotion(EMOTIONS.DEFAULT).left);
  let curR = Object.assign({}, getTargetsForEmotion(EMOTIONS.DEFAULT).right);
  let tgtL = Object.assign({}, curL);
  let tgtR = Object.assign({}, curR);
  let currentBgColor = '#000000';
  let targetBgColor = '#000000';

  // Saccades
  let saccadeX = 0, saccadeY = 0;
  let nextSaccadeTime = Date.now() + 2000;

  // Procedural blinking
  let isBlinking = false;
  let blinkStartTime = 0;
  let nextBlinkTime = Date.now() + 3000;
  let blinkScale = 1.0;

  // Dazed knock wake-up
  let dazedStartTime = 0;
  let dazedEndTime = 0;

  // Proximity & Signal Engine
  let rawRssi = -100;
  let filteredRssi = -100;
  let proximityTier = 'NONE';

  // Timing
  let lastFrameTime = performance.now();
  let frameCounter = 0;
  let fps = 30;
  let lastFpsCheck = performance.now();

  // DOM Elements
  const hudMood = document.getElementById('hudMood');
  const hudRssi = document.getElementById('hudRssi');
  const hudFps = document.getElementById('hudFps');
  const buzzerLed = document.getElementById('buzzerLed');
  const buzzerLabel = document.getElementById('buzzerLabel');
  const proximitySlider = document.getElementById('proximitySlider');
  const proximityValLabel = document.getElementById('proximityValLabel');
  const audioToggleBtn = document.getElementById('audioToggleBtn');
  const cappyChassis = document.getElementById('cappyChassis');
  const knockRipple = document.getElementById('knockRipple');
  const glassesToggleBtn = document.getElementById('glassesToggleBtn');
  let glassesActive = true;
  let lastActivityTime = performance.now();
  let sleepTransitionTimeout = null;

  // =========================================================================
  // HAPTIC UI INDICATOR
  // =========================================================================
  let hapticTimeout = null;
  function triggerHapticUI(pattern) {
    playHapticBuzz(pattern);
    if (!buzzerLed || !buzzerLabel) return;

    buzzerLed.className = 'buzzer-led-dot buzzing' + (pattern === 'rage' ? ' rage' : '');
    buzzerLabel.textContent = pattern === 'rage' ? 'TRIPLE RAGE BUZZ' : (pattern === 'double' ? 'DOUBLE BUZZ' : 'SINGLE BUZZ');

    clearTimeout(hapticTimeout);
    hapticTimeout = setTimeout(() => {
      buzzerLed.className = 'buzzer-led-dot';
      buzzerLabel.textContent = 'STANDBY';
    }, pattern === 'rage' ? 320 : (pattern === 'double' ? 220 : 130));
  }

  // =========================================================================
  // EMOTION CONTROLLER
  // =========================================================================
  function setCappyEmotion(emo, force = false) {
    if (currentEmotion === emo && !force) return;
    currentEmotion = emo;

    const targets = getTargetsForEmotion(emo);
    tgtL = Object.assign({}, targets.left);
    tgtR = Object.assign({}, targets.right);
    targetBgColor = targets.bgColor;

    // Update active button state
    document.querySelectorAll('.mood-btn, .cappy-action-btn').forEach(btn => {
      btn.classList.toggle('active', btn.dataset.emo === emo);
    });

    if (hudMood) {
      let displayName = emo;
      if (emo === EMOTIONS.DEFAULT) displayName = 'SAFE (HAPPY)';
      if (emo === EMOTIONS.CURIOUS) displayName = 'CURIOUS (LOOKING)';
      if (emo === EMOTIONS.ANGRY) displayName = 'ANGRY AT PERV';
      if (emo === EMOTIONS.RAGE) displayName = 'RAGE MODE!';
      if (emo === EMOTIONS.SLEEPY) displayName = 'SLEEPY (30s SAFE)';
      if (emo === EMOTIONS.SLEEPING) displayName = 'SLEEPING (Zzz)';
      if (emo === EMOTIONS.DAZED) displayName = 'WAKING UP (GROGGY)';

      hudMood.textContent = displayName;
      hudMood.style.color = 
        emo === EMOTIONS.RAGE ? 'var(--accent-red)' :
        emo === EMOTIONS.ANGRY ? 'var(--accent-red)' :
        emo === EMOTIONS.CURIOUS ? '#d49b00' :
        emo === EMOTIONS.DAZED ? 'var(--cappy-orange)' : 'var(--text-title)';
    }

    logSerial(`[Cappy] Expression changed &rarr; ${emo}`);
  }

  // =========================================================================
  // KNOCK & DOUBLE-TAP DETECTOR (ADXL345 Tap Interrupt)
  // =========================================================================
  function triggerSingleTap(source = 'Chassis Tap') {
    playHapticBuzz('knock');

    if (knockRipple) {
      knockRipple.classList.remove('pulse');
      void knockRipple.offsetWidth;
      knockRipple.classList.add('pulse');
    }

    const isAsleep = currentEmotion === EMOTIONS.SLEEPING || currentEmotion === EMOTIONS.SLEEPY;

    if (isAsleep) {
      // Single tap while asleep stirs Cappy but does NOT wake him up!
      saccadeY = 3.5;
      setTimeout(() => { saccadeY = 0; }, 160);
      logSerial(`[ADXL345] Single tap detected (${source}) &mdash; Cappy stirs sleepily. <strong>Double-tap</strong> required to wake him up!`);
    } else {
      // Single tap while awake
      saccadeY = 6;
      setTimeout(() => { saccadeY = 0; }, 180);
      lastActivityTime = performance.now();
      logSerial(`[ADXL345] Single tap detected (${source}) &mdash; Cappy nods hello.`);
    }
  }

  function triggerDoubleTap(source = 'Physical Double-Tap') {
    // Double buzz haptic knock
    playHapticBuzz('knock');
    setTimeout(() => { playHapticBuzz('knock'); }, 100);

    if (knockRipple) {
      knockRipple.classList.remove('pulse');
      void knockRipple.offsetWidth;
      knockRipple.classList.add('pulse');
    }

    const isAsleep = currentEmotion === EMOTIONS.SLEEPING || currentEmotion === EMOTIONS.SLEEPY;

    if (!isAsleep) {
      logSerial(`[ADXL345] Double-tap detected (${source}) &mdash; Cappy is already awake!`);
      saccadeY = 8;
      setTimeout(() => { saccadeY = -4; }, 110);
      setTimeout(() => { saccadeY = 0; }, 220);
      lastActivityTime = performance.now();
      return;
    }

    const now = performance.now();
    clearTimeout(sleepTransitionTimeout);
    dazedStartTime = now;
    dazedEndTime = now + 7500;
    lastActivityTime = now;
    setCappyEmotion(EMOTIONS.DAZED);
    logSerial(`[ADXL345] Double-tap confirmed (${source})! Waking up companion &rarr; Flutter blinks &amp; looking around (7.5s)`);
  }

  // =========================================================================
  // GLASS PROXIMITY ENGINE (Undetected, Detected, Close, Close up)
  // =========================================================================
  const PROXIMITY_METRICS = [
    { level: 0, label: 'Undetected', emotion: EMOTIONS.DEFAULT, haptic: null },
    { level: 1, label: 'Detected', emotion: EMOTIONS.CURIOUS, haptic: 'single' },
    { level: 2, label: 'Close', emotion: EMOTIONS.ANGRY, haptic: 'double' },
    { level: 3, label: 'Close up', emotion: EMOTIONS.RAGE, haptic: 'rage' }
  ];

  let currentMetricLevel = 0;

  function setProximityLevel(level, triggerSound = true, cursorAngle = null) {
    level = Math.max(0, Math.min(3, Math.round(level)));

    if (proximitySlider && document.activeElement !== proximitySlider) {
      proximitySlider.value = level;
    }

    const metric = PROXIMITY_METRICS[level];
    if (proximityValLabel) {
      proximityValLabel.textContent = metric.label;
      proximityValLabel.style.color =
        level === 3 ? 'var(--accent-red)' :
        level === 2 ? 'var(--accent-red)' :
        level === 1 ? 'var(--accent-yellow)' : 'var(--cappy-orange)';
    }

    if (level !== currentMetricLevel) {
      currentMetricLevel = level;
      if (triggerSound && metric.haptic) {
        triggerHapticUI(metric.haptic);
      }
    }

    // If glasses detected nearby (level >= 1), keep Cappy alert and active
    if (level > 0) {
      lastActivityTime = performance.now();
      clearTimeout(sleepTransitionTimeout);
    }

    const isAsleep = currentEmotion === EMOTIONS.SLEEPING || currentEmotion === EMOTIONS.SLEEPY;

    // If Cappy is asleep and no glasses are detected (level 0), keep sleeping peacefully!
    if (isAsleep && level === 0) {
      return;
    }

    // Wake up if asleep when glasses are detected nearby (level >= 1)
    if (isAsleep && level > 0) {
      dazedEndTime = 0;
    }

    // While waking up groggy from a knock, maintain groggy wake-up animation unless danger Close up (level 3)
    if (dazedEndTime > performance.now() && level < 3) {
      return;
    }

    setCappyEmotion(metric.emotion);

    // If Detected (level 1), look directly towards the sunglasses cursor
    if (level === 1 && cursorAngle !== null) {
      // Invert delta because canvas is rotated 180° around (CX, CY)
      saccadeX = -Math.cos(cursorAngle) * 22;
      saccadeY = -Math.sin(cursorAngle) * 14;
      tgtL.angle = -30 + Math.sin(cursorAngle) * 15;
      tgtR.angle = -30 + Math.sin(cursorAngle) * 15;
    }
  }

  // =========================================================================
  // SUB-PIXEL ROTATED CANVAS RENDERER
  // =========================================================================
  function drawRoundedRectRotated(context, cx, cy, w, h, r, angleDeg, color) {
    context.save();
    context.translate(cx, cy);
    context.rotate((angleDeg * Math.PI) / 180);

    const halfW = w / 2;
    const halfH = h / 2;
    const rad = Math.min(r, halfW, halfH);

    context.beginPath();
    context.moveTo(-halfW + rad, -halfH);
    context.lineTo(halfW - rad, -halfH);
    context.quadraticCurveTo(halfW, -halfH, halfW, -halfH + rad);
    context.lineTo(halfW, halfH - rad);
    context.quadraticCurveTo(halfW, halfH, halfW - rad, halfH);
    context.lineTo(-halfW + rad, halfH);
    context.quadraticCurveTo(-halfW, halfH, -halfW, halfH - rad);
    context.lineTo(-halfW, -halfH + rad);
    context.quadraticCurveTo(-halfW, -halfH, -halfW + rad, -halfH);
    context.closePath();

    context.fillStyle = color;
    context.fill();
    context.restore();
  }

  function drawVectorZ(context, cx, cy, size, color, alpha) {
    const hw = size * 0.38;
    const hh = size * 0.48;

    context.save();
    context.strokeStyle = color;
    context.globalAlpha = alpha;
    context.lineWidth = 2.4;
    context.lineCap = 'round';
    context.lineJoin = 'round';

    context.beginPath();
    context.moveTo(cx - hw, cy - hh);
    context.lineTo(cx + hw, cy - hh);
    context.lineTo(cx - hw, cy + hh);
    context.lineTo(cx + hw, cy + hh);
    context.stroke();

    context.restore();
  }

  function renderZzzParticles(now) {
    if (currentEmotion !== EMOTIONS.SLEEPING) return;

    for (let i = 0; i < 3; i++) {
      const cycle = 2600;
      const offset = i * 860;
      const p = ((now + offset) % cycle) / cycle;

      if (p < 0.04) continue;

      // Trajectory rising upwards from the drooped right eye towards upper-right
      const zX = (CX + 22) + p * 40 + Math.sin(p * 5 + i) * 5;
      const zY = (CY + 18) - p * 86;
      const size = 9 + i * 4.5;

      let alpha = 1.0;
      if (p < 0.2) alpha = p / 0.2;
      else if (p > 0.75) alpha = (1.0 - p) / 0.25;

      if (alpha > 0.08) {
        drawVectorZ(ctx, zX, zY, size, '#ffffff', alpha);
      }
    }
  }

  // =========================================================================
  // ANIMATION LOOP (30 FPS PACE)
  // =========================================================================
  function renderLoop(timestamp) {
    requestAnimationFrame(renderLoop);

    const now = performance.now();
    let dt = (now - lastFrameTime) * 0.001;
    if (dt > 0.05) dt = 0.05;
    lastFrameTime = now;

    // FPS Meter
    frameCounter++;
    if (now - lastFpsCheck >= 1000) {
      fps = Math.round((frameCounter * 1000) / (now - lastFpsCheck));
      frameCounter = 0;
      lastFpsCheck = now;
      if (hudFps) hudFps.textContent = `${fps} FPS`;
    }

    // -------------------------------------------------------------
    // Dazed Wake-up Choreography
    // -------------------------------------------------------------
    if (currentEmotion === EMOTIONS.DAZED && dazedEndTime > now) {
      const elapsed = now - dazedStartTime;
      if (elapsed < 2000) {
        saccadeX = Math.sin(now * 0.006) * 2.0;
        saccadeY = -2.0;
        tgtL.w = 48; tgtL.h = 22; tgtL.r = 11; tgtL.angle = -3;
        tgtR.w = 48; tgtR.h = 22; tgtR.r = 11; tgtR.angle = 3;
      } else if (elapsed < 3800) {
        // Look Left
        saccadeX = -26 + Math.sin(now * 0.004) * 2.0;
        saccadeY = 6.0;
        tgtL.w = 48; tgtL.h = 24; tgtL.r = 12; tgtL.angle = -12;
        tgtR.w = 48; tgtR.h = 24; tgtR.r = 12; tgtR.angle = -12;
      } else if (elapsed < 5600) {
        // Look Right
        saccadeX = 26 + Math.sin(now * 0.004) * 2.0;
        saccadeY = 6.0;
        tgtL.w = 48; tgtL.h = 24; tgtL.r = 12; tgtL.angle = 12;
        tgtR.w = 48; tgtR.h = 24; tgtR.r = 12; tgtR.angle = 12;
      } else {
        // Return Center & stretch wide
        saccadeX = 0;
        saccadeY = 2.0;
        tgtL.w = 38; tgtL.h = 42; tgtL.r = 16; tgtL.angle = -5;
        tgtR.w = 38; tgtR.h = 42; tgtR.r = 16; tgtR.angle = -5;
      }
    } else {
      if (currentEmotion === EMOTIONS.DAZED && dazedEndTime <= now) {
        dazedEndTime = 0;
        lastActivityTime = now;
        setCappyEmotion(EMOTIONS.DEFAULT);
      }

      // Automatic Inactivity Sleep Engine (safe & undisturbed: 8s drowsy, 16s deep sleep)
      if (currentMetricLevel === 0 && dazedEndTime <= now) {
        const idleMs = now - lastActivityTime;
        if (idleMs >= 16000) {
          if (currentEmotion !== EMOTIONS.SLEEPING) {
            setCappyEmotion(EMOTIONS.SLEEPING);
            logSerial(`[Idle Engine] Inactivity timeout reached &rarr; Deep Sleep Mode (Zzz). Knock on Cappy's body to wake him up!`);
          }
        } else if (idleMs >= 8000) {
          if (currentEmotion !== EMOTIONS.SLEEPY && currentEmotion !== EMOTIONS.SLEEPING) {
            setCappyEmotion(EMOTIONS.SLEEPY);
            logSerial(`[Idle Engine] Safe for 8s &rarr; Cappy is getting drowsy (Sleepy Mode).`);
          }
        }
      }

      // Organic glances
      if (now > nextSaccadeTime) {
        if (currentEmotion === EMOTIONS.DEFAULT) {
          const r = Math.random();
          if (r < 0.5) {
            const glances = [
              { x: 15, y: -10, a: -16 },
              { x: -15, y: -10, a: 16 },
              { x: 16, y: 0, a: -10 },
              { x: -16, y: 0, a: 10 }
            ];
            const g = glances[Math.floor(Math.random() * glances.length)];
            saccadeX = g.x; saccadeY = g.y;
            tgtL.angle = g.a; tgtR.angle = g.a;
            nextSaccadeTime = now + 1600 + Math.random() * 1200;
          } else {
            saccadeX = (Math.random() - 0.5) * 10;
            saccadeY = (Math.random() - 0.5) * 6;
            tgtL.angle = -5; tgtR.angle = -5;
            nextSaccadeTime = now + 2000 + Math.random() * 1500;
          }
        } else if (currentEmotion === EMOTIONS.CURIOUS) {
          const dirs = [
            { x: 0, y: 0, a: -30 },
            { x: -56, y: 0, a: 30 },
            { x: 6, y: 18, a: -20 },
            { x: -56, y: 18, a: 20 }
          ];
          const d = dirs[Math.floor(Math.random() * dirs.length)];
          saccadeX = d.x; saccadeY = d.y;
          tgtL.angle = d.a; tgtR.angle = d.a;
          nextSaccadeTime = now + 1200 + Math.random() * 1200;
        } else {
          saccadeX = 0; saccadeY = 0;
          nextSaccadeTime = now + 3000;
        }
      }
    }

    // -------------------------------------------------------------
    // Procedural Blinking
    // -------------------------------------------------------------
    if (currentEmotion === EMOTIONS.SLEEPING) {
      isBlinking = false;
      blinkScale = 1.0;
    } else if (currentEmotion === EMOTIONS.DAZED && dazedEndTime > now) {
      const elapsed = now - dazedStartTime;
      if (elapsed < 120) {
        blinkScale = 0.05;
      } else if (elapsed < 310) {
        const t = (elapsed - 120) / 190;
        blinkScale = 0.05 + 0.40 * Math.sin(t * Math.PI * 0.5);
      } else if (elapsed < 500) {
        const t = (elapsed - 310) / 190;
        blinkScale = 0.05 + 0.40 * Math.cos(t * Math.PI * 0.5);
      } else if (elapsed < 700) {
        blinkScale = 0.05;
      } else if (elapsed < 950) {
        const t = (elapsed - 700) / 250;
        blinkScale = 0.05 + 0.70 * Math.sin(t * Math.PI * 0.5);
      } else if (elapsed < 1200) {
        const t = (elapsed - 950) / 250;
        blinkScale = 0.05 + 0.70 * Math.cos(t * Math.PI * 0.5);
      } else if (elapsed < 1400) {
        blinkScale = 0.05;
      } else if (elapsed < 2000) {
        const t = (elapsed - 1400) / 600;
        blinkScale = 0.05 + 0.95 * Math.sin(t * Math.PI * 0.5);
      } else {
        blinkScale = 1.0;
      }
    } else {
      if (!isBlinking && now > nextBlinkTime) {
        isBlinking = true;
        blinkStartTime = now;
      }
      if (isBlinking) {
        const closeTime = (currentEmotion === EMOTIONS.SLEEPY) ? 140 : 85;
        const openTime = (currentEmotion === EMOTIONS.SLEEPY) ? 280 : 170;
        const p = now - blinkStartTime;
        if (p < closeTime) {
          blinkScale = 1.0 - (p / closeTime);
        } else if (p < openTime) {
          blinkScale = (p - closeTime) / (openTime - closeTime);
        } else {
          isBlinking = false;
          nextBlinkTime = now + (currentEmotion === EMOTIONS.SLEEPY ? 1500 + Math.random() * 1200 : 2500 + Math.random() * 3000);
          blinkScale = 1.0;
        }
      }
    }

    // Breathing
    let breathY = Math.sin(now * 0.003) * 2.5;
    if (currentEmotion === EMOTIONS.DAZED) {
      breathY += Math.sin(now * 0.006) * 2.5;
    }

    // Rage Tremor
    let rageShakeX = 0, rageShakeY = 0, rageShakeAngle = 0;
    if (currentEmotion === EMOTIONS.RAGE) {
      rageShakeX = Math.sin(now * 0.055) * 2.8 + (Math.random() - 0.5) * 4;
      rageShakeY = Math.cos(now * 0.065) * 2.2 + (Math.random() - 0.5) * 4;
      rageShakeAngle = (Math.random() - 0.5) * 6;
    }

    // Non-linear spring lerping: factor = 1 - exp(-12 * dt)
    const factor = 1.0 - Math.exp(-12.0 * dt);
    currentBgColor = lerpColor(currentBgColor, targetBgColor, factor * 0.8);

    curL.x += ((tgtL.x + saccadeX) - curL.x) * factor;
    curL.y += ((tgtL.y + saccadeY + breathY) - curL.y) * factor;
    curL.w += (tgtL.w - curL.w) * factor;
    curL.h += (tgtL.h - curL.h) * factor;
    curL.r += (tgtL.r - curL.r) * factor;
    curL.angle += (tgtL.angle - curL.angle) * factor;
    curL.color = lerpColor(curL.color, tgtL.color, factor * 1.5);

    curR.x += ((tgtR.x + saccadeX) - curR.x) * factor;
    curR.y += ((tgtR.y + saccadeY + breathY) - curR.y) * factor;
    curR.w += (tgtR.w - curR.w) * factor;
    curR.h += (tgtR.h - curR.h) * factor;
    curR.r += (tgtR.r - curR.r) * factor;
    curR.angle += (tgtR.angle - curR.angle) * factor;
    curR.color = lerpColor(curR.color, tgtR.color, factor * 1.5);

    // Canvas Draw
    if (ctx) {
      ctx.fillStyle = currentBgColor;
      ctx.fillRect(0, 0, 240, 240);

      const renderHL = Math.max(3.0, curL.h * blinkScale);
      const renderHR = Math.max(3.0, curR.h * blinkScale);

      const rXL = curL.x + rageShakeX;
      const rYL = curL.y + rageShakeY;
      const rXR = curR.x + rageShakeX;
      const rYR = curR.y + rageShakeY;
      const rAL = curL.angle + rageShakeAngle;
      const rAR = curR.angle - rageShakeAngle;

      // Rotate 180 degrees around center (CX, CY) to match hardware lcd.setRotation(2)
      ctx.save();
      ctx.translate(CX, CY);
      ctx.rotate(Math.PI);
      ctx.translate(-CX, -CY);

      drawRoundedRectRotated(ctx, rXL, rYL, curL.w, renderHL, curL.r, rAL, curL.color);
      drawRoundedRectRotated(ctx, rXR, rYR, curR.w, renderHR, curR.r, rAR, curR.color);

      ctx.restore();

      // Zzz animation during sleep
      renderZzzParticles(now);

      // Subtle bezel ring
      ctx.strokeStyle = currentBgColor === '#000000' ? '#182430' : '#4d1414';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(CX, CY, 119, 0, Math.PI * 2);
      ctx.stroke();
    }
  }

  // =========================================================================
  // SERIAL TERMINAL CLI
  // =========================================================================
  const termScreen = document.getElementById('termScreen');
  const termInput = document.getElementById('termInput');

  function logSerial(msg) {
    if (!termScreen) return;
    const time = new Date().toISOString().substring(11, 19);
    const line = document.createElement('div');
    line.innerHTML = `<span style="color: #6b7280;">[${time}]</span> ${msg}`;
    termScreen.appendChild(line);
    termScreen.scrollTop = termScreen.scrollHeight;
  }

  function handleCLICommand(cmdRaw) {
    const cmd = cmdRaw.trim().toLowerCase();
    if (!cmd) return;

    logSerial(`<span style="color: #fff; font-weight: 700;">esp32-s3 &gt; ${cmdRaw}</span>`);

    switch (cmd) {
      case 'rage':
      case 'closeup':
      case 'close-up':
      case 'superclose':
        setProximityLevel(3, true);
        break;

      case 'close':
      case 'near':
      case 'angry':
        setProximityLevel(2, true);
        break;

      case 'detected':
      case 'far':
      case 'curious':
        setProximityLevel(1, true);
        break;

      case 'undetected':
      case 'none':
      case 'safe':
      case 'happy':
      case 'default':
        setProximityLevel(0, false);
        setCappyEmotion(EMOTIONS.DEFAULT);
        break;

      case 'sleepy':
      case 'drowsy':
        setCappyEmotion(EMOTIONS.SLEEPY);
        if (proximitySlider) proximitySlider.value = 0;
        if (proximityValLabel) {
          proximityValLabel.textContent = 'Undetected';
          proximityValLabel.style.color = 'var(--text-muted)';
        }
        break;

      case 'sleep':
      case 'sleeping':
        setCappyEmotion(EMOTIONS.SLEEPING);
        if (proximitySlider) proximitySlider.value = 0;
        if (proximityValLabel) {
          proximityValLabel.textContent = 'Undetected';
          proximityValLabel.style.color = 'var(--text-muted)';
        }
        break;

      case 'doubletap':
      case 'double-tap':
      case 'wake':
        triggerDoubleTap('Serial CLI');
        break;

      case 'knock':
      case 'tap':
        triggerSingleTap('Serial CLI');
        break;

      case 'glasses':
      case 'toggle':
        if (glassesToggleBtn) glassesToggleBtn.click();
        break;

      case 'buzz':
      case 'vibe':
      case 'motor':
        triggerHapticUI('double');
        logSerial(`[Haptic] Motor pulsed via MOSFET on GPIO 4`);
        break;

      case 'status':
        logSerial(`----------------- CAPPY STATUS -----------------`);
        logSerial(`Mood: ${currentEmotion}`);
        logSerial(`Smart Glasses Active: ${glassesActive ? 'YES' : 'NO'}`);
        logSerial(`Proximity: ${PROXIMITY_METRICS[currentMetricLevel].label} (Level ${currentMetricLevel})`);
        logSerial(`Display: GC9A01 240x240 Round via 40MHz SPI (DMA)`);
        logSerial(`Knock Sensor: ADXL345 at I2C 0x53 (INT1 on GPIO 3)`);
        logSerial(`BLE Sentinel: NimBLE on Core 0 (100% duty cycle)`);
        logSerial(`Case: Cappy 2.0 Orange Squircle Enclosure`);
        logSerial(`------------------------------------------------`);
        break;

      case 'clear':
        termScreen.innerHTML = '';
        break;

      case 'help':
        logSerial(`Commands: rage, close, detected, undetected, safe, sleepy, sleep, knock, glasses, buzz, status, clear`);
        break;

      default:
        logSerial(`Command not recognized: '${cmdRaw}'. Type 'help' for command list.`);
        break;
    }
  }

  // =========================================================================
  // SMART GLASSES DETECTION DATABASE
  // =========================================================================
  const DETECTION_DATA = [
    {
      company: "Meta Platforms",
      sigId: "0x01AB",
      device: "Ray-Ban Stories / Ray-Ban Meta",
      category: "smart-glasses",
      role: "Primary Target",
      desc: "Captures 12MP photos & 1080p 60fps video with dual cameras. Advertises 0x01AB beacon frames continuously."
    },
    {
      company: "Meta Platforms Tech",
      sigId: "0x058E",
      device: "Ray-Ban Meta / Quest 3",
      category: "smart-glasses",
      role: "Primary Target",
      desc: "Latest firmware hardware vendor identifier for Meta AI smart glasses."
    },
    {
      company: "Luxottica Group",
      sigId: "0x0D53",
      device: "Ray-Ban Meta / Oakley Radar Pace",
      category: "smart-glasses",
      role: "Primary Target",
      desc: "Eyewear manufacturing parent company embedded in BLE advertisement packets."
    },
    {
      company: "Snapchat Inc",
      sigId: "0x03C2",
      device: "Snap Spectacles 3 & 4",
      category: "smart-glasses",
      role: "Target",
      desc: "Dual HD camera frames for 3D stereoscopic video recording."
    },
    {
      company: "Vuzix Corporation",
      sigId: "0x060C",
      device: "Vuzix Blade / Shield AR",
      category: "ar-glasses",
      role: "Target",
      desc: "Optical waveguide head-up displays with integrated video cameras."
    },
    {
      company: "TCL Communication",
      sigId: "0x0BC6",
      device: "RayNeo X2 / RayNeo Air 2",
      category: "ar-glasses",
      role: "Target",
      desc: "Full-color binocular Micro-LED optical waveguide glasses."
    },
    {
      company: "Google LLC",
      sigId: "0x00E0",
      device: "Google Glass / Enterprise Edition 2",
      category: "ar-glasses",
      role: "Target",
      desc: "The original wearable that birthed the term 'glasshole'."
    },
    {
      company: "Amazon.com",
      sigId: "0x0171",
      device: "Echo Frames (Gen 2 & 3)",
      category: "audio-frames",
      role: "Target",
      desc: "Open-ear directional audio eyewear with multi-microphone array."
    },
    {
      company: "Bose Corporation",
      sigId: "0x009E",
      device: "Bose Frames (Tenor / Soprano / Tempo)",
      category: "audio-frames",
      role: "Target",
      desc: "Acoustic audio sunglasses with built-in micro speakers."
    },
    {
      company: "Brilliant Labs",
      sigId: "0x0A5C",
      device: "Monocle / Frame AI Glasses",
      category: "ar-glasses",
      role: "Target",
      desc: "Open-source AR eyewear with camera and micro OLED display."
    },
    {
      company: "Nothing Technology",
      sigId: "0x0CCA",
      device: "Nothing Ear (2) / Ear (a)",
      category: "test-device",
      role: "Moulik's Test Device",
      desc: "Used by Moulik for real-world calibration before procuring the Meta glasses."
    }
  ];

  function renderDetectionCards(items) {
    const grid = document.getElementById('detectionCardsGrid');
    if (!grid) return;

    grid.innerHTML = items.map(d => `
      <div class="story-card" style="padding: 20px;">
        <div style="display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 10px;">
          <div>
            <div style="font-weight: 800; font-size: 1.05rem; color: var(--text-title);">${d.company}</div>
            <div style="font-size: 0.85rem; color: var(--cappy-orange); font-weight: 700; margin-top: 2px;">${d.device}</div>
          </div>
          <span class="card-stamp stamp-orange" style="margin-bottom: 0;">${d.sigId}</span>
        </div>
        <p style="font-size: 0.88rem; color: var(--text-body); line-height: 1.5; margin-bottom: 12px;">${d.desc}</p>
        <div style="font-family: var(--font-mono); font-size: 0.72rem; color: var(--text-muted);">
          <span>Role: <strong>${d.role}</strong></span>
        </div>
      </div>
    `).join('');
  }

  // =========================================================================
  // DOM EVENT BINDINGS
  // =========================================================================
  window.addEventListener('DOMContentLoaded', () => {
    // 1. Initial renders
    renderDetectionCards(DETECTION_DATA);
    requestAnimationFrame(renderLoop);

    // 2. Chassis Click & Tap -> Double-Tap Wake-up Interaction
    if (cappyChassis) {
      let lastTapTimestamp = 0;

      const handleChassisTap = (e) => {
        e.stopPropagation();
        initAudio();
        const now = performance.now();
        const elapsed = now - lastTapTimestamp;
        lastTapTimestamp = now;

        // Double-tap threshold: between 60ms and 480ms
        if (elapsed > 60 && elapsed < 480) {
          lastTapTimestamp = 0; // consume double-tap
          triggerDoubleTap('Chassis Tap-Tap');
        } else {
          triggerSingleTap('Chassis Tap');
        }
      };

      cappyChassis.addEventListener('click', handleChassisTap);
      if (canvas) {
        canvas.addEventListener('click', handleChassisTap);
      }
    }

    // 3. Smart Glasses Toggle Button (Top-Left)
    if (glassesToggleBtn) {
      glassesToggleBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        initAudio();
        glassesActive = !glassesActive;
        document.body.classList.toggle('glasses-on', glassesActive);
        glassesToggleBtn.classList.toggle('active', glassesActive);
        const textSpan = glassesToggleBtn.querySelector('.glasses-toggle-text');
        if (textSpan) {
          textSpan.textContent = glassesActive ? 'Glasses: ON' : 'Glasses: OFF';
        }

        clearTimeout(sleepTransitionTimeout);

        if (!glassesActive) {
          // Reset proximity when smart glasses are turned off
          currentMetricLevel = 0;
          if (proximitySlider) proximitySlider.value = 0;
          if (proximityValLabel) {
            proximityValLabel.textContent = 'Undetected';
            proximityValLabel.style.color = 'var(--text-muted)';
          }

          logSerial(`[BLE] Smart glasses toggled OFF &mdash; radio silence, no targets in range.`);
          logSerial(`[Cappy] No glasses detected. Drooping into Sleep Mode...`);

          // 1. Immediately droop eyes into sleepy mode
          setCappyEmotion(EMOTIONS.SLEEPY);

          // 2. After 1.2s, close eyes into deep sleep with Zzz
          sleepTransitionTimeout = setTimeout(() => {
            if (!glassesActive) {
              lastActivityTime = performance.now() - 25000;
              setCappyEmotion(EMOTIONS.SLEEPING);
              logSerial(`[Cappy] Deep Sleep Mode activated (Zzz)! Tap or knock on Cappy's orange chassis to wake him up.`);
            }
          }, 1200);
        } else {
          // Glasses turned back ON
          lastActivityTime = performance.now();
          setCappyEmotion(EMOTIONS.DEFAULT);
          logSerial(`[BLE] Smart glasses toggled ON &mdash; BLE radio active, sniffing Meta frames.`);
          playHapticBuzz('single');
        }
      });
    }

    // 4. Mouse cursor proximity tracking (Only active when glasses are ON)
    let isUserDraggingSlider = false;
    let lastMouseMoveTime = 0;

    window.addEventListener('mousemove', (e) => {
      // If smart glasses are toggled OFF, or manual slider being dragged, ignore mouse proximity
      if (!cappyChassis || !glassesActive || isUserDraggingSlider) return;

      const now = performance.now();
      if (now - lastMouseMoveTime < 25) return; // ~40fps smooth throttle
      lastMouseMoveTime = now;

      const rect = cappyChassis.getBoundingClientRect();
      const cappyCenterX = rect.left + rect.width / 2;
      const cappyCenterY = rect.top + rect.height / 2;

      const dx = e.clientX - cappyCenterX;
      const dy = e.clientY - cappyCenterY;
      const dist = Math.hypot(dx, dy);
      const cursorAngle = Math.atan2(dy, dx);

      // Distance thresholds:
      // dist > 520px: Undetected (level 0)
      // 300px < dist <= 520px: Detected (level 1)
      // 170px < dist <= 300px: Close (level 2)
      // dist <= 170px: Close up (level 3 - sunglasses right over Cappy!)
      let level = 0;
      if (dist <= 170) {
        level = 3;
      } else if (dist <= 300) {
        level = 2;
      } else if (dist <= 520) {
        level = 1;
      } else {
        level = 0;
      }

      setProximityLevel(level, true, cursorAngle);
    });

    document.addEventListener('mouseleave', () => {
      if (!isUserDraggingSlider && glassesActive) {
        setProximityLevel(0, false);
      }
    });

    // 5. Proximity Slider
    if (proximitySlider) {
      proximitySlider.addEventListener('mousedown', () => { isUserDraggingSlider = true; });
      proximitySlider.addEventListener('touchstart', () => { isUserDraggingSlider = true; }, { passive: true });
      window.addEventListener('mouseup', () => { setTimeout(() => { isUserDraggingSlider = false; }, 350); });
      window.addEventListener('touchend', () => { setTimeout(() => { isUserDraggingSlider = false; }, 350); });

      proximitySlider.addEventListener('input', (e) => {
        initAudio();
        const val = parseFloat(e.target.value);
        setProximityLevel(val, true, null);
      });
    }

    // 6. Cappy Quick Emotion Buttons
    document.querySelectorAll('.mood-btn, .cappy-action-btn').forEach(btn => {
      btn.addEventListener('click', (e) => {
        e.stopPropagation();
        initAudio();
        const emo = btn.dataset.emo;
        clearTimeout(sleepTransitionTimeout);

        if (emo === 'KNOCK') {
          triggerKnock('Control Button');
        } else if (emo === 'DEFAULT') {
          lastActivityTime = performance.now();
          setProximityLevel(0, false);
          setCappyEmotion(EMOTIONS.DEFAULT);
        } else if (emo === 'CURIOUS') {
          lastActivityTime = performance.now();
          setProximityLevel(1, true);
        } else if (emo === 'ANGRY') {
          lastActivityTime = performance.now();
          setProximityLevel(2, true);
        } else if (emo === 'RAGE') {
          lastActivityTime = performance.now();
          setProximityLevel(3, true);
        } else if (emo === 'SLEEPY') {
          dazedEndTime = 0;
          lastActivityTime = performance.now() - 9000;
          setCappyEmotion(EMOTIONS.SLEEPY);
          if (proximitySlider) proximitySlider.value = 0;
          if (proximityValLabel) {
            proximityValLabel.textContent = 'Undetected';
            proximityValLabel.style.color = 'var(--text-muted)';
          }
        } else if (emo === 'SLEEPING') {
          dazedEndTime = 0;
          lastActivityTime = performance.now() - 25000;
          setCappyEmotion(EMOTIONS.SLEEPING);
          if (proximitySlider) proximitySlider.value = 0;
          if (proximityValLabel) {
            proximityValLabel.textContent = 'Undetected';
            proximityValLabel.style.color = 'var(--text-muted)';
          }
        } else {
          lastActivityTime = performance.now();
          setCappyEmotion(emo);
        }
      });
    });

    // 5. Terminal inputs
    if (termInput) {
      termInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
          handleCLICommand(termInput.value);
          termInput.value = '';
        }
      });
    }

    document.querySelectorAll('.term-chip').forEach(chip => {
      chip.addEventListener('click', () => {
        handleCLICommand(chip.dataset.cmd);
      });
    });

    // 6. Audio Toggle
    if (audioToggleBtn) {
      audioToggleBtn.addEventListener('click', () => {
        audioEnabled = !audioEnabled;
        audioToggleBtn.classList.toggle('active', audioEnabled);
        const textSpan = audioToggleBtn.querySelector('.audio-text');
        if (textSpan) textSpan.textContent = audioEnabled ? 'Haptics: ON' : 'Haptics: OFF';
        if (audioEnabled) {
          initAudio();
          playHapticBuzz('single');
        }
      });
    }

    // 7. Initial terminal log
    logSerial(`<span style="color: var(--cappy-orange); font-weight: 800;">==================================================</span>`);
    logSerial(`<span style="color: var(--cappy-orange); font-weight: 800;">  CAPPY 2.0 // THE MAKINGS OF A PERVERT DETECTOR   </span>`);
    logSerial(`<span style="color: var(--cappy-orange); font-weight: 800;">  A3 Presentation Showcase &bull; Moulik Nanda       </span>`);
    logSerial(`<span style="color: var(--cappy-orange); font-weight: 800;">==================================================</span>`);
    logSerial(`[System] Initialized GC9A01 240x240 LCD at 40MHz SPI via DMA.`);
    logSerial(`[System] ADXL345 Knock Detector active on GPIO 3.`);
    logSerial(`[System] NimBLE scanner tracking Meta Ray-Bans on Core 0.`);
    logSerial(`[System] Click Cappy's orange chassis to knock on him.`);
  });

})();
