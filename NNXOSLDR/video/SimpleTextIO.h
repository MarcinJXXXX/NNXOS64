#ifndef NNX_STIO_HEADER
#define NNX_STIO_HEADER
#ifdef __cplusplus
extern "C"
{
#endif

#include <nnxtype.h>
#include "nnxarg.h"
#include "../nnxcfg.h"

	void TextIoSetBoundingBox(UINT32 *boundingBox);
	void TextIoGetBoundingBox(UINT32 *boundingBox);
	void TextIoSetCursorPosition(UINT32 posX, UINT32 posY);
	void TextIoGetCursorPosition(UINT32* posX, UINT32* posY);
	void TextIoSetColorInformation(UINT32 color, UINT32 background, UINT8 renderBack);
	void TextIoGetColorInformation(UINT32 *color, UINT32* background, UINT8 *renderBack);
	void TextIoSetAlignment(UINT8 alignment);
	void TextIoInitialize(UINT32* framebufferIn, UINT32* framebufferEndIn, UINT32 w, UINT32 h, UINT32 p);
	void TextIoOutputCharacter(UINT8 characterID, UINT32 posX, UINT32 posY, UINT32 color, UINT32 backdrop, UINT8 renderBackdrop);
	void TextIoOutputFormatedString(char* input, UINT32 size, va_list args2);
	void TextIoTest(UINT64 mode);
	UINT8 TextIoGetAlignment();
	UINT8 TextIoIsInitialized();
	void TextIoOutputCharacterWithinBox(UINT8 characterID, UINT32 posX, UINT32 posY, UINT32 color, UINT32 backdrop, UINT8 renderBackdrop, UINT32 minX, UINT32 maxX, UINT32 minY, UINT32 maxY);
	void PrintTA(const char* input, ...);
	void TextIoOutputStringGlobal(const char* input);
	void TextIoOutputString(const char* input, UINT32 posX, UINT32 posY, UINT32 color, UINT32 backdrop, UINT8 renderBackdrop, UINT32 minX, UINT32 maxX, UINT32 minY, UINT32 maxY);
#ifdef VERBOSE
#define PrintT PrintTA("<%s %i>: ",__FILE__, __LINE__);PrintTA
#else
#define PrintT PrintTA
#endif

#ifdef PRINT_IN_DEBUG_ONLY
#ifndef DEBUG
	inline void null(const char* input, ...)
	{
		return;
	}
#define PrintT null
#endif
#endif



	void TextIoClear();
	UINT64 FrameBufferSize();

	extern UINT32* gFramebuffer;
	extern UINT32* gFramebufferEnd;
	extern UINT32 gWidth;
	extern UINT32 gHeight;
	extern UINT32 gPixelsPerScanline;

	extern UINT32 TextIoDeltaX;
	extern UINT32 TextIoDeltaY;


#ifdef __cplusplus
}
#endif
#endif
