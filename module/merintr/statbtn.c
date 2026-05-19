// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * statbtn.c:  Stat group buttons in a fixed WS_POPUP bar at the bottom-right
 *             of the screen.  Each button toggles its group panel open/closed.
 */

#include "client.h"
#include "merintr.h"

typedef struct {
  int idLeft, idMid, idRight;
  HWND hwnd;
  char *bitsLeft, *bitsMid, *bitsRight;
  int  width, height;
  int  iWidthLeft, iWidthMid, iWidthRight;
  int  x, y;
  int  name;
} StatButton;

#define NUM_BUTTONS 5

static StatButton buttons[NUM_BUTTONS] = {
  { IDB_SBUTTON1_LEFT, IDB_SBUTTON1_MID, IDB_SBUTTON1_RIGHT, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, IDS_TT_STATS },
  { IDB_SBUTTON2_LEFT, IDB_SBUTTON2_MID, IDB_SBUTTON2_RIGHT, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, IDS_TT_SPELLS },
  { IDB_SBUTTON3_LEFT, IDB_SBUTTON3_MID, IDB_SBUTTON3_RIGHT, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, IDS_TT_SKILLS },
  { IDB_SBUTTON5_LEFT, IDB_SBUTTON4_MID, IDB_SBUTTON4_RIGHT, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, IDS_TT_QUEST },
  { IDB_SBUTTON4_LEFT, IDB_SBUTTON4_MID, IDB_SBUTTON4_RIGHT, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0, 0, IDS_TT_INVENTORY },
};

static keymap stats_key_table[] = {
{ VK_TAB,         KEY_NONE,   A_TABFWD,    (void *) IDC_STATS },
{ VK_TAB,         KEY_SHIFT,  A_TABBACK,   (void *) IDC_STATS },
{ VK_ESCAPE,      KEY_ANY,    A_GOTOMAIN },
{ VK_SINGLEQUOTE, KEY_ANY,    A_GOTOSAY },
{ VK_RIGHT,       KEY_ANY,    A_NEXT },
{ VK_LEFT,        KEY_ANY,    A_PREV },
{ 0, 0, 0},
};

static HWND hStatButtonBar = NULL;
static WNDPROC lpfnDefButtonProc;
static int max_height = 0;
static int button_border = 0;

/* Display order of buttons (maps visual slot 0-4 to button index) */
static const int kButtonOrder[NUM_BUTTONS] = { 4, 1, 2, 0, 3 };

static void StatsCreateButtons(void);
static long CALLBACK StatButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
static void StatsMoveButtonFocus(HWND button, Bool forward);

/* ---------------------------------------------------------------
 * Button bar window proc — fixed to bottom-right of work area
 * --------------------------------------------------------------- */
static LRESULT CALLBACK StatButtonBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
   switch (msg)
   {
   case WM_ERASEBKGND:
      return 1;

   case WM_DRAWITEM:
      StatButtonDrawItem(hwnd, (const DRAWITEMSTRUCT *)lp);
      return TRUE;

   case WM_COMMAND:
      if (HIWORD(wp) == BN_CLICKED)
         StatButtonCommand(hwnd, LOWORD(wp), (HWND)lp, HIWORD(wp));
      return 0;

   case WM_ACTIVATEAPP:
      /* Don't float over other applications when M59 is in the background */
      SetWindowPos(hwnd, wp ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      return 0;
   }
   return DefWindowProc(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------
 * StatButtonsCreate — load bitmaps (called early from StatsCreate)
 * --------------------------------------------------------------- */
void StatButtonsCreate(void)
{
   int i;
   BITMAPINFOHEADER *ptr;

   for (i = 0; i < NUM_BUTTONS; i++)
   {
      ptr = GetBitmapResource(hInst, buttons[i].idLeft);
      if (!ptr) { buttons[i].bitsLeft = NULL; continue; }
      buttons[i].height   = ptr->biHeight;
      buttons[i].bitsLeft = (char *)((BYTE *)ptr + sizeof(BITMAPINFOHEADER) + NUM_COLORS * sizeof(RGBQUAD));
      buttons[i].iWidthLeft = ptr->biWidth / 2;

      ptr = GetBitmapResource(hInst, buttons[i].idMid);
      if (!ptr) { buttons[i].bitsMid = NULL; continue; }
      buttons[i].bitsMid    = (char *)((BYTE *)ptr + sizeof(BITMAPINFOHEADER) + NUM_COLORS * sizeof(RGBQUAD));
      buttons[i].iWidthMid  = ptr->biWidth / 2;

      ptr = GetBitmapResource(hInst, buttons[i].idRight);
      if (!ptr) { buttons[i].bitsRight = NULL; continue; }
      buttons[i].bitsRight  = (char *)((BYTE *)ptr + sizeof(BITMAPINFOHEADER) + NUM_COLORS * sizeof(RGBQUAD));
      buttons[i].iWidthRight = ptr->biWidth / 2;

      max_height = max(max_height, buttons[i].height);
   }
   button_border = 0;  /* overlay strip overlays content; no reserved space */

   /* Create the bar immediately so it's visible from login, not dependent on server message */
   StatsCreateButtons();
}

void StatButtonsDestroy(void)
{
}

/* ---------------------------------------------------------------
 * StatsSetButtons — called from StatsGroupsInfo; bar already created at startup
 * --------------------------------------------------------------- */
void StatsSetButtons(int num_groups)
{
   StatsMoveButtons();
}

/* ---------------------------------------------------------------
 * StatsCreateButtons — create the button bar and button HWNDs
 * --------------------------------------------------------------- */
static void StatsCreateButtons(void)
{
   int i;

   /* Create button bar if needed */
   if (!hStatButtonBar)
   {
      static Bool classReg = False;
      if (!classReg)
      {
         WNDCLASSEX wc;
         memset(&wc, 0, sizeof(wc));
         wc.cbSize        = sizeof(wc);
         wc.lpfnWndProc   = StatButtonBarProc;
         wc.hInstance     = hInst;
         wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
         wc.lpszClassName = "M59StatButtonBar";
         RegisterClassEx(&wc);
         classReg = True;
      }

      int barH = (max_height > 0) ? max_height : 32;
      int barW = NUM_BUTTONS * 50;

      RECT work;
      SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);

      hStatButtonBar = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
         "M59StatButtonBar", NULL,
         WS_POPUP | WS_VISIBLE,
         work.right - barW, work.bottom - barH,
         barW, barH,
         cinfo->hMain, NULL, hInst, NULL);
   }

   /* Destroy and recreate button HWNDs */
   for (i = 0; i < NUM_BUTTONS; i++)
   {
      if (buttons[i].hwnd)
         DestroyWindow(buttons[i].hwnd);

      buttons[i].hwnd = CreateWindow("button", NULL,
         WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
         0, 0, 0, 0,
         hStatButtonBar, (HMENU)IDC_STATBUTTON, hInst, NULL);
      lpfnDefButtonProc = SubclassWindow(buttons[i].hwnd, StatButtonProc);

      if (buttons[i].name)
         TooltipAddWindow(buttons[i].hwnd, hInst, buttons[i].name);
   }

   StatsMoveButtons();
}

/* ---------------------------------------------------------------
 * StatsDestroyButtons — destroy the button bar
 * --------------------------------------------------------------- */
void StatsDestroyButtons(void)
{
   int i;
   for (i = 0; i < NUM_BUTTONS; i++)
      if (buttons[i].hwnd) { DestroyWindow(buttons[i].hwnd); buttons[i].hwnd = NULL; }

   if (hStatButtonBar)
   {
      DestroyWindow(hStatButtonBar);
      hStatButtonBar = NULL;
   }
}

/* ---------------------------------------------------------------
 * StatsMoveButtons — lay buttons out in the bar
 * --------------------------------------------------------------- */
void StatsMoveButtons(void)
{
   if (!hStatButtonBar) return;

   RECT r;
   GetClientRect(hStatButtonBar, &r);
   int barW = r.right;
   int barH = r.bottom;

   /* Shift bar left by PANEL_HANDLE so it doesn't overlap the pull tab at
      the bottom-right corner.  No vertical adjustment needed.
      SWP_NOZORDER: bar already has WS_EX_TOPMOST at creation. */
   RECT wr;
   GetWindowRect(cinfo->hMain, &wr);
   SetWindowPos(hStatButtonBar, NULL,
      wr.right - barW - PANEL_HANDLE, wr.bottom - barH, barW, barH,
      SWP_NOZORDER | SWP_NOACTIVATE);

   PanelGameTabUpdate();

   int btnW = barW / NUM_BUTTONS;

   for (int slot = 0; slot < NUM_BUTTONS; slot++)
   {
      int bi = kButtonOrder[slot];
      StatButton *b = &buttons[bi];
      if (!b->hwnd) continue;

      int x = slot * btnW;
      int w = (slot < NUM_BUTTONS - 1) ? btnW : barW - x;
      b->width = w;
      b->x = x;
      b->y = 0;
      MoveWindow(b->hwnd, x, 0, w, barH, TRUE);
      ShowWindow(b->hwnd, SW_SHOWNORMAL);
   }

   /* Repaint bar to reflect updated pressed states */
   InvalidateRect(hStatButtonBar, NULL, FALSE);
}

/* ---------------------------------------------------------------
 * StatsSetButtonFocus — no-op for floating panel approach
 * --------------------------------------------------------------- */
void StatsSetButtonFocus(int group)
{
}

/* ---------------------------------------------------------------
 * StatsGetButtonBorder — content starts below drag strip
 * --------------------------------------------------------------- */
int StatsGetButtonBorder(void)
{
   return button_border;
}

/* ---------------------------------------------------------------
 * StatButtonProc — subclassed button proc
 * --------------------------------------------------------------- */
static long CALLBACK StatButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   MSG msg;
   msg.hwnd = hwnd; msg.message = message; msg.wParam = wParam; msg.lParam = lParam;
   TooltipForwardMessage(&msg);

   switch (message)
   {
   case WM_ERASEBKGND:
      return 1;
   case WM_KEYDOWN:
      if (HANDLE_WM_KEYDOWN_BLAK(hwnd, wParam, lParam, StatInputKey) == True)
         return 0;
      break;
   }
   return CallWindowProc(lpfnDefButtonProc, hwnd, message, wParam, lParam);
}

/* ---------------------------------------------------------------
 * StatInputKey — keyboard handling for button focus
 * --------------------------------------------------------------- */
Bool StatInputKey(HWND hwnd, UINT key, Bool fDown, int cRepeat, UINT flags)
{
   Bool held_down = (flags & 0x4000) ? True : False;
   int action;
   void *action_data;

   UserDidSomething();
   action = TranslateKey(key, stats_key_table, &action_data);
   if (action == A_NOACTION) return False;

   switch (action)
   {
   case A_TABFWD:   TextInputSetFocus(True); break;
   case A_TABBACK:  SetFocus(cinfo->hMain);  break;
   case A_NEXT:     StatsMoveButtonFocus(hwnd, True);  break;
   case A_PREV:     StatsMoveButtonFocus(hwnd, False); break;
   default:
      if (!held_down) PerformAction(action, action_data);
      break;
   }
   return True;
}

/* ---------------------------------------------------------------
 * StatButtonDrawItem — draw a stat group button
 * --------------------------------------------------------------- */
Bool StatButtonDrawItem(HWND hwnd, const DRAWITEMSTRUCT *lpdis)
{
   int group, xSrc, xDest;
   StatButton *button;
   Bool bPressed = False;

   switch (lpdis->itemAction)
   {
   case ODA_SELECT:
   case ODA_DRAWENTIRE:
      group = StatsFindGroupByHwnd(lpdis->hwndItem) - 1; /* 0-based button idx */
      if (group < 0 || group >= NUM_BUTTONS || !buttons[group].bitsLeft)
      {
         FillRect(lpdis->hDC, &lpdis->rcItem, (HBRUSH)GetStockObject(LTGRAY_BRUSH));
         return True;
      }
      button = &buttons[group];

      if (lpdis->itemState & ODS_SELECTED || StatsIsPanelVisible(group))
         bPressed = True;

      SelectPalette(lpdis->hDC, cinfo->hPal, FALSE);

      xSrc = bPressed ? button->iWidthLeft : 0;
      OffscreenBitBlt(lpdis->hDC, 0, 0, button->iWidthLeft, button->height,
         (BYTE *)button->bitsLeft, xSrc, 0, 2 * button->iWidthLeft,
         OBB_FLIP | OBB_COPY | OBB_TRANSPARENT);

      xSrc  = bPressed ? button->iWidthMid : 0;
      xDest = button->iWidthLeft;
      while (xDest < button->width - button->iWidthRight)
      {
         OffscreenBitBlt(lpdis->hDC, xDest, 0, button->iWidthMid, button->height,
            (BYTE *)button->bitsMid, xSrc, 0, 2 * button->iWidthMid,
            OBB_FLIP | OBB_COPY | OBB_TRANSPARENT);
         xDest += button->iWidthMid;
      }

      xSrc  = bPressed ? button->iWidthRight : 0;
      xDest = button->width - button->iWidthRight;
      OffscreenBitBlt(lpdis->hDC, xDest, 0, button->iWidthRight, button->height,
         (BYTE *)button->bitsRight, xSrc, 0, 2 * button->iWidthRight,
         OBB_FLIP | OBB_COPY | OBB_TRANSPARENT);

      return True;
   }
   return False;
}

/* ---------------------------------------------------------------
 * StatButtonCommand — toggle the panel for the clicked button
 * --------------------------------------------------------------- */
void StatButtonCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
   int button_idx = StatsFindGroupByHwnd(hwndCtl) - 1; /* 0-based */
   if (button_idx < 0 || button_idx >= NUM_BUTTONS) return;

   ToggleStatGroupPanel(button_idx);

   /* Repaint button to show new pressed/unpressed state */
   InvalidateRect(hwndCtl, NULL, FALSE);
}

/* ---------------------------------------------------------------
 * StatsMoveButtonFocus
 * --------------------------------------------------------------- */
static void StatsMoveButtonFocus(HWND button, Bool forward)
{
   int num = StatsFindGroupByHwnd(button);
   int dx  = forward ? +1 : -1;

   if (num == GROUP_NONE) return;
   num = (num - 1 + dx) % NUM_BUTTONS;
   if (num < 0) num += NUM_BUTTONS;
   if (num < NUM_BUTTONS) SetFocus(buttons[num].hwnd);
}

/* ---------------------------------------------------------------
 * StatsFindGroupByHwnd
 * --------------------------------------------------------------- */
int StatsFindGroupByHwnd(HWND hwnd)
{
   int i;
   for (i = 0; i < NUM_BUTTONS; i++)
      if (buttons[i].hwnd == hwnd) return i + 1;
   return GROUP_NONE;
}
