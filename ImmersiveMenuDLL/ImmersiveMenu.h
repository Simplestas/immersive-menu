#pragma once
#include <windows.h>

extern "C"
{
	BOOL WINAPI ImmersiveTrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, CONST RECT *prcRect);
	void WINAPI DisableImmersiveMenu(HWND hWnd);
	void WINAPI EnableImmersiveMenu(HWND hWnd);
}
