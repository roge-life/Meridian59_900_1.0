// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * stats.c:  Handle display of game statistics.
 *
 * Each of the 4 stat groups (Stats/Spells/Skills/Quests) has its own
 * independent floating panel.  Inventory has its own panel in inventry.c.
 * The 5 group buttons live in a fixed button bar at the bottom-right of
 * the screen (managed by statbtn.c).
 */

#include "client.h"
#include "merintr.h"

static void StatsCreateGroup(void);
static void StatsDestroyGroup(void);

/* Groups: Stats=2, Spells=3, Skills=4, Quests=5, Inventory=6 (separate) */
#define NUM_STAT_PANELS 4   /* one per non-inventory group */
#define PANEL_GROUP_BASE 2  /* first group index */

typedef struct {
   HWND      hwnd;
   int       group;      /* STATS_SPELLS etc. */
   int       group_type; /* GROUP_NONE / STATS_NUMERIC / STATS_LIST */
   list_type stats;
   AREA      area;       /* content area inside panel */
} StatPanelState;

static StatPanelState sps[NUM_STAT_PANELS];

/* Globals read by statnum.c / statlist.c */
HWND hStats = NULL;
static AREA   stats_area;
static int    current_group;
static int    group_type;
static list_type stats_list; /* renamed from 'stats' to avoid shadowing */

/* ---------------------------------------------------------------
 * SetActiveStatPanel — point the statnum/statlist globals at panel idx
 * --------------------------------------------------------------- */
static void SetActiveStatPanel(int idx)
{
   if (idx < 0 || idx >= NUM_STAT_PANELS) return;
   hStats        = sps[idx].hwnd;
   current_group = sps[idx].group;
   group_type    = sps[idx].group_type;
   stats_list    = sps[idx].stats;
   RECT r;
   GetClientRect(hStats, &r);
   /* stats_area.cy = r.bottom so statnum/statlist comparisons work:
      content starts at StatsGetButtonBorder() = PANEL_DRAG_H */
   sps[idx].area.x  = 0;
   sps[idx].area.y  = PANEL_DRAG_H;
   sps[idx].area.cx = r.right;
   sps[idx].area.cy = r.bottom;
   stats_area = sps[idx].area;
}

static int GroupToIdx(int group)
{
   int idx = group - PANEL_GROUP_BASE;
   return (idx >= 0 && idx < NUM_STAT_PANELS) ? idx : -1;
}

/* ---------------------------------------------------------------
 * StatsGetArea / StatsGetCurrentGroup — used by statnum.c, statlist.c
 * --------------------------------------------------------------- */
void StatsGetArea(AREA *a)
{
   memcpy(a, &stats_area, sizeof(stats_area));
}

int StatsGetCurrentGroup(void)
{
   return current_group;
}

/* --------------------------------------------------------------- */
Bool StatsIsPanelVisible(int button_idx)
{
   if (button_idx == 4)
      return IsInventoryVisible();
   if (button_idx >= 0 && button_idx < NUM_STAT_PANELS)
      return sps[button_idx].hwnd != NULL && IsWindowVisible(sps[button_idx].hwnd);
   return False;
}

/* ---------------------------------------------------------------
 * StatGroupPanelProc — window proc for each of the 4 stat panels
 * --------------------------------------------------------------- */
static LRESULT CALLBACK StatGroupPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
   int idx = (int)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);

   switch (msg)
   {
   case WM_NCHITTEST:
      return PanelHitTest(hwnd, lp, TRUE);

   case WM_WINDOWPOSCHANGING:
      PanelSnap(hwnd, (WINDOWPOS *)lp);
      return 0;

   case WM_SIZE:
      SetActiveStatPanel(idx);
      StatsMove();
      return 0;

   case WM_PAINT:
   {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT r;
      GetClientRect(hwnd, &r);
      FillRect(hdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
      PanelDrawDragStrip(hdc, r.right);
      EndPaint(hwnd, &ps);
      /* Redraw text labels for numeric groups — they are drawn directly to
         the DC (not via child controls) and get erased by the FillRect above. */
      SetActiveStatPanel(idx);
      StatsDraw();
      return 0;
   }

   case WM_ERASEBKGND:
      return 1;

   case WM_DRAWITEM:
      SetActiveStatPanel(idx);
      return StatsListDrawItem(hwnd, (const DRAWITEMSTRUCT *)lp);

   case WM_MEASUREITEM:
      SetActiveStatPanel(idx);
      StatsListMeasureItem(hwnd, (MEASUREITEMSTRUCT *)lp);
      return TRUE;

   case WM_COMMAND:
      SetActiveStatPanel(idx);
      StatsListCommand(hwnd, LOWORD(wp), (HWND)lp, HIWORD(wp));
      return 0;

   case WM_VSCROLL:
      SetActiveStatPanel(idx);
      StatsNumVScroll(hwnd, (HWND)lp, LOWORD(wp), HIWORD(wp));
      return 0;

   case WM_CLOSE:
      ShowWindow(hwnd, SW_HIDE);
      StatsMoveButtons();
      return 0;
   }
   return DefWindowProc(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------
 * StatsCreate — create the 4 floating group panels
 * --------------------------------------------------------------- */
void StatsCreate(HWND hParent)
{
   static Bool classRegistered = False;
   if (!classRegistered)
   {
      WNDCLASSEX wc;
      memset(&wc, 0, sizeof(wc));
      wc.cbSize        = sizeof(wc);
      wc.lpfnWndProc   = StatGroupPanelProc;
      wc.hInstance     = hInst;
      wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
      wc.lpszClassName = "M59StatGroupPanel";
      RegisterClassEx(&wc);
      classRegistered = True;
   }

   int default_w = 200, default_h = 320 + PANEL_DRAG_H;
   RECT cr;
   GetClientRect(hParent, &cr);

   for (int i = 0; i < NUM_STAT_PANELS; i++)
   {
      sps[i].group      = i + PANEL_GROUP_BASE;
      sps[i].group_type = GROUP_NONE;
      sps[i].stats      = NULL;

      /* Default position: right side of main window, cascaded */
      POINT pt = {cr.right - default_w - 4, 54 + i * 30};
      ClientToScreen(hParent, &pt);

      sps[i].hwnd = CreateWindowEx(WS_EX_TOOLWINDOW, "M59StatGroupPanel", NULL,
         WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
         pt.x, pt.y, default_w, default_h,
         hParent, NULL, hInst, NULL);
      SetWindowLongPtr(sps[i].hwnd, GWLP_USERDATA, (LONG_PTR)i);

      sps[i].area.x  = 0;
      sps[i].area.y  = PANEL_DRAG_H;
      sps[i].area.cx = default_w;
      sps[i].area.cy = default_h;

      ShowWindow(sps[i].hwnd, SW_HIDE);
      PanelRegister(sps[i].hwnd);
      char panelName[32];
      sprintf(panelName, "StatGroup%d", i);
      PanelLoadPos(sps[i].hwnd, panelName);
   }

   /* Point globals at panel 0 as a safe default */
   SetActiveStatPanel(0);

   StatCacheCreate();
   StatButtonsCreate();
   RequestStatGroups();
}

/* ---------------------------------------------------------------
 * StatsDestroy
 * --------------------------------------------------------------- */
void StatsDestroy(void)
{
   for (int i = 0; i < NUM_STAT_PANELS; i++)
   {
      if (!sps[i].hwnd) continue;
      SetActiveStatPanel(i);
      StatsDestroyGroup();
      sps[i].stats      = NULL;
      sps[i].group_type = GROUP_NONE;
      PanelUnregister(sps[i].hwnd);
      DestroyWindow(sps[i].hwnd);
      sps[i].hwnd = NULL;
   }
   StatsDestroyButtons();
   StatButtonsDestroy();
   StatCacheDestroy();
   hStats = NULL;
}

/* ---------------------------------------------------------------
 * StatsResize — called on main window resize; panels are floating
 * so just refresh their content layout
 * --------------------------------------------------------------- */
void StatsResize(int xsize, int ysize, AREA *view)
{
   for (int i = 0; i < NUM_STAT_PANELS; i++)
   {
      if (!sps[i].hwnd || sps[i].group_type == GROUP_NONE) continue;
      SetActiveStatPanel(i);
      StatsMove();
   }
}

/* ---------------------------------------------------------------
 * StatsSetFocus / StatsDrawBorder — no-ops for floating panels
 * --------------------------------------------------------------- */
void StatsSetFocus(Bool forward)
{
   if (hStats) SetFocus(hStats);
}

void StatsDrawBorder(void)
{
}

/* ---------------------------------------------------------------
 * StatsResetFont
 * --------------------------------------------------------------- */
void StatsResetFont(void)
{
   for (int i = 0; i < NUM_STAT_PANELS; i++)
   {
      if (!sps[i].hwnd || sps[i].group_type == GROUP_NONE) continue;
      SetActiveStatPanel(i);
      StatsDestroyGroup();
      StatsCreateGroup();
      StatsMove();
      InvalidateRect(sps[i].hwnd, NULL, TRUE);
   }
}

/* ---------------------------------------------------------------
 * StatsChangeColor — update graph colours across all panels
 * --------------------------------------------------------------- */
void StatsChangeColor(void)
{
   for (int i = 0; i < NUM_STAT_PANELS; i++)
   {
      if (sps[i].group_type != STATS_NUMERIC) continue;
      list_type l;
      for (l = sps[i].stats; l != NULL; l = l->next)
      {
         Statistic *s = (Statistic *)(l->data);
         if (s->numeric.tag != STAT_INT) continue;
         SendMessage(s->hControl, GRPH_COLORSET, GRAPHCOLOR_BAR,      GetColor(COLOR_BAR1));
         SendMessage(s->hControl, GRPH_COLORSET, GRAPHCOLOR_LIMITBAR, GetColor(COLOR_BAR2));
         SendMessage(s->hControl, GRPH_COLORSET, GRAPHCOLOR_BKGND,    GetColor(COLOR_BAR3));
      }
   }
}

/* ---------------------------------------------------------------
 * StatsClearArea — with floating panels just invalidate
 * --------------------------------------------------------------- */
void StatsClearArea(void)
{
   if (hStats) InvalidateRect(hStats, NULL, FALSE);
}

/* ---------------------------------------------------------------
 * StatsDraw / StatsMove — use active panel globals
 * --------------------------------------------------------------- */
void StatsDraw(void)
{
   switch (group_type)
   {
   case STATS_NUMERIC:
      StatsNumDraw(stats_list);
      break;
   case STATS_LIST:
      break;
   }
}

void StatsMove(void)
{
   switch (group_type)
   {
   case STATS_NUMERIC:
      StatsNumResize(stats_list);
      break;
   case STATS_LIST:
      StatsListResize(stats_list);
      break;
   }
}

/* ---------------------------------------------------------------
 * StatsDrawNumItem — owner-draw callback (kept for compat)
 * --------------------------------------------------------------- */
Bool StatsDrawNumItem(HWND hwnd, const DRAWITEMSTRUCT *lpdis)
{
   StatsDraw();
   return True;
}

/* ---------------------------------------------------------------
 * StatsCreateGroup / StatsDestroyGroup — create/destroy content controls
 * --------------------------------------------------------------- */
static void StatsCreateGroup(void)
{
   switch (group_type)
   {
   case STATS_NUMERIC:
      StatsNumCreate(stats_list);
      break;
   case STATS_LIST:
      StatsListCreate(stats_list);
      break;
   }
}

static void StatsDestroyGroup(void)
{
   switch (group_type)
   {
   case STATS_NUMERIC:
      StatsNumDestroy(stats_list);
      break;
   case STATS_LIST:
      StatsListDestroy(stats_list);
      break;
   }
}

/* ---------------------------------------------------------------
 * DisplayStatGroup — populate and show the panel for this group
 * --------------------------------------------------------------- */
void DisplayStatGroup(BYTE group, list_type l)
{
   int idx = GroupToIdx(group);
   if (idx < 0) return;

   Bool wasVisible = IsWindowVisible(sps[idx].hwnd);

   /* Tear down old content */
   SetActiveStatPanel(idx);
   StatsDestroyGroup();

   sps[idx].stats      = l;
   sps[idx].group_type = (l == NULL) ? GROUP_NONE : ((Statistic *)(l->data))->type;
   SetActiveStatPanel(idx);

   StatsCreateGroup();
   StatsMove();
   InvalidateRect(sps[idx].hwnd, NULL, TRUE);

   /* Only re-show if user already had this panel open; new opens go via ToggleStatGroupPanel */
   if (wasVisible)
      ShowWindow(sps[idx].hwnd, SW_SHOWNOACTIVATE);
   /* Repaint the button bar button to reflect pressed state */
   StatsMoveButtons();
}

/* ---------------------------------------------------------------
 * StatsGroupsInfo — server told us how many groups exist
 * --------------------------------------------------------------- */
void StatsGroupsInfo(BYTE num_groups, ID *names)
{
   SafeFree(names);
   for (int i = 0; i < NUM_STAT_PANELS; i++)
   {
      SetActiveStatPanel(i);
      StatsDestroyGroup();
      sps[i].stats      = NULL;
      sps[i].group_type = GROUP_NONE;
   }
   StatsMainDestroy();
   StatsSetButtons(6);
   StatCacheSetSize(num_groups);
   RequestStats(STATS_MAIN);
   /* Pre-fetch all panel groups so panels are populated while hidden.
      Without this every group loads empty on the user's first open. */
   for (int g = PANEL_GROUP_BASE; g < PANEL_GROUP_BASE + NUM_STAT_PANELS; g++)
      RequestStats(g);
}

/* ---------------------------------------------------------------
 * StatsReceiveGroup — server delivered a group of stats
 * --------------------------------------------------------------- */
void StatsReceiveGroup(BYTE group, list_type l)
{
   if (group == STATS_MAIN)
      StatsMainReceive(l);
   else
      DisplayStatGroup(group, l);
   StatCacheSetEntry(group, l);
}

/* ---------------------------------------------------------------
 * StatChange — a single stat changed value
 * --------------------------------------------------------------- */
void StatChange(BYTE group, Statistic *s)
{
   Statistic *new_stat = StatCacheUpdate(group, s);

   if (group == STATS_MAIN && new_stat != NULL)
   {
      StatsMainChange(new_stat);
      return;
   }

   int idx = GroupToIdx(group);
   if (idx < 0 || new_stat == NULL) return;

   /* Only redraw if this panel is visible */
   if (!IsWindowVisible(sps[idx].hwnd)) return;

   SetActiveStatPanel(idx);
   switch (new_stat->type)
   {
   case STATS_NUMERIC: StatsNumChangeStat(new_stat);  break;
   case STATS_LIST:    StatsListChangeStat(new_stat); break;
   }
}

/* ---------------------------------------------------------------
 * TogglStatGroupPanel — called from the button bar to open/close
 * --------------------------------------------------------------- */
void ToggleStatGroupPanel(int button_idx)
{
   if (button_idx == 4)
   {
      /* Inventory */
      ShowInventory(!IsInventoryVisible());
      return;
   }
   if (button_idx < 0 || button_idx >= NUM_STAT_PANELS) return;

   HWND hPanel = sps[button_idx].hwnd;
   if (IsWindowVisible(hPanel))
   {
      ShowWindow(hPanel, SW_HIDE);
   }
   else
   {
      /* If we have never loaded this group, request it from server */
      if (sps[button_idx].group_type == GROUP_NONE)
      {
         int group = sps[button_idx].group;
         list_type stat_list;
         if (StatCacheGetEntry(group, &stat_list))
            DisplayStatGroup((BYTE)group, stat_list);
         else
            RequestStats(group);
      }
      else
      {
         /* Data already present — refresh layout in case the panel was
            resized while hidden, then force all children to repaint now
            so the panel never appears momentarily blank. */
         SetActiveStatPanel(button_idx);
         StatsMove();
      }
      ShowWindow(hPanel, SW_SHOWNOACTIVATE);
      SetForegroundWindow(hPanel);
      RedrawWindow(hPanel, NULL, NULL,
         RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
   }
}

/* ---------------------------------------------------------------
 * Legacy stubs kept for compilation
 * --------------------------------------------------------------- */
void DisplayInventoryAsStatGroup(BYTE group)
{
}

void StatsShowGroup(Bool bShow)
{
}
