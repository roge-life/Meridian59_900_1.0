// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * panel.c: Registry and shared helpers for floating UI panels.
 *
 * A single shared WS_POPUP overlay window (M59PanelOverlay) is positioned
 * over whichever registered panel the cursor is over.  It draws a drag strip
 * at the top and a 45-degree resize triangle at the bottom-right, and handles
 * move/resize via SetCapture.  A 50 ms timer drives the cursor-panel detection
 * so it works regardless of which child control the cursor is over.
 */

#include "client.h"
#include "panel.h"

#define MAX_PANELS       16
#define OVERLAY_TIMER_ID 42
#define OVERLAY_POLL_MS  50       /* cursor check interval (ms) */
#define OVERLAY_HANDLE   PANEL_HANDLE  /* bottom-right resize corner size (px) */

static HWND panels[MAX_PANELS];
static int  panelFlags[MAX_PANELS];  /* per-panel PANEL_FLAG_* bits */
static int  nPanels    = 0;
static BOOL gMovingAll = FALSE;   /* suppress snap during PanelMoveAll */

/* ── Named panel position persistence ──────────────────────────────── */
#define MAX_NAMED_PANELS 16
typedef struct { HWND hwnd; char name[32]; } NamedPanel;
static NamedPanel namedPanels[MAX_NAMED_PANELS];
static int nNamedPanels = 0;
static char panel_section[] = "Panels";

static void PanelSavePosForHwnd(HWND hwnd); /* forward decl for OverlayProc */

/* ── Overlay state ──────────────────────────────────────────────── */
static HWND  hOverlay    = NULL;  /* shared overlay window            */
static HWND  hOvTarget   = NULL;  /* panel currently hovered          */
static BOOL  gOvDragging = FALSE; /* move drag in progress            */
static BOOL  gOvResizing = FALSE; /* resize drag in progress          */
static POINT gOvStartPt;          /* screen cursor at drag start      */
static RECT  gOvStartRect;        /* panel screen rect at drag start  */

/* ------------------------------------------------------------------ */

static int OverlayGetTargetFlags(void)
{
   if (!hOvTarget) return 0;
   for (int i = 0; i < nPanels; i++)
      if (panels[i] == hOvTarget) return panelFlags[i];
   return 0;
}

static void OverlayApplyRegion(HWND hPanel)
{
   RECT rc;
   GetWindowRect(hPanel, &rc);
   int w = rc.right  - rc.left;
   int h = rc.bottom - rc.top;

   HRGN rgnStrip  = CreateRectRgn(0, 0, w, PANEL_DRAG_H);
   HRGN rgnHandle = CreateRectRgn(w - OVERLAY_HANDLE, h - OVERLAY_HANDLE, w, h);
   HRGN rgnTotal  = CreateRectRgn(0, 0, 0, 0);
   CombineRgn(rgnTotal, rgnStrip, rgnHandle, RGN_OR);
   DeleteObject(rgnStrip);
   DeleteObject(rgnHandle);
   SetWindowRgn(hOverlay, rgnTotal, FALSE); /* region ownership transferred */

   /* SWP_NOZORDER: hOverlay already has WS_EX_TOPMOST; no need to re-fight
      z-order with the button bar every 50 ms. */
   SetWindowPos(hOverlay, NULL,
      rc.left, rc.top, w, h,
      SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static HWND OverlayFindPanel(POINT pt)
{
   HWND w = WindowFromPoint(pt);
   while (w)
   {
      for (int i = 0; i < nPanels; i++)
         if (panels[i] == w) return w;
      HWND p = GetParent(w);
      if (!p || p == GetDesktopWindow()) break;
      w = p;
   }
   return NULL;
}

static void OverlayTick(void)
{
   if (gOvDragging || gOvResizing) return;

   POINT pt;
   GetCursorPos(&pt);

   HWND hPanel = OverlayFindPanel(pt);

   /* If cursor is over the overlay itself keep the current target */
   if (!hPanel && WindowFromPoint(pt) == hOverlay)
      hPanel = hOvTarget;

   /* Fixed panels (e.g. stat button bar) travel with hMain but don't get drag chrome */
   if (hPanel)
   {
      for (int i = 0; i < nPanels; i++)
         if (panels[i] == hPanel && (panelFlags[i] & PANEL_FLAG_FIXED)) { hPanel = NULL; break; }
   }

   if (hPanel)
   {
      /* Check whether the panel moved/resized since last tick */
      RECT rc;
      GetWindowRect(hPanel, &rc);
      RECT oc;
      GetWindowRect(hOverlay, &oc);
      if (hPanel != hOvTarget ||
          oc.left != rc.left || oc.top != rc.top ||
          (oc.right - oc.left) != (rc.right - rc.left) ||
          (oc.bottom - oc.top) != (rc.bottom - rc.top))
      {
         hOvTarget = hPanel;
         OverlayApplyRegion(hPanel);
         InvalidateRect(hOverlay, NULL, FALSE);
      }
   }
   else
   {
      if (hOvTarget)
      {
         hOvTarget = NULL;
         ShowWindow(hOverlay, SW_HIDE);
      }
   }
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
   switch (msg)
   {
   case WM_CREATE:
      SetTimer(hwnd, OVERLAY_TIMER_ID, OVERLAY_POLL_MS, NULL);
      return 0;

   case WM_DESTROY:
      KillTimer(hwnd, OVERLAY_TIMER_ID);
      return 0;

   case WM_TIMER:
      if (wp == OVERLAY_TIMER_ID) OverlayTick();
      return 0;

   case WM_PAINT:
   {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      int w = rc.right, h = rc.bottom;

      /* Drag strip at top */
      PanelDrawDragStrip(hdc, w);

      /* × close button at far-right of drag strip (unless panel opted out) */
      if (!(OverlayGetTargetFlags() & PANEL_FLAG_NOCLOSE))
      {
         int cx = w - PANEL_CLOSE_W / 2;
         int cy = PANEL_DRAG_H / 2;
         int r  = 4;
         HPEN hPen = CreatePen(PS_SOLID, 2, PANEL_DRAG_DOT);
         HPEN hOld = (HPEN)SelectObject(hdc, hPen);
         MoveToEx(hdc, cx - r, cy - r, NULL); LineTo(hdc, cx + r + 1, cy + r + 1);
         MoveToEx(hdc, cx + r, cy - r, NULL); LineTo(hdc, cx - r - 1, cy + r + 1);
         SelectObject(hdc, hOld);
         DeleteObject(hPen);
      }

      /* 45-degree resize triangle at bottom-right */
      POINT tri[3] = { {w - OVERLAY_HANDLE, h}, {w, h - OVERLAY_HANDLE}, {w, h} };
      HRGN  rgn = CreatePolygonRgn(tri, 3, WINDING);
      HBRUSH hBr = CreateSolidBrush(PANEL_DRAG_COLOR);
      FillRgn(hdc, rgn, hBr);
      DeleteObject(rgn);
      DeleteObject(hBr);

      EndPaint(hwnd, &ps);
      return 0;
   }

   case WM_SETCURSOR:
   {
      if (hOvTarget)
      {
         POINT pt;
         GetCursorPos(&pt);
         RECT rc;
         GetWindowRect(hOvTarget, &rc);
         if (pt.y >= rc.bottom - OVERLAY_HANDLE && pt.x >= rc.right - OVERLAY_HANDLE)
            SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
         else if (!(OverlayGetTargetFlags() & PANEL_FLAG_NOCLOSE) &&
                  pt.y < rc.top + PANEL_DRAG_H && pt.x >= rc.right - PANEL_CLOSE_W)
            SetCursor(LoadCursor(NULL, IDC_HAND));
         else
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
      }
      return TRUE;
   }

   case WM_LBUTTONDOWN:
   {
      if (!hOvTarget) return 0;
      POINT pt;
      GetCursorPos(&pt);
      RECT rc;
      GetWindowRect(hOvTarget, &rc);

      /* Close button: top-right PANEL_CLOSE_W × PANEL_DRAG_H strip */
      if (!(OverlayGetTargetFlags() & PANEL_FLAG_NOCLOSE) &&
          pt.y < rc.top + PANEL_DRAG_H && pt.x >= rc.right - PANEL_CLOSE_W)
      {
         HWND target = hOvTarget;
         hOvTarget = NULL;
         ShowWindow(hOverlay, SW_HIDE);
         SendMessage(target, WM_CLOSE, 0, 0);
         return 0;
      }

      gOvStartPt   = pt;
      gOvStartRect = rc;
      SetCapture(hwnd);
      if (pt.y >= rc.bottom - OVERLAY_HANDLE && pt.x >= rc.right - OVERLAY_HANDLE)
         gOvResizing = TRUE;
      else
         gOvDragging = TRUE;
      return 0;
   }

   case WM_MOUSEMOVE:
   {
      if (!hOvTarget) return 0;
      POINT pt;
      GetCursorPos(&pt);
      int dx = pt.x - gOvStartPt.x;
      int dy = pt.y - gOvStartPt.y;

      if (gOvDragging)
      {
         SetWindowPos(hOvTarget, NULL,
            gOvStartRect.left + dx, gOvStartRect.top + dy, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
         /* Track actual post-snap position for the overlay */
         RECT trc;
         GetWindowRect(hOvTarget, &trc);
         SetWindowPos(hwnd, NULL,
            trc.left, trc.top, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      else if (gOvResizing)
      {
         int newW = max((gOvStartRect.right  - gOvStartRect.left) + dx, 60);
         int newH = max((gOvStartRect.bottom - gOvStartRect.top)  + dy, 40);
         SetWindowPos(hOvTarget, NULL,
            gOvStartRect.left, gOvStartRect.top, newW, newH,
            SWP_NOZORDER | SWP_NOACTIVATE);
         OverlayApplyRegion(hOvTarget);
         InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
   }

   case WM_LBUTTONUP:
      if (hOvTarget && (gOvDragging || gOvResizing))
         PanelSavePosForHwnd(hOvTarget);
      gOvDragging = gOvResizing = FALSE;
      ReleaseCapture();
      return 0;

   case WM_CAPTURECHANGED:
      gOvDragging = gOvResizing = FALSE;
      return 0;
   }
   return DefWindowProc(hwnd, msg, wp, lp);
}

static void PanelOverlayCreate(void)
{
   static Bool classReg = False;
   if (!classReg)
   {
      WNDCLASSEX wc;
      memset(&wc, 0, sizeof(wc));
      wc.cbSize        = sizeof(wc);
      wc.lpfnWndProc   = OverlayProc;
      wc.hInstance     = hInst;
      wc.lpszClassName = "M59PanelOverlay";
      RegisterClassEx(&wc);
      classReg = True;
   }
   hOverlay = CreateWindowEx(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
      "M59PanelOverlay", NULL,
      WS_POPUP,
      0, 0, 1, 1,
      NULL, NULL, hInst, NULL);
}

static void PanelSavePosForHwnd(HWND hwnd)
{
   int i;
   for (i = 0; i < nNamedPanels; i++)
   {
      if (namedPanels[i].hwnd != hwnd) continue;
      RECT r;
      GetWindowRect(hwnd, &r);
      char key[64];
      const char *n = namedPanels[i].name;
      sprintf(key, "%s_X", n); WriteConfigInt(panel_section, key, r.left, ini_file);
      sprintf(key, "%s_Y", n); WriteConfigInt(panel_section, key, r.top, ini_file);
      sprintf(key, "%s_W", n); WriteConfigInt(panel_section, key, r.right - r.left, ini_file);
      sprintf(key, "%s_H", n); WriteConfigInt(panel_section, key, r.bottom - r.top, ini_file);
      return;
   }
}

/* ================================================================== */
/* Public API                                                           */
/* ================================================================== */

M59EXPORT void PanelRegister(HWND hwnd)
{
   if (nPanels == 0 && !hOverlay)
      PanelOverlayCreate();
   if (nPanels < MAX_PANELS)
      panels[nPanels++] = hwnd;
}

M59EXPORT void PanelUnregister(HWND hwnd)
{
   /* Clear named panel entry */
   for (int i = 0; i < nNamedPanels; i++)
      if (namedPanels[i].hwnd == hwnd) { namedPanels[i].hwnd = NULL; break; }

   /* Hide overlay if it was targeting this panel */
   if (hOvTarget == hwnd)
   {
      hOvTarget = NULL;
      ShowWindow(hOverlay, SW_HIDE);
   }
   for (int i = 0; i < nPanels; i++)
      if (panels[i] == hwnd)
      {
         --nPanels;
         panels[i]     = panels[nPanels];
         panelFlags[i] = panelFlags[nPanels];
         return;
      }
}

M59EXPORT void PanelSetName(HWND hwnd, const char *name)
{
   for (int i = 0; i < nNamedPanels; i++)
   {
      if (namedPanels[i].hwnd == hwnd)
      {
         strncpy(namedPanels[i].name, name, 31);
         namedPanels[i].name[31] = '\0';
         return;
      }
   }
   if (nNamedPanels < MAX_NAMED_PANELS)
   {
      namedPanels[nNamedPanels].hwnd = hwnd;
      strncpy(namedPanels[nNamedPanels].name, name, 31);
      namedPanels[nNamedPanels].name[31] = '\0';
      nNamedPanels++;
   }
}

M59EXPORT void PanelLoadPos(HWND hwnd, const char *name)
{
   PanelSetName(hwnd, name);
   RECT r;
   GetWindowRect(hwnd, &r);
   char key[64];
   int x, y, w, h;
   sprintf(key, "%s_X", name); x = GetConfigInt(panel_section, key, r.left, ini_file);
   sprintf(key, "%s_Y", name); y = GetConfigInt(panel_section, key, r.top, ini_file);
   sprintf(key, "%s_W", name); w = GetConfigInt(panel_section, key, r.right - r.left, ini_file);
   sprintf(key, "%s_H", name); h = GetConfigInt(panel_section, key, r.bottom - r.top, ini_file);

   /* Compensate for hMain having moved since panels were last saved.
      MainWindow_X/Y is only written when live panels exist, so it always
      matches the panel absolute positions in the INI. */
   RECT mr;
   GetWindowRect(hMain, &mr);
   int savedMainX = GetConfigInt(panel_section, "MainWindow_X", mr.left, ini_file);
   int savedMainY = GetConfigInt(panel_section, "MainWindow_Y", mr.top,  ini_file);
   x += mr.left - savedMainX;
   y += mr.top  - savedMainY;

   if (x != r.left || y != r.top || w != (r.right - r.left) || h != (r.bottom - r.top))
      SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

M59EXPORT void PanelSaveAll(void)
{
   /* Save hMain anchor only when panels are live — keeps anchor in sync with
      panel absolute positions so PanelLoadPos can compensate next session. */
   BOOL anyLive = FALSE;
   for (int i = 0; i < nNamedPanels; i++)
      if (namedPanels[i].hwnd) { anyLive = TRUE; break; }
   if (anyLive)
   {
      RECT mr;
      GetWindowRect(hMain, &mr);
      WriteConfigInt(panel_section, "MainWindow_X", mr.left, ini_file);
      WriteConfigInt(panel_section, "MainWindow_Y", mr.top,  ini_file);
   }
   for (int i = 0; i < nNamedPanels; i++)
      if (namedPanels[i].hwnd) PanelSavePosForHwnd(namedPanels[i].hwnd);
}

M59EXPORT void PanelMoveAll(int dx, int dy)
{
   if (dx == 0 && dy == 0) return;
   gMovingAll = TRUE;
   for (int i = 0; i < nPanels; i++)
   {
      RECT r;
      GetWindowRect(panels[i], &r);
      SetWindowPos(panels[i], NULL,
         r.left + dx, r.top + dy, 0, 0,
         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
   }
   gMovingAll = FALSE;
}

M59EXPORT void PanelShowAll(int nCmdShow)
{
   for (int i = 0; i < nPanels; i++)
      ShowWindow(panels[i], nCmdShow);
}

/*
 * PanelHitTest: WM_NCHITTEST helper — still used as a fallback when the
 * overlay isn't yet visible.  Resize-edge detection is no longer the primary
 * path (the overlay's triangle handles it) so canResize is kept but less
 * critical.
 */
M59EXPORT LRESULT PanelHitTest(HWND hwnd, LPARAM lParam, BOOL canResize)
{
   POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
   ScreenToClient(hwnd, &pt);
   RECT r;
   GetClientRect(hwnd, &r);
   int rb = PANEL_RESIZE_B;

   if (canResize)
   {
      BOOL L = pt.x < rb;
      BOOL R = pt.x >= r.right  - rb;
      BOOL T = pt.y < rb;
      BOOL B = pt.y >= r.bottom - rb;

      if (L && T) return HTTOPLEFT;
      if (R && T) return HTTOPRIGHT;
      if (L && B) return HTBOTTOMLEFT;
      if (R && B) return HTBOTTOMRIGHT;
      if (L)      return HTLEFT;
      if (R)      return HTRIGHT;
      if (T)      return HTTOP;
      if (B)      return HTBOTTOM;
   }

   if (pt.y < PANEL_DRAG_H) return HTCAPTION;
   return HTCLIENT;
}

/*
 * PanelClampAll: After a main-window resize, nudge any registered panel that
 * has drifted entirely outside hRef's screen rect back into view.  Leaves at
 * least 40 px of the panel inside the reference window on each axis.
 */
M59EXPORT void PanelClampAll(HWND hRef)
{
   RECT bounds;
   GetWindowRect(hRef, &bounds);
   int margin = 40;

   for (int i = 0; i < nPanels; i++)
   {
      RECT pr;
      GetWindowRect(panels[i], &pr);
      int pw = pr.right  - pr.left;
      int ph = pr.bottom - pr.top;
      int nx = pr.left, ny = pr.top;

      if (nx + pw < bounds.left + margin) nx = bounds.left + margin - pw;
      if (nx       > bounds.right - margin)  nx = bounds.right - margin;
      if (ny + ph < bounds.top  + margin) ny = bounds.top  + margin - ph;
      if (ny       > bounds.bottom - margin) ny = bounds.bottom - margin;

      if (nx != pr.left || ny != pr.top)
         SetWindowPos(panels[i], NULL, nx, ny, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
   }
}

/*
 * PanelSnap: Adjust WINDOWPOS during a move to snap edges to nearby panels.
 */
M59EXPORT void PanelSnap(HWND hwnd, WINDOWPOS *wp)
{
   if (wp->flags & SWP_NOMOVE) return;
   if (gMovingAll) return;

   int x = wp->x, y = wp->y;
   int w = wp->cx, h = wp->cy;
   int snap = PANEL_SNAP_DIST;

   for (int i = 0; i < nPanels; i++)
   {
      if (panels[i] == hwnd) continue;
      if (!IsWindowVisible(panels[i])) continue;

      RECT o;
      GetWindowRect(panels[i], &o);

      if (abs((x + w) - o.left) < snap) x = o.left - w;
      else if (abs(x - o.right)  < snap) x = o.right;

      if (abs((y + h) - o.top)    < snap) y = o.top - h;
      else if (abs(y - o.bottom)  < snap) y = o.bottom;
   }

   wp->x = x;
   wp->y = y;
}

/*
 * PanelDrawDragStrip: Draw the top drag bar into hdc (used by overlay WM_PAINT).
 */
M59EXPORT void PanelDrawDragStrip(HDC hdc, int width)
{
   RECT strip = { 0, 0, width, PANEL_DRAG_H };
   HBRUSH bg = CreateSolidBrush(PANEL_DRAG_COLOR);
   FillRect(hdc, &strip, bg);
   DeleteObject(bg);

   int cx  = width / 2;
   int mid = PANEL_DRAG_H / 2;
   for (int dx = -18; dx <= 18; dx += 4)
   {
      SetPixel(hdc, cx + dx, mid - 2, PANEL_DRAG_DOT);
      SetPixel(hdc, cx + dx, mid + 1, PANEL_DRAG_DOT);
   }
}

/* These are kept as no-ops: the overlay replaces per-panel hover tracking. */
M59EXPORT BOOL PanelIsHovered(HWND hwnd)  { return FALSE; }
M59EXPORT void PanelTrackHover(HWND hwnd) { }
M59EXPORT void PanelLeaveHover(HWND hwnd) { }

/* ================================================================== */
/* Game-view resize pull tab                                            */
/* ================================================================== */

static HWND hGameTab = NULL;
static HWND hGameRef = NULL;

/* Compute the screen position for the pull tab (bottom-right of hRef's client area). */
static void GameTabClientCorner(HWND hRef, int *px, int *py)
{
   RECT cr;
   GetClientRect(hRef, &cr);
   POINT pt = { cr.right, cr.bottom };
   ClientToScreen(hRef, &pt);
   *px = pt.x - OVERLAY_HANDLE;
   *py = pt.y - OVERLAY_HANDLE;
}

static LRESULT CALLBACK GameTabProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
   switch (msg)
   {
   case WM_ERASEBKGND:
      return 1;

   case WM_PAINT:
   {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      int w = rc.right, h = rc.bottom;
      /* Same 45-degree triangle as the panel overlay, filling the whole window */
      POINT tri[3] = { {0, h}, {w, 0}, {w, h} };
      HRGN  rgn = CreatePolygonRgn(tri, 3, WINDING);
      HBRUSH hBr = CreateSolidBrush(PANEL_DRAG_COLOR);
      FillRgn(hdc, rgn, hBr);
      DeleteObject(rgn);
      DeleteObject(hBr);
      EndPaint(hwnd, &ps);
      return 0;
   }

   case WM_SETCURSOR:
      SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
      return TRUE;

   case WM_LBUTTONDOWN:
   {
      POINT pt;
      GetCursorPos(&pt);
      /* Simulate a click on the bottom-right resize corner of the game window */
      SendMessage(hGameRef, WM_NCLBUTTONDOWN, HTBOTTOMRIGHT, MAKELPARAM(pt.x, pt.y));
      return 0;
   }

   case WM_ACTIVATEAPP:
      /* Don't float over other applications when M59 is in the background */
      SetWindowPos(hwnd, wp ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      return 0;
   }
   return DefWindowProc(hwnd, msg, wp, lp);
}

M59EXPORT void PanelGameTabCreate(HWND hRef)
{
   static Bool classReg = False;
   if (!classReg)
   {
      WNDCLASSEX wc;
      memset(&wc, 0, sizeof(wc));
      wc.cbSize        = sizeof(wc);
      wc.lpfnWndProc   = GameTabProc;
      wc.hInstance     = hInst;
      wc.lpszClassName = "M59GameViewTab";
      RegisterClassEx(&wc);
      classReg = True;
   }
   hGameRef = hRef;
   if (hGameTab) DestroyWindow(hGameTab);
   int tx, ty;
   GameTabClientCorner(hRef, &tx, &ty);
   hGameTab = CreateWindowEx(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
      "M59GameViewTab", NULL,
      WS_POPUP | WS_VISIBLE,
      tx, ty, OVERLAY_HANDLE, OVERLAY_HANDLE,
      NULL, NULL, hInst, NULL);
}

M59EXPORT void PanelGameTabDestroy(void)
{
   if (hGameTab) { DestroyWindow(hGameTab); hGameTab = NULL; }
   hGameRef = NULL;
}

M59EXPORT void PanelGameTabUpdate(void)
{
   if (!hGameTab || !hGameRef) return;
   int tx, ty;
   GameTabClientCorner(hGameRef, &tx, &ty);
   SetWindowPos(hGameTab, NULL,
      tx, ty, OVERLAY_HANDLE, OVERLAY_HANDLE,
      SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

M59EXPORT void PanelGameTabHide(void)
{
   if (hGameTab) ShowWindow(hGameTab, SW_HIDE);
}

/*
 * PanelResetAll: Move all registered panels to a default cascaded layout
 * inside hRef's client area.  Called from the "Reset window positions" menu.
 */
M59EXPORT void PanelResetAll(HWND hRef)
{
   RECT cr;
   GetClientRect(hRef, &cr);
   POINT origin = { cr.left + 4, cr.top + 4 };
   ClientToScreen(hRef, &origin);

   for (int i = 0; i < nPanels; i++)
   {
      if (!IsWindowVisible(panels[i])) continue;
      RECT pr;
      GetWindowRect(panels[i], &pr);
      int pw = pr.right  - pr.left;
      int ph = pr.bottom - pr.top;
      int nx = origin.x + i * 20;
      int ny = origin.y + i * 20;
      SetWindowPos(panels[i], NULL, nx, ny, 0, 0,
         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      PanelSavePosForHwnd(panels[i]);
   }
}

M59EXPORT void PanelSetFlags(HWND hwnd, int flags)
{
   for (int i = 0; i < nPanels; i++)
      if (panels[i] == hwnd) { panelFlags[i] = flags; return; }
}
