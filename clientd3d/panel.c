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
#define OVERLAY_HANDLE   20       /* bottom-right resize corner size (px) */

static HWND panels[MAX_PANELS];
static int  nPanels    = 0;
static BOOL gMovingAll = FALSE;   /* suppress snap during PanelMoveAll */

/* ── Overlay state ──────────────────────────────────────────────── */
static HWND  hOverlay    = NULL;  /* shared overlay window            */
static HWND  hOvTarget   = NULL;  /* panel currently hovered          */
static BOOL  gOvDragging = FALSE; /* move drag in progress            */
static BOOL  gOvResizing = FALSE; /* resize drag in progress          */
static POINT gOvStartPt;          /* screen cursor at drag start      */
static RECT  gOvStartRect;        /* panel screen rect at drag start  */

/* ------------------------------------------------------------------ */

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

   SetWindowPos(hOverlay, HWND_TOPMOST,
      rc.left, rc.top, w, h,
      SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
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
         SetWindowPos(hwnd, HWND_TOPMOST,
            trc.left, trc.top, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
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
   /* Hide overlay if it was targeting this panel */
   if (hOvTarget == hwnd)
   {
      hOvTarget = NULL;
      ShowWindow(hOverlay, SW_HIDE);
   }
   for (int i = 0; i < nPanels; i++)
      if (panels[i] == hwnd) { panels[i] = panels[--nPanels]; return; }
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
