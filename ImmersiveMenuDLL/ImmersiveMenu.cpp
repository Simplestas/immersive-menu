#include "ImmersiveMenu.h"
#include <ShellScalingApi.h>
#include <Uxtheme.h>
#include <vssym32.h>

static const UINT EXTRAHEIGHT = 7;
static const UINT EXTRAHEIGHTDARK = 16;
static const UINT EXTRAWIDTH = 32;
static const UINT CHECKSIZE = 32;
static const UINT ARROWSIZE = 16;
static const UINT EXTRAPADDING = 2;

static bool isUsingDarkMenu()
{
	// Remember there is no dark menu before Win10 1511
	return true;
}

int GetDPI()
{
	static int iDisplayDPI;
	if (iDisplayDPI) return iDisplayDPI;
	HDC hDC = GetDC(0);
	iDisplayDPI = GetDeviceCaps(hDC, LOGPIXELSX);
	ReleaseDC(0, hDC);
	return iDisplayDPI;
}

int GetEffectiveDPI(HWND hwnd)
{
	// There is GetDpiForWindow but only since Win10 1607
	UINT dpi;
	HRESULT hr = GetDpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), MDT_EFFECTIVE_DPI, &dpi, &dpi);
	//dbgprintf(L"GetDpiForMonitor = %d %x", dpi, hr);
	if (SUCCEEDED(hr))
		return dpi;
	else
		return GetDPI();
}

void DrawCompositedText(HDC hdc, LPCWSTR text, UINT cch, LPRECT rect, DWORD flags, DWORD color)
{
	HTHEME wndtheme = OpenThemeData(NULL, L"CompositedWindow::Window");
	DTTOPTS opts = { sizeof(DTTOPTS) };
	opts.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
	opts.crText = color;
	DrawThemeTextEx(wndtheme, hdc, 0, 0, text, cch, flags, rect, &opts);

	CloseThemeData(wndtheme);
}

static DWORD getImmersiveMenuColor(int partId, int stateId, int propId)
{
	HTHEME theme = NULL;
	if (isUsingDarkMenu())
		theme = OpenThemeData(NULL, L"ImmersiveStartDark::Menu");
	if (!theme)
		theme = OpenThemeData(NULL, L"ImmersiveStart::Menu");

	DWORD color = 0xFF00FF;
	GetThemeColor(theme, partId, stateId, propId, &color);
	CloseThemeData(theme);
	return color;
}

static void drawImmersiveMenu(HDC hdc, LPRECT rc, int partId, int stateId)
{
	HTHEME theme = NULL;
	if (isUsingDarkMenu())
		theme = OpenThemeData(NULL, L"ImmersiveStartDark::Menu");
	if (!theme)
		theme = OpenThemeData(NULL, L"ImmersiveStart::Menu");

	DrawThemeBackground(theme, hdc, partId, stateId, rc, NULL);
	CloseThemeData(theme);
}

static LONG g_brushRef;
static HBRUSH g_immersiveBrush;
static HBRUSH getImmersiveMenuBrush()
{
	g_brushRef++;
	if (!g_immersiveBrush)
		g_immersiveBrush = CreateSolidBrush(getImmersiveMenuColor(MENU_POPUPBACKGROUND, 0, TMT_FILLCOLOR));
	return g_immersiveBrush;
}

static void releaseImmersiveMenuBrush()
{
	g_brushRef--;
	if (!g_brushRef)
	{
		DeleteObject(g_immersiveBrush);
		g_immersiveBrush = 0;
	}
}

typedef struct
{
	HBITMAP bitmap;
	WCHAR text[200];
	DWORD state;
	DWORD type;
	BOOL submenu;
	ULONG_PTR data;
	BOOL first;
	BOOL last;
} IMMERSIVE_MENU_ITEM;

int getMenuItemIndex(HDPA items, ULONG_PTR data)
{
	int j = 0;
	while (IMMERSIVE_MENU_ITEM *item = (IMMERSIVE_MENU_ITEM *)DPA_GetPtr(items, j))
	{
		if ((ULONG_PTR)item == data)
			return j;

		j++;
	}
	return -1;
}

void makeMenuImmersive(HMENU hMenu, HDPA items)
{
	MENUINFO mi = { sizeof(MENUINFO) };
	mi.fMask = MIM_BACKGROUND;
	mi.hbrBack = getImmersiveMenuBrush();
	SetMenuInfo(hMenu, &mi);
	for (int i = 0; i < GetMenuItemCount(hMenu); i++)
	{
		MENUITEMINFO mi = { sizeof(MENUITEMINFO) };
		mi.fMask = MIIM_FTYPE;
		GetMenuItemInfo(hMenu, i, TRUE, &mi);
		if (mi.fType & MFT_OWNERDRAW)
			continue;

		IMMERSIVE_MENU_ITEM *item = (IMMERSIVE_MENU_ITEM *)malloc(sizeof(IMMERSIVE_MENU_ITEM));
		DPA_AppendPtr(items, item);
		mi.dwTypeData = item->text;
		mi.cch = ARRAYSIZE(item->text);
		mi.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_ID | MIIM_STRING | MIIM_BITMAP | MIIM_STATE | MIIM_DATA;
		GetMenuItemInfo(hMenu, i, TRUE, &mi);
		item->bitmap = mi.hbmpItem;
		item->state = mi.fState;
		item->type = mi.fType;
		item->data = mi.dwItemData;
		item->submenu = !!mi.hSubMenu;
		item->first = (i == 0);
		item->last = (i == GetMenuItemCount(hMenu) - 1);
		//dbgprintf("got item %ws %x", item->text, item->type);

		mi.fType |= MFT_OWNERDRAW;
		mi.hbmpItem = 0;
		mi.dwItemData = (ULONG_PTR)item;
		mi.fMask = MIIM_FTYPE | MIIM_BITMAP | MIIM_DATA;
		SetMenuItemInfo(hMenu, i, TRUE, &mi);
	}
}

void unmakeMenuImmersive(HMENU hMenu, HDPA items)
{
	releaseImmersiveMenuBrush();
	for (int i = 0; i < GetMenuItemCount(hMenu); i++)
	{
		MENUITEMINFO mi = { sizeof(MENUITEMINFO) };
		mi.fMask = MIIM_DATA;
		GetMenuItemInfo(hMenu, i, TRUE, &mi);

		int idx = getMenuItemIndex(items, mi.dwItemData);
		if (idx >= 0)
		{
			IMMERSIVE_MENU_ITEM *item = (IMMERSIVE_MENU_ITEM *)mi.dwItemData;
			mi.fType &= ~MFT_OWNERDRAW;
			mi.hbmpItem = item->bitmap;
			mi.dwItemData = item->data;
			mi.fMask = MIIM_FTYPE | MIIM_BITMAP | MIIM_DATA;
			SetMenuItemInfo(hMenu, i, TRUE, &mi);

			free(item);
			DPA_DeletePtr(items, idx);
		}
	}
}

HFONT CreateScaledMenuFont(HWND hWnd, BOOL bold)
{
	NONCLIENTMETRICS metrics = { sizeof(NONCLIENTMETRICS) };
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics, 0);
	metrics.lfMenuFont.lfHeight = MulDiv(metrics.lfMenuFont.lfHeight, GetEffectiveDPI(hWnd), GetDPI());
	if (bold)
		metrics.lfMenuFont.lfWeight = FW_BOLD;
	return CreateFontIndirect(&metrics.lfMenuFont);
}

HFONT CreateScaledSymbolFont(HWND hWnd)
{
	NONCLIENTMETRICS metrics = { sizeof(NONCLIENTMETRICS) };
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics, 0);
	int iSize = MulDiv(metrics.lfMenuFont.lfHeight, GetEffectiveDPI(hWnd) * 11, GetDPI() * 10);
	return CreateFont(iSize, 0, 0, 0, FW_DONTCARE, 0, 0, 0, DEFAULT_CHARSET, 0, 0, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
}

static LRESULT CALLBACK s_SubclassForMenuProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR pThis, DWORD_PTR dwRefData)
{
	HDPA items = (HDPA)dwRefData;
	int M = GetEffectiveDPI(hWnd);

	if (uMsg == WM_DESTROY)
	{
		DisableImmersiveMenu(hWnd);
	}
	if (uMsg == WM_INITMENUPOPUP)
	{
		LRESULT ret = DefSubclassProc(hWnd, uMsg, wParam, lParam);
		makeMenuImmersive((HMENU)wParam, items);
		return ret;
	}
	if (uMsg == WM_UNINITMENUPOPUP)
	{
		LRESULT ret = DefSubclassProc(hWnd, uMsg, wParam, lParam);
		unmakeMenuImmersive((HMENU)wParam, items);
		return ret;
	}
	if (uMsg == WM_MEASUREITEM)
	{
		MEASUREITEMSTRUCT *ms = (MEASUREITEMSTRUCT *)lParam;
		IMMERSIVE_MENU_ITEM *item = (IMMERSIVE_MENU_ITEM *)ms->itemData;
		if (ms->CtlType == ODT_MENU && (getMenuItemIndex(items, ms->itemData) >= 0))
		{
			if (item->type & MFT_SEPARATOR)
			{
				ms->itemHeight = 7; // no hidpi here
			}
			else
			{
				HFONT menuFnt = CreateScaledMenuFont(hWnd, item->state & MF_DEFAULT);
				HDC tempDC = CreateCompatibleDC(0);
				SelectObject(tempDC, menuFnt);
				SIZE s;
				GetTextExtentExPoint(tempDC, item->text, lstrlen(item->text), 0, NULL, NULL, &s);
				DeleteObject(menuFnt);
				DeleteDC(tempDC);
				ms->itemWidth = s.cx + MulDiv(CHECKSIZE + EXTRAWIDTH, M, 96);
				if (!isUsingDarkMenu())
					ms->itemHeight = s.cy + MulDiv(EXTRAHEIGHT, M, 96);
				else
					ms->itemHeight = s.cy + MulDiv(EXTRAHEIGHTDARK, M, 96);

				if (item->submenu)
					ms->itemWidth += MulDiv(ARROWSIZE, M, 96);
			}
			if (item->first || item->last)
			{
				ms->itemHeight += EXTRAPADDING;
			}
			return TRUE;
		}
	}
	else if (uMsg == WM_DRAWITEM)
	{
		DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lParam;
		IMMERSIVE_MENU_ITEM *item = (IMMERSIVE_MENU_ITEM *)draw->itemData;
		if (draw->CtlType == ODT_MENU && (getMenuItemIndex(items, draw->itemData) >= 0))
		{
			if (item->first)
				draw->rcItem.top += EXTRAPADDING;
			if (item->last)
				draw->rcItem.bottom -= EXTRAPADDING;

			if (item->type & MFT_SEPARATOR)
			{
				drawImmersiveMenu(draw->hDC, &draw->rcItem, MENU_POPUPITEM, MPI_NORMAL);
				InflateRect(&draw->rcItem, -8, -1);
				//draw->rcItem.left = MulDiv(CHECKSIZE, M, 96);
				//FillRectRGB(draw->hDC, &draw->rcItem, SEPARATORCOLOR);
				//drawImmersiveMenu(draw->hDC, &draw->rcItem, MENU_POPUPITEM, 0);
				drawImmersiveMenu(draw->hDC, &draw->rcItem, MENU_POPUPSEPARATOR, 1);
			}
			else // normal item
			{
				HDC bufferDC;
				BP_PAINTPARAMS bufpar = { 0 };
				bufpar.cbSize = sizeof(BP_PAINTPARAMS);
				bufpar.dwFlags = BPPF_ERASE;
				HPAINTBUFFER paintBuffer = BeginBufferedPaint(draw->hDC, &draw->rcItem, BPBF_TOPDOWNDIB, &bufpar, &bufferDC);

				int stateId = MPI_NORMAL;
				if (draw->itemState & ODS_SELECTED)
					stateId = MPI_HOT;
				if (draw->itemState & ODS_DISABLED)
					stateId = MPI_DISABLED;

				RECT rcDraw = draw->rcItem;
				drawImmersiveMenu(bufferDC, &draw->rcItem, MENU_POPUPITEM, stateId);

				DWORD textColor = getImmersiveMenuColor(MENU_POPUPITEM, stateId, TMT_TEXTCOLOR);
				/*DWORD bgColor = getImmersiveMenuColor(MENU_POPUPBACKGROUND, TMT_FILLCOLOR);
				if (draw->itemState & ODS_SELECTED)
				bgColor = SELECTEDCOLOR;

				DWORD textColor = 0;
				if (draw->itemState & ODS_DISABLED)
				textColor = DISABLEDCOLOR;

				FillRectRGB(bufferDC, &rcDraw, bgColor);*/

				// draw check symbol & dropdown glyph
				if (item->state & MF_CHECKED || item->submenu)
				{
					HFONT hSymbolFnt = CreateScaledSymbolFont(hWnd);
					HGDIOBJ oldFnt = SelectObject(bufferDC, hSymbolFnt);

					if (item->state & MF_CHECKED)
					{
						RECT rc = rcDraw;
						rc.right = rc.left + MulDiv(CHECKSIZE, M, 96);
						WCHAR symbol = 0xE0E7;
						DrawCompositedText(bufferDC, &symbol, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, textColor);
					}
					if (item->submenu)
					{
						RECT rc = rcDraw;
						rc.left = rc.right - MulDiv(ARROWSIZE, M, 96);
						WCHAR symbol = GetLayout(draw->hDC) ? 0xE0E2 : 0xE0E3;
						DrawCompositedText(bufferDC, &symbol, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE, textColor);
					}
					DeleteObject(SelectObject(bufferDC, oldFnt));
				}

				// draw bitmap
				if (item->bitmap)
				{
					RECT rc = rcDraw;
					rc.right = rc.left + MulDiv(CHECKSIZE, M, 96);
					BITMAP b;
					GetObject(item->bitmap, sizeof(b), &b);
					HDC tempDC = CreateCompatibleDC(0);
					HGDIOBJ oldBM = SelectObject(tempDC, item->bitmap);
					int x = rc.left + (rc.right - rc.left - b.bmWidth) / 2;
					int y = rc.top + (rc.bottom - rc.top - b.bmHeight) / 2;
					BLENDFUNCTION bf = { 0 };
					bf.SourceConstantAlpha = 255;
					if (b.bmBits && b.bmBitsPixel == 32)
						bf.AlphaFormat = AC_SRC_ALPHA;
					GdiAlphaBlend(bufferDC, x, y, b.bmWidth, b.bmHeight, tempDC, 0, 0, b.bmWidth, b.bmHeight, bf);
					SelectObject(tempDC, oldBM);
					DeleteDC(tempDC);
				}

				// draw text
				RECT rc = rcDraw;
				rc.left += MulDiv(CHECKSIZE, M, 96);
				HFONT menuFnt = CreateScaledMenuFont(hWnd, item->state & MF_DEFAULT);
				HGDIOBJ oldFnt = SelectObject(bufferDC, menuFnt);
				DWORD flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
				if (draw->itemState & ODS_NOACCEL)
					flags |= DT_HIDEPREFIX;

				DrawCompositedText(bufferDC, item->text, lstrlen(item->text), &rc, flags, textColor);

				DeleteObject(SelectObject(bufferDC, oldFnt));

				EndBufferedPaint(paintBuffer, TRUE);

				// forbid menu drawing classic shit
				ExcludeClipRect(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom);
			}
			return TRUE;
		}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void WINAPI EnableImmersiveMenu(HWND hWnd)
{
	// TODO: do not enable for high contrast
	HDPA items = DPA_Create(1);
	SetWindowSubclass(hWnd, s_SubclassForMenuProc, (UINT_PTR)s_SubclassForMenuProc, (DWORD_PTR)items);
}

int CALLBACK destroyImmItem(void *p, void *pData)
{
	free(p);
	return 1;
}

void WINAPI DisableImmersiveMenu(HWND hWnd)
{
	//#error Crash
	HDPA items = NULL;
	GetWindowSubclass(hWnd, s_SubclassForMenuProc, (UINT_PTR)s_SubclassForMenuProc, (DWORD_PTR *)&items);
	RemoveWindowSubclass(hWnd, s_SubclassForMenuProc, (UINT_PTR)s_SubclassForMenuProc);
	if (items)
		DPA_DestroyCallback(items, destroyImmItem, NULL);
}

BOOL WINAPI ImmersiveTrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, CONST RECT *prcRect)
{
	EnableImmersiveMenu(hWnd);
	BOOL ret = TrackPopupMenu(hMenu, uFlags, x, y, nReserved, hWnd, prcRect);
	DisableImmersiveMenu(hWnd);
	return ret;
}
