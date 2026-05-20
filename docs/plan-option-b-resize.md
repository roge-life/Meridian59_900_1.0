# Option B: Fixed FOV + Live D3D Backbuffer Resize

## Background

MINOR_REV 89 status: resize "sorta works so long as we don't resize too much."  
Root cause: two coupled problems — broken FOV scaling AND a fixed-size backbuffer.

## Root Causes

### 1. FOV macros scale with window size (d3drender.c:16-17)

```c
#define FOV_H  ((gD3DRect.right - gD3DRect.left) / (float)(MAXX * stretchfactor) * (-PI / 3.6f))
#define FOV_V  ((gD3DRect.bottom - gD3DRect.top)  / (float)(MAXY * stretchfactor) * ( PI / 6.0f))
```

At 800px wide: FOV_H ≈ -0.7723 rad (44°) — looks fine.  
At 1800px wide: FOV_H ≈ -1.74 rad (100°) — perspective matrix breaks; objects distort toward edges.  
Past ~2200px: FOV_H exceeds 120°, tan() approaches infinity — hard crash / garbage rendering.

### 2. D3D backbuffer is fixed 800×600 (d3ddriver.c:93-109)

`gPresentParam.BackBufferWidth/Height` are set once at startup from `gScreenWidth/gScreenHeight` (800×600 for large_area, now always).  
`IDirect3DDevice9_Present(..., &rect, &gD3DRect, ...)` stretches the 800×600 backbuffer to fill `gD3DRect` (the window).  
Result: blurry upscaling at large windows; the broken FOV above is applied to a low-res render that gets stretched further.

## Solution Overview

Fix FOV to a constant derived from native 800×600 geometry. Resize the D3D backbuffer to match the window on resize (debounced). Remove the Present stretch. Fix mouse coordinate mapping.

## Step-by-Step Changes

### Step 1 — Fix FOV_H to a constant; derive FOV_V from aspect ratio

**File:** `clientd3d/d3drender.c` lines 16-17

Replace:
```c
#define FOV_H  ((gD3DRect.right - gD3DRect.left) / (float)(MAXX * stretchfactor) * (-PI / 3.6f))
#define FOV_V  ((gD3DRect.bottom - gD3DRect.top)  / (float)(MAXY * stretchfactor) * ( PI / 6.0f))
```

With:
```c
// FOV_H fixed at native 800x600 large_area (stretchfactor=2) geometry.
#define FOV_H  (800.0f / (float)(MAXX * 2) * (-PI / 3.6f))

// FOV_V derived from FOV_H + current aspect ratio — correct perspective at any window shape.
#define FOV_V  (2.0f * atanf(tanf(-FOV_H / 2.0f) * (float)gScreenHeight / (float)gScreenWidth))
```

Native values at 800×600: FOV_H ≈ -0.7723 rad (44.2°), FOV_V ≈ +0.5691 rad (32.6°).  
At 16:9 (e.g. 1920×1080): FOV_V ≈ 0.434 rad (24.9°) — correct perspective for 16:9 display.

Note: `XformMatrixPerspective` takes FOV_H negative (convention in this codebase) and FOV_V positive. The atan derivation produces a positive value, matching the expected sign.

### Step 2 — Debounced backbuffer resize

**Files:** `clientd3d/client.h`, `clientd3d/graphics.c`, `clientd3d/game.c` (or winmsg.c)

Add a timer ID constant to `client.h`:
```c
#define TIMER_D3D_RESIZE  3
```

In `D3DRenderResizeDisplay` (`d3drender.c:3364`) — or in `GraphicsAreaResize` (`graphics.c:238`) after the existing call — arm a one-shot 200ms timer instead of resetting immediately:
```c
static UINT_PTR sResizeTimer = 0;
if (sResizeTimer) KillTimer(hMain, sResizeTimer);
sResizeTimer = SetTimer(hMain, TIMER_D3D_RESIZE, 200, NULL);
```

In the WM_TIMER handler (currently `GameTimer` in `game.c`), handle the new ID:
```c
case TIMER_D3D_RESIZE:
    KillTimer(hwnd, id);
    sResizeTimer = 0;
    gScreenWidth  = view.cx;
    gScreenHeight = view.cy;
    D3DRenderReset();   // ShutDown + Init; gPresentParam picks up new gScreenWidth/Height
    break;
```

Why `D3DRenderReset()` works cleanly:
- `D3DRenderShutDown` releases all `D3DPOOL_DEFAULT` resources (gpBackBufferTex[16], gpBackBufferTexFull, skybox textures) before device Reset.
- `D3DRenderInit` recreates them at the new size via `gPresentParam.BackBufferWidth/Height = gScreenWidth/gScreenHeight` (d3ddriver.c:108-109).
- Viewport (`gViewport.Width/Height = gScreenWidth/gScreenHeight`, d3ddriver.c:357-365) corrects automatically.

During the 200ms debounce window, the old backbuffer renders and stretches — visually imperfect for a moment during live drag, then snaps clean.

### Step 3 — Fix the Present call

**File:** `clientd3d/d3drender.c` line 1387

Replace:
```c
RECT rect;
rect.top = 0; rect.bottom = gScreenHeight;
rect.left = 0; rect.right = gScreenWidth;
hr = IDirect3DDevice9_Present(gpD3DDevice, &rect, &gD3DRect, NULL, NULL);
```

With:
```c
hr = IDirect3DDevice9_Present(gpD3DDevice, NULL, NULL, NULL, NULL);
```

`NULL` source/dest tells D3D the backbuffer IS the window; no stretching. Only valid once Step 2 makes backbuffer = window size. The surrounding `rect` local variable and three `rect.*` assignments can be deleted.

### Step 4 — Fix mouse coordinate mapping

**File:** `clientd3d/graphics.c` lines 149-156 (`TranslateToRoom`) and lines 406, 412-413 (`UserMouseMove`)

**TranslateToRoom:**  
With backbuffer = window, window pixels are backbuffer pixels — the stretchfactor division is gone:
```c
// Before:
int stretchfactor = config.large_area ? 2 : 1;
*x = (client_x - view.x) / stretchfactor;
*y = (client_y - view.y) / stretchfactor;

// After:
*x = (client_x - view.x);
*y = (client_y - view.y);
```

**UserMouseMove:**  
Same: `stretchfactor` drops out. Change:
```c
// Before:
int stretchfactor = config.large_area ? 2 : 1;
xunit = x * SCREEN_UNIT * stretchfactor / view.cx;
yunit = y * SCREEN_UNIT * stretchfactor / view.cy;

// After:
xunit = x * SCREEN_UNIT / view.cx;
yunit = y * SCREEN_UNIT / view.cy;
```

`x` and `y` are now in `[0, view.cx]` and `[0, view.cy]` respectively, so dividing by `view.cx`/`view.cy` gives the correct normalized unit.

### Step 5 — MINOR_REV bump and deploy

Every exe change triggers client auto-update. After all code changes:
1. Bump `MINOR_REV` in `clientd3d/client.h` (currently 89 → 90).
2. Update `MinClassicVersion` in `scripts/deploy_dev_gameserver.sh` to match (5089 → 5090).
3. Let CI build. Then run `deploy_dev_gameserver.sh` and `deploy_dev_patch.sh`.

## Files Changed Summary

| File | Change |
|------|--------|
| `clientd3d/d3drender.c:16-17` | Fixed FOV macros |
| `clientd3d/d3drender.c:1381-1387` | Present call — remove rect, pass NULLs |
| `clientd3d/d3drender.c:3364-3370` | `D3DRenderResizeDisplay` — arm debounce timer |
| `clientd3d/client.h:68` | Add `TIMER_D3D_RESIZE 3` |
| `clientd3d/game.c` | Handle `TIMER_D3D_RESIZE` in `GameTimer` |
| `clientd3d/graphics.c:149-156` | `TranslateToRoom` — remove stretchfactor |
| `clientd3d/graphics.c:406,412-413` | `UserMouseMove` — remove stretchfactor |
| `clientd3d/client.h:54` | MINOR_REV 89 → 90 |
| `scripts/deploy_dev_gameserver.sh` | MinClassicVersion 5089 → 5090 |

## Verification Checklist

- [ ] Window resize snaps clean after dragging stops (~200ms)
- [ ] No FOV distortion at any window size (test 1024×768, 1920×1080, ultrawide)
- [ ] Clicking objects works correctly (ray-cast picks up objects under cursor)
- [ ] Mouse-move locomotion (move_areas) still works
- [ ] Minimap click coordinates unaffected (MouseToMiniMap uses its own path)
- [ ] Anti-aliasing config preserved after resize reset
