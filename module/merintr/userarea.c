// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * userarea.c:  Handle the part of the main window where user faces are displayed.
 */

#include "client.h"
#include "merintr.h"

#define MIN_FACE 1          // Minimum hotspot to which face parts are attached
#define MAX_FACE 20         // Maximum hotspot to which face parts are attached

#define MIN_PLAYER_OVERLAYS 7 // The minimum # of overlays a player will have

static HWND hUser;          // User window
static AREA user_area;      // Drawing area within the panel (below drag strip)

extern BYTE *selftrgt_bits;        // Bitmap for in-use highlight

static LRESULT CALLBACK UserAreaProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
/************************************************************************/
/*
 * UserAreaCreate:  Create the user display area.
 */
void UserAreaCreate(void)
{
   static BOOL classReg = FALSE;
   if (!classReg)
   {
      WNDCLASSEX wc;
      memset(&wc, 0, sizeof(wc));
      wc.cbSize        = sizeof(wc);
      wc.lpfnWndProc   = UserAreaProc;
      wc.hInstance     = hInst;
      wc.hbrBackground = NULL;
      wc.lpszClassName = "M59UserArea";
      RegisterClassEx(&wc);
      classReg = TRUE;
   }

   // Default position: upper-right corner near hMain (overridden by PanelLoadPos if saved)
   RECT cr;
   GetClientRect(cinfo->hMain, &cr);
   POINT pt = {cr.right - USERAREA_WIDTH - 8, 54};
   ClientToScreen(cinfo->hMain, &pt);

   user_area.x  = 0;
   user_area.y  = PANEL_DRAG_H;
   user_area.cx = USERAREA_WIDTH;
   user_area.cy = USERAREA_HEIGHT;

   hUser = CreateWindowEx(WS_EX_TOOLWINDOW, "M59UserArea", NULL,
      WS_POPUP | WS_VISIBLE,
      pt.x, pt.y, USERAREA_WIDTH, USERAREA_HEIGHT + PANEL_DRAG_H,
      cinfo->hMain, NULL, hInst, NULL);

   PanelRegister(hUser);
   PanelSetFlags(hUser, PANEL_FLAG_NOCLOSE);
   PanelLoadPos(hUser, "UserArea");
}
/************************************************************************/
/*
 * UserAreaDestroy:  Destroy the user display area.
 */
void UserAreaDestroy(void)
{
   if (hUser)
   {
      PanelUnregister(hUser);
      DestroyWindow(hUser);
      hUser = NULL;
   }
}
/************************************************************************/
/*
 * UserAreaRedraw:  Trigger a repaint of the user display area.
 */
void UserAreaRedraw(void)
{
   if (hUser)
      InvalidateRect(hUser, NULL, FALSE);
}
/************************************************************************/
/*
 * UserAreaResize:  Called when the main window is resized.
 */
void UserAreaResize(int xsize, int ysize, AREA *view)
{
   // Panel is user-positioned; just keep drawing coords relative to our client area.
   user_area.x  = 0;
   user_area.y  = PANEL_DRAG_H;
   user_area.cx = USERAREA_WIDTH;
   user_area.cy = USERAREA_HEIGHT;
}

/************************************************************************/
/*
 * UserAreaProc:  Window procedure for user area window.
 */
static LRESULT CALLBACK UserAreaProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   switch (message)
   {
   case WM_ERASEBKGND:
      return 1;

   case WM_PAINT:
   {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);

      PanelDrawDragStrip(hdc, rc.right);

      room_contents_node *r = GetRoomObjectById(cinfo->player->id);
      if (r != NULL)
      {
         OffscreenWindowBackground(NULL, user_area.x, user_area.y, user_area.cx, user_area.cy);

         if (GetUserTargetID() == GetPlayer()->id)
            OffscreenStretchBlt(hdc, 0, PANEL_DRAG_H, user_area.cx, user_area.cy,
               selftrgt_bits, 0, 0, 64, 64, OBB_FLIP | OBB_TRANSPARENT);

         AREA area = { 0, PANEL_DRAG_H, USERAREA_WIDTH, USERAREA_HEIGHT };
         if (list_length(*r->obj.overlays) >= MIN_PLAYER_OVERLAYS)
            DrawStretchedOverlayRange(hdc, &r->obj, &area, NULL, MIN_FACE, MAX_FACE);
         else
            DrawStretchedObjectDefault(hdc, &r->obj, &area, NULL);
      }
      else
      {
         RECT faceRect = {0, PANEL_DRAG_H, USERAREA_WIDTH, PANEL_DRAG_H + USERAREA_HEIGHT};
         FillRect(hdc, &faceRect, GetBrush(COLOR_BGD));
      }

      EndPaint(hwnd, &ps);
      return 0;
   }

   case WM_NCHITTEST:
      return PanelHitTest(hwnd, lParam, FALSE);

   case WM_WINDOWPOSCHANGING:
      PanelSnap(hwnd, (WINDOWPOS *)lParam);
      return 0;

   case WM_LBUTTONDOWN:
      if (GameGetState() == GAME_SELECT)
         PerformAction(A_SELECT, (void *) cinfo->player->id);
      return 0;

   case WM_RBUTTONDOWN:
      SetDescParams(cinfo->hMain, DESC_NONE);
      RequestLook(cinfo->player->id);
      return 0;
   }

   return DefWindowProc(hwnd, message, wParam, lParam);
}
