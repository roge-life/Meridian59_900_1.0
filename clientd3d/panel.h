// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * panel.h: Shared infrastructure for borderless floating UI panels.
 *
 * All floating panels (minimap, chat, stat bars, enchantments, stats,
 * inventory) register here so they can be moved/hidden together when
 * the main window moves or minimizes, and so they can snap to each
 * other to avoid overlapping.
 */

#ifndef _PANEL_H
#define _PANEL_H

#define PANEL_DRAG_H    18   /* px at top of every panel: drag-to-move strip */
#define PANEL_RESIZE_B   5   /* px at each edge: resize handle zone          */
#define PANEL_SNAP_DIST 12   /* px within which panel edges snap together    */
#define PANEL_HANDLE    20   /* px for the bottom-right resize pull tab      */
#define PANEL_CLOSE_W   18   /* px at far-right of drag strip for the × btn  */

/* Panel flags — passed to PanelSetFlags() after PanelRegister() */
#define PANEL_FLAG_NOCLOSE  0x01  /* suppress the × close button in the drag strip */
#define PANEL_FLAG_FIXED    0x02  /* fixed-position: suppress overlay (no drag/resize chrome) */

/* Drag-strip colour – used by PanelDrawDragStrip and WM_ERASEBKGND */
#define PANEL_DRAG_COLOR  RGB(50, 50, 53)
#define PANEL_DRAG_DOT    RGB(140, 140, 140)

M59EXPORT void    PanelRegister(HWND hwnd);
M59EXPORT void    PanelUnregister(HWND hwnd);
M59EXPORT void    PanelMoveAll(int dx, int dy);
M59EXPORT void    PanelShowAll(int nCmdShow);
M59EXPORT LRESULT PanelHitTest(HWND hwnd, LPARAM lParam, BOOL canResize);
M59EXPORT void    PanelSnap(HWND hwnd, WINDOWPOS *wp);
M59EXPORT void    PanelDrawDragStrip(HDC hdc, int width);
M59EXPORT BOOL    PanelIsHovered(HWND hwnd);
M59EXPORT void    PanelTrackHover(HWND hwnd);
M59EXPORT void    PanelLeaveHover(HWND hwnd);
M59EXPORT void    PanelSetName(HWND hwnd, const char *name);
M59EXPORT void    PanelLoadPos(HWND hwnd, const char *name);
M59EXPORT void    PanelSaveAll(void);
M59EXPORT void    PanelClampAll(HWND hRef);
M59EXPORT void    PanelGameTabCreate(HWND hRef);
M59EXPORT void    PanelGameTabDestroy(void);
M59EXPORT void    PanelGameTabUpdate(void);
M59EXPORT void    PanelGameTabHide(void);
M59EXPORT void    PanelResetAll(HWND hRef);
M59EXPORT void    PanelSetFlags(HWND hwnd, int flags);

#endif /* _PANEL_H */
