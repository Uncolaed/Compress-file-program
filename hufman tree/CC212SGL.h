#pragma once

#ifdef CC212SGL_EXPORTS
#define CC212SGL_API __declspec(dllexport)
#else
#define CC212SGL_API __declspec(dllimport)
#endif

#if       _WIN32_WINNT < 0x0500
#undef  _WIN32_WINNT
#define _WIN32_WINNT   0x0500
#endif

#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <ctype.h>
#include "Freeimage.h"

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "freeimage.lib")


CC212SGL_API enum COLORS {
	RED = 1, LIME = 2, BLUE = 3, BROWN = 4, YELLOW = 5, GREEN = 6, CYAN = 7, SKYBLUE = 8,
	MAGENTA = 9, ORANGE = 10, WHITE = 11, BLACK = 100
};


class CC212SGL
{
private:
	HWND console_handle;
	HDC device_context;
	HPEN pen;

	HDC mdc;
	HBITMAP mbmp;
	HBITMAP moldbmp;

	int penSize;
	int fontSize;
	int fontBoldness;
	COLORREF penColor;


	COLORREF convertFromColors(COLORS c);
	HFONT hFont;
	LOGFONT logfont;
	HFONT hNewFont;
	HFONT hOldFont;




public:
	CC212SGL_API CC212SGL();
	CC212SGL_API void setup();
	CC212SGL_API void drawLine(int xs, int ys, int xe, int ye);
	CC212SGL_API COLORREF generateFromRGB(int r, int g, int b);
	CC212SGL_API void setDrawingColor(COLORS c);
	CC212SGL_API void setDrawingColor(COLORREF c);

	CC212SGL_API void setDrawingThickness(int size);
	CC212SGL_API void setFontSizeAndBoldness(int size, int boldness);
	CC212SGL_API void resetFontSize();
	CC212SGL_API void beginDraw();
	CC212SGL_API void endDraw();
	CC212SGL_API void drawCircle(int xc, int yc, int rc);
	CC212SGL_API void drawEllipse(int xc, int yc, int xr, int yr);
	CC212SGL_API void drawSolidEllipse(int xc, int yc, int xr, int yr);
	CC212SGL_API void drawSolidCircle(int xc, int yc, int rc);
	CC212SGL_API void drawRectangle(int x, int y, int width, int height);
	CC212SGL_API  void drawSolidRectangle(int x, int y, int width, int height);
	CC212SGL_API void drawPixel(int x, int y);
	CC212SGL_API bool drawImage(int imgID, int x, int y, COLORREF transparentKey);
	CC212SGL_API int loadImage(const char* path);
	CC212SGL_API void resizeImage(int imgID, int w, int h);
	CC212SGL_API int deleteImage(int imgId);
	CC212SGL_API int getWindowWidth();
	CC212SGL_API  int getWindowHeight();
	CC212SGL_API void fillScreen(COLORS c);


	CC212SGL_API void drawText(int x, int y, const char* text);
	CC212SGL_API void moveCursor(int x, int y);

	CC212SGL_API void hideCursor();
	CC212SGL_API void showCursor();

	CC212SGL_API void setFullScreenMode();

	CC212SGL_API void setWindowScreenMode();
};



__declspec(dllexport) CC212SGL *CreateCC212SGL();