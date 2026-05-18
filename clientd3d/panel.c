// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * panel.c: Registry and shared helpers for floating UI panels.
 */

#include "client.h"
#include "panel.h"

#define MAX_PANELS 16

static HWND panels[MAX_PANELS];
static int  nPanels = 0;
static BOOL gMovingAll = FALSE; /* suppress snap during PanelMoveAll */

M59EXPORT void PanelRegister(HWND hwnd)
{
   if (nPanels < MAX_PANELS)
      panels[nPanels++] = hwnd;
}

M59EXPORT void PanelUnregister(HWND hwnd)
{
   for (int i = 0; i < nPanels; i++)
      if (panels[i] == hwnd) { panels[i] = panels[--nPanels]; return; }
}

M59EXPORT void PanelMoveAll(int dx, int dy)
{
   if (dx == 0 && dy == 0) return;
   gMovingAll = TRUE; /* suppress PanelSnap so panels don't snap against mid-move positions */
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
 * PanelHitTest: Return the WM_NCHITTEST code for the given screen point.
 *   Top PANEL_DRAG_H px → HTCAPTION (drag).
 *   PANEL_RESIZE_B px at each edge → resize HT codes (if canResize).
 *   Resize edges take priority over the drag strip at corners/top.
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
 * PanelSnap: Adjust WINDOWPOS during a move so panel edges snap to other
 *   registered panels when within PANEL_SNAP_DIST pixels.
 *   Call from WM_WINDOWPOSCHANGING.
 */
M59EXPORT void PanelSnap(HWND hwnd, WINDOWPOS *wp)
{
   if (wp->flags & SWP_NOMOVE) return;
   if (gMovingAll) return; /* batch move — panels haven't all relocated yet, skip snap */

   int x = wp->x, y = wp->y;
   int w = wp->cx, h = wp->cy;
   int snap = PANEL_SNAP_DIST;

   for (int i = 0; i < nPanels; i++)
   {
      if (panels[i] == hwnd) continue;
      if (!IsWindowVisible(panels[i])) continue;

      RECT o;
      GetWindowRect(panels[i], &o);

      /* Horizontal: snap right-of-moving to left-of-other, and vice-versa */
      if (abs((x + w) - o.left) < snap) x = o.left - w;
      else if (abs(x - o.right) < snap)  x = o.right;

      /* Vertical: snap bottom-of-moving to top-of-other, and vice-versa */
      if (abs((y + h) - o.top) < snap)  y = o.top - h;
      else if (abs(y - o.bottom) < snap) y = o.bottom;
   }

   wp->x = x;
   wp->y = y;
}

/*
 * PanelIsHovered: Returns TRUE if the cursor is currently inside the drag-strip
 *   zone (top PANEL_DRAG_H px) of hwnd.  Call from WM_PAINT to decide whether
 *   to draw the strip.
 */
M59EXPORT BOOL PanelIsHovered(HWND hwnd)
{
   POINT pt;
   RECT  r;
   GetCursorPos(&pt);
   ScreenToClient(hwnd, &pt);
   GetClientRect(hwnd, &r);
   return (pt.y >= 0 && pt.y < PANEL_DRAG_H && pt.x >= 0 && pt.x < r.right);
}

/*
 * PanelTrackHover: Call from WM_MOUSEMOVE.  Starts leave-tracking and
 *   invalidates the drag-strip zone so it can appear/disappear.
 */
M59EXPORT void PanelTrackHover(HWND hwnd)
{
   TRACKMOUSEEVENT tme;
   RECT rc;
   tme.cbSize      = sizeof(tme);
   tme.dwFlags     = TME_LEAVE;
   tme.hwndTrack   = hwnd;
   tme.dwHoverTime = 0;
   TrackMouseEvent(&tme);
   GetClientRect(hwnd, &rc);
   rc.bottom = PANEL_DRAG_H;
   InvalidateRect(hwnd, &rc, FALSE);
}

/*
 * PanelLeaveHover: Call from WM_MOUSELEAVE to hide the drag strip.
 */
M59EXPORT void PanelLeaveHover(HWND hwnd)
{
   RECT rc;
   GetClientRect(hwnd, &rc);
   rc.bottom = PANEL_DRAG_H;
   InvalidateRect(hwnd, &rc, FALSE);
}

/*
 * PanelDrawDragStrip: Paint the top drag strip into hdc.
 *   Draws a dark bar with a subtle dot-grip pattern centred horizontally.
 */
M59EXPORT void PanelDrawDragStrip(HDC hdc, int width)
{
   RECT strip = { 0, 0, width, PANEL_DRAG_H };
   HBRUSH bg = CreateSolidBrush(PANEL_DRAG_COLOR);
   FillRect(hdc, &strip, bg);
   DeleteObject(bg);

   /* Grip dots – two rows of dots centred in the strip */
   int cx  = width / 2;
   int mid = PANEL_DRAG_H / 2;
   for (int dx = -18; dx <= 18; dx += 4)
   {
      SetPixel(hdc, cx + dx, mid - 2, PANEL_DRAG_DOT);
      SetPixel(hdc, cx + dx, mid + 1, PANEL_DRAG_DOT);
   }
}
