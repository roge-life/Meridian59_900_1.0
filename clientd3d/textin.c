// Meridian 59, Copyright 1994-2012 Andrew Kirmse and Chris Kirmse.
// All rights reserved.
//
// This software is distributed under a license that is described in
// the LICENSE file that accompanies it.
//
// Meridian is a registered trademark.
/*
 * textin.c:  Deals with the text input area on the main window.
 */

#include "client.h"

/* Position & size of input box */
AREA input_area;

#define EDITBOX_HISTORY 20

static HWND hwndInput;            // Text input window handle
static WNDPROC lpfnDefInputProc;  // Default text box message handler
static HWND hChatPanel;           // Floating chat popup (parents hwndInput + hwndText)

#define CHAT_PANEL_CLIENT_H 130   // Client-area height of the chat popup

HWND TextInputGetChatPanel(void) { return hChatPanel; }
static int inputHeight = TEXTINPUT_HEIGHT;

static keymap textin_key_table[] = {
{ VK_TAB,         KEY_NONE,             A_TABFWD,    (void *) IDC_TEXTINPUT },
{ VK_TAB,         KEY_SHIFT,            A_TABBACK,   (void *) IDC_TEXTINPUT },
{ VK_ESCAPE,      KEY_ANY,              A_GOTOMAIN },
{ 0, 0, 0 },   // Must end table this way
};

// True when we should ignore next selection message (used to override default
// combo box behavior).
static Bool skip_selection;       

extern HPALETTE hPal;
extern int border_index;

static long CALLBACK TextInputProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
static Bool TextInputKey(HWND hwnd, UINT key, Bool fDown, int cRepeat, UINT flags);

static LRESULT CALLBACK ChatPanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
   switch (msg)
   {
   case WM_NCHITTEST:
      return PanelHitTest(hwnd, lp, TRUE);

   case WM_WINDOWPOSCHANGING:
      PanelSnap(hwnd, (WINDOWPOS *)lp);
      return 0;

   case WM_SIZE:
   {
      int w = LOWORD(lp), h = HIWORD(lp);
      int inputH = GetTextInputHeight();
      int inputY = h - inputH;
      int textH  = inputY - PANEL_DRAG_H;
      if (hwndInput)
         MoveWindow(hwndInput, 0, inputY, w, inputH * 6, TRUE);
      // hwndText (richedit) is a child; resize it too via EditBoxResize
      HWND hwndText = EditBoxWindow();
      if (hwndText)
         MoveWindow(hwndText, 0, PANEL_DRAG_H, w, max(textH, 0), TRUE);
      return 0;
   }

   case WM_PAINT:
   {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT r;
      GetClientRect(hwnd, &r);
      FillRect(hdc, &r, GetSysColorBrush(COLOR_WINDOW));
      EndPaint(hwnd, &ps);
      return 0;
   }

   case WM_ERASEBKGND:
      return 1;
   }
   return DefWindowProc(hwnd, msg, wp, lp);
}

static void CalculateWindowHeight(void)
{
   HDC hdc = GetDC(GetDesktopWindow());
   int oldMapMode = SetMapMode(hdc,MM_TEXT);
   HFONT hOldFont = (HFONT) SelectObject(hdc,GetFont(FONT_INPUT));
   TEXTMETRIC tm;

   GetTextMetrics(hdc,&tm);
   inputHeight = tm.tmHeight + tm.tmInternalLeading;
   SelectObject(hdc,hOldFont);
   SetMapMode(hdc,oldMapMode);
   ReleaseDC(GetDesktopWindow(),hdc);
}

int GetTextInputHeight(void)
{
   return inputHeight + 2*GetSystemMetrics(SM_CYEDGE);
}

/************************************************************************/
/*
 * TextInputCreate:  Create the text input box.
 */
void TextInputCreate(HWND hParent)
{
   HWND hwndEdit;

   // Create the floating chat popup panel.
   {
      static Bool classRegistered = False;
      if (!classRegistered)
      {
         WNDCLASSEX wc;
         memset(&wc, 0, sizeof(wc));
         wc.cbSize        = sizeof(wc);
         wc.lpfnWndProc   = ChatPanelWndProc;
         wc.hInstance     = hInst;
         wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
         wc.lpszClassName = "M59ChatPanel";
         RegisterClassEx(&wc);
         classRegistered = True;
      }

      RECT cr;
      GetClientRect(hParent, &cr);
      int ww = cr.right;
      int wh = CHAT_PANEL_CLIENT_H + PANEL_DRAG_H;
      POINT pt = {0, cr.bottom};
      ClientToScreen(hParent, &pt);
      hChatPanel = CreateWindowEx(0, "M59ChatPanel", NULL,
         WS_POPUP | WS_VISIBLE,
         pt.x, pt.y - wh, ww, wh,
         hParent, NULL, hInst, NULL);
      PanelRegister(hChatPanel);
      PanelLoadPos(hChatPanel, "Chat");
   }

   CalculateWindowHeight();
   {
      RECT chatCR;
      GetClientRect(hChatPanel, &chatCR);
      int inputH = GetTextInputHeight();
      int inputY = chatCR.bottom - inputH;
      hwndInput = CreateWindow("combobox", NULL,
                  WS_CHILD | WS_BORDER | WS_VISIBLE |
                  CBS_AUTOHSCROLL | CBS_DROPDOWN | WS_VSCROLL,
                  0, inputY, chatCR.right, inputH * 6,
                  hChatPanel, (HMENU) IDC_TEXTINPUT, hInst, NULL);
   }

   SetWindowFont(hwndInput, GetFont(FONT_INPUT), TRUE);
   CalculateWindowHeight();
   ComboBox_LimitText(hwndInput, MAXSAY);

   // The editbox is the first child of the combobox.
   // Subclass the editbox for key events.
   //
   hwndEdit = GetWindow(hwndInput, GW_CHILD);
   lpfnDefInputProc = SubclassWindow(hwndEdit, TextInputProc);
}
/************************************************************************/
/*
 * TextInputDestroy:  Destroy the edit box and the text buffer.
 */
void TextInputDestroy(void)
{
   DestroyWindow(hwndInput);
   PanelUnregister(hChatPanel);
   DestroyWindow(hChatPanel);
   hChatPanel = NULL;
}

void TextInputResetFont(void)
{
   SetWindowFont(hwndInput, GetFont(FONT_INPUT), TRUE);
   CalculateWindowHeight();
}

/************************************************************************/
/*
 * TextInputResize:  Resize the text input box when the main window is resized
 *   to (xsize, ysize).  view is the current grid area view.
 */
void TextInputResize(int xsize, int ysize, AREA view)
{
   CalculateWindowHeight();
   /* hwndInput is a child of hChatPanel which manages its own layout via WM_SIZE */
}

/************************************************************************/
void TextInputSetFocus(Bool forward)
{
   SetFocus(hwndInput);
   ComboBox_SetEditSel(hwndInput, 0, -1);  // select all text
}
/************************************************************************/
void TextInputDrawBorder(void)
{
   // Put border around edit box and text input box
   AREA a; //, edit_area;

//   EditBoxGetArea(&edit_area);
//   UnionArea(&a, &edit_area, &input_area);
   EditBoxGetArea(&a);
   a.cy += GetTextInputHeight();

//	Add space for edit treatment. Assuming same size as HIGHLIGHT_THICKNESS.
   a.x -= HIGHLIGHT_THICKNESS;
   a.y -= HIGHLIGHT_THICKNESS;
   a.cx += 2 * HIGHLIGHT_THICKNESS;
   a.cy += 2 * HIGHLIGHT_THICKNESS - 1; //There is no bottom treatment on editbox.

   if (IsChild(hwndInput, GetFocus()))
      DrawBorder(&a, HIGHLIGHT_INDEX, NULL);
   else 
      DrawBorder(&a, border_index, NULL);
}

/************************************************************************/
/*
 * TextInputSetText:  Set the contents of the text input box to the given string.
 *   If focus is True, set focus to text input box.
 */
void TextInputSetText(char *text, Bool focus)
{
   int len;

   ComboBox_SetText(hwndInput, text);
   len = ComboBox_GetTextLength(hwndInput);

   if (focus)
      SetFocus(hwndInput);

   ComboBox_SetEditSel(hwndInput, len, len);  // Move caret to end of text
}
/************************************************************************/
/*
 * TextInputProc:  Subclassed window procedure for text box.
 */
long CALLBACK TextInputProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   switch (message)
   {
   case WM_KEYDOWN:
      if (HANDLE_WM_KEYDOWN_BLAK(hwnd, wParam, lParam, TextInputKey) == True)
      	 return 0;
      break;

   case WM_CHAR:   // Avoid beep when Tab or Return or Escape pressed
      if (wParam == VK_TAB || wParam == VK_RETURN || wParam == VK_ESCAPE)
	 return 0;
      break;

   case WM_SETFOCUS:
   case WM_KILLFOCUS:
      skip_selection = True;
      TextInputDrawBorder();
      break;

   case EM_SETSEL:
      // Override default behavior of selecting all text when the control
      // gets the focus, or selecting (0, 0) when losing the focus.
      if (skip_selection)
      {
	 skip_selection = False;
	 return 0;
      }
      break;

   }
   return CallWindowProc(lpfnDefInputProc, hwnd, message, wParam, lParam);
}
/************************************************************************/
/*
 * TextInputKey:  User pressed a key on text input box.
 *   Return True iff key should NOT be passed on to Windows for default processing.
 */
Bool TextInputKey(HWND hwnd, UINT key, Bool fDown, int cRepeat, UINT flags)
{
   Bool held_down = (flags & 0x4000) ? True : False;  /* Is key being held down? */
   char string[MAXSAY + 1];
   int action;
   BOOL bValid;
   void *action_data;

   if (key == VK_RETURN && !held_down)
   {
      UserDidSomething();

      ComboBox_GetText(hwndInput, string, MAXSAY + 1);
      if (string[0] == 0)
	 return True;

      SetFocus(hMain);
      bValid = ParseGotText(string);

      // Add it to the history.
      if (*string && bValid)
      {
	 BOOL bAdd = TRUE;
	 int iCount;
	 char achHead[MAXSAY+1];
	 iCount = ComboBox_GetCount(hwndInput);
	 if (iCount > 0)
	 {
	    ComboBox_GetLBText(hwndInput, 0, achHead);
	    if (0 == strcmp(achHead, string))
	       bAdd = FALSE;
	 }
	 if (bAdd)
	 {
	    ComboBox_InsertString(hwndInput, 0, string);
	    if (iCount > EDITBOX_HISTORY)
	       ComboBox_DeleteString(hwndInput, iCount);
	 }
      }

      return True;
   }
   
   // Check for special keys
   action = TranslateKey(key, textin_key_table, &action_data);

   if (action == A_NOACTION)
      return False;

   PerformAction(action, action_data);

   return True;
}
