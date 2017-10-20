
#include <dwmapi.h>

#include "win32window.h"
#include "drawingitem.h"
#include "renderingcore.h"

Win32Window::windowmap	Win32Window::handles;
WNDCLASSEX				Win32Window::wc;
Guard					Win32Window::handlesguard;

LRESULT WINAPI Win32Window::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	handlesguard.Lock();

	Win32Window::windowmap::iterator it = Win32Window::handles.find(hWnd);
	Win32Window* window = 0;

	if( it != Win32Window::handles.end() )
		window = it->second;
	
	handlesguard.Unlock();

	if( window == 0 )
		return DefWindowProc(hWnd, msg, wParam, lParam);

	switch( msg )
	{
	case WM_CLOSE:
		ShowWindow(hWnd, SW_HIDE);

		if( window->CloseCallback )
			(*window->CloseCallback)(window);

		window->UninitOpenGL();
		break;

	case WM_DESTROY:
		PostQuitMessage(0);

		return 0;
	
	case WM_KEYUP:
		if( window->KeyPressCallback )
			window->KeyPressCallback(window, wParam);

		break;

	case WM_MOUSEMOVE: {
		short x = (short)(lParam & 0xffff);
		short y = (short)((lParam >> 16) & 0xffff);

		window->mousedx += x - window->mousex;
		window->mousedy += y - window->mousey;

		window->mousex = x;
		window->mousey = y;

		if( window->MouseMoveCallback )
			window->MouseMoveCallback(window);

		} break;

	case WM_LBUTTONDOWN:
		window->mousedown = 1;

		if( window->MouseMoveCallback )
			window->MouseMoveCallback(window);

		break;

	case WM_RBUTTONDOWN:
		window->mousedown = 2;

		if( window->MouseMoveCallback )
			window->MouseMoveCallback(window);

		break;

	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
		window->mousedown = 0;

		if( window->MouseMoveCallback )
			window->MouseMoveCallback(window);

		break;

	default:
		break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

Win32Window::Win32Window(long x, long y, long width, long height)
{
	if( handles.size() == 0 )
	{
		wc.cbSize			= sizeof(WNDCLASSEX);
		wc.style			= CS_OWNDC;
		wc.lpfnWndProc		= (WNDPROC)WndProc;
		wc.cbClsExtra		= 0L;
		wc.cbWndExtra		= 0L;
		wc.hInstance		= GetModuleHandle(NULL);
		wc.hIcon			= NULL;
		wc.hCursor			= LoadCursor(0, IDC_ARROW);
		wc.hbrBackground	= NULL;
		wc.lpszMenuName		= NULL;
		wc.lpszClassName	= "TestClass";
		wc.hIconSm			= NULL;

		RegisterClassEx(&wc);
	}

	KeyPressCallback	= 0;
	MouseMoveCallback	= 0;
	UpdateCallback		= 0;
	RenderCallback		= 0;
	CloseCallback		= 0;
	CreateCallback		= 0;

	RECT realrect;
	RECT rect = { x, y, width, height };
	DWORD style = WS_CLIPCHILDREN|WS_CLIPSIBLINGS|WS_SYSMENU|WS_BORDER|WS_CAPTION|WS_MINIMIZEBOX;

	rect.left	= x;
	rect.top	= y;
	rect.right	= x + width;
	rect.bottom	= y + height;

	hwnd = CreateWindowA("TestClass", "Win32Window", style,
		rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
		NULL, NULL, wc.hInstance, NULL);

	DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &realrect, sizeof(RECT));

	rect.left	= rect.left + (rect.left - realrect.left);
	rect.right	= rect.right - (realrect.right - rect.right);
	rect.top	= rect.top + (rect.top - realrect.top);
	rect.bottom	= rect.bottom - (realrect.bottom - rect.bottom);

	MoveWindow(hwnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, FALSE);

	hdc			= 0;
	glcontextid	= -1;
	drawingitem	= 0;

	if( hwnd )
	{
		hdc = GetDC(hwnd);
		glcontextid = GetRenderingCore()->CreateUniverse(hdc);

		if( glcontextid != -1 )
		{
			drawingitem = new DrawingItem(
				glcontextid,
				(unsigned int)(rect.right - rect.left),
				(unsigned int)(rect.bottom - rect.top));
		}
	}

	handlesguard.Lock();
	handles.insert(windowmap::value_type(hwnd, this));
	handlesguard.Unlock();
}

Win32Window::~Win32Window()
{
	ReleaseDC(hwnd, hdc);

	handlesguard.Lock();
	handles.erase(hwnd);

	hdc = 0;

	if( handles.size() == 0 )
		UnregisterClass("TestClass", wc.hInstance);

	handlesguard.Unlock();
}

void Win32Window::UninitOpenGL()
{
	if( drawingitem )
		delete drawingitem;

	if( glcontextid != -1 )
	{
		GetRenderingCore()->DeleteUniverse(glcontextid);
		glcontextid = -1;

		DestroyWindow(hwnd);
	}
}

void Win32Window::SetTitle(const char* title)
{
	if( hwnd )
		::SetWindowText(hwnd, title);
}

void Win32Window::SetFocus()
{
	if( hwnd )
		::SetFocus(hwnd);
}

void Win32Window::Close()
{
	if( hwnd )
		SendMessage(hwnd, WM_CLOSE, 0, 0);
}

void Win32Window::MessageHook()
{
	LARGE_INTEGER	qwTicksPerSec = { 0, 0 };
	LARGE_INTEGER	qwTime;
	LONGLONG		tickspersec;
	MSG				msg;
	POINT			p;
	double			last, current;
	double			delta, accum = 0;
	bool			render = true;

	if( CreateCallback )
		(*CreateCallback)(this);

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

	ZeroMemory(&msg, sizeof(msg));

	GetCursorPos(&p);
	ScreenToClient(hwnd, &p);
	
	QueryPerformanceFrequency(&qwTicksPerSec);
	tickspersec = qwTicksPerSec.QuadPart;

	QueryPerformanceCounter(&qwTime);
	last = (qwTime.QuadPart % tickspersec) / (double)tickspersec;

	while( msg.message != WM_QUIT )
	{
		QueryPerformanceCounter(&qwTime);

		current = (qwTime.QuadPart % tickspersec) / (double)tickspersec;

		if (current < last)
			delta = ((1.0 + current) - last);
		else
			delta = (current - last);

		last = current;
		accum += delta;

		mousedx = mousedy = 0;

		while( accum > 0.1f )
		{
			accum -= 0.1f;

			while( PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE) )
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);

				if( msg.message == WM_CLOSE )
					render = false;

				if( msg.message == WM_QUIT )
					break;
			}

			if( msg.message == WM_QUIT )
				break;

			if( render && UpdateCallback )
				UpdateCallback(0.1f);
		}

		if( render && msg.message != WM_QUIT )
		{
			if( RenderCallback && glcontextid != -1 )
				RenderCallback(this, (float)accum / 0.1f, (float)delta);
		}
	}
}
