/*
 *  GUI core elements
 *
 *  Copyright (C) 2016
 *    Sandor Zsuga (Jubatian)
 *  Uzem (the base of CUzeBox) is copyright (C)
 *    David Etherton,
 *    Eric Anderton,
 *    Alec Bourque (Uze),
 *    Filipe Rinaldi,
 *    Sandor Zsuga (Jubatian),
 *    Matt Pandina (Artcfox)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/



#include "guicore.h"
#include "cu_ufile.h"
#ifdef ENABLE_DISPLAY_FILTERS
#include "filters.h"
#endif
#ifdef ENABLE_MICROUI
#include "microui/mui_integration.h"
#endif


/* Display initialized internal marker in flags */
#define GUICORE_INIT 0x10000U

/* Window dimensions, double scan */
#define WND_W    640U
#define WND_H    560U
/* Window dimensions, single scan */
#define WNDS_W   640U
#define WNDS_H   560U
/* Window dimensions, double scan, game only */
#define WNDG_W   620U
#define WNDG_H   456U
/* Window dimensions, single scan, game only */
#define WNDSG_W  620U
#define WNDSG_H  456U

/* Texture dimensions, double scan */
#define TEX_W    640U
#define TEX_H    560U
/* Texture dimensions, single scan */
#define TEXS_W   320U
#define TEXS_H   280U
/* Texture dimensions, double scan, game only */
#define TEXG_W   620U
#define TEXG_H   456U
/* Texture dimensions, single scan, game only */
#define TEXSG_W  310U
#define TEXSG_H  228U

/* Target pixel buffer offsets for game only */
#define TGOG_X   5U
#define TGOG_Y   19U



#ifdef USE_SDL1

/* SDL screen */
static SDL_Surface*  guicore_surface;

#else

/* SDL window */
SDL_Window*   guicore_window;
/* SDL renderer */
static SDL_Renderer* guicore_renderer;
/* SDL texture which is used to interface with the renderer */
static SDL_Texture*  guicore_texture;

#endif

/* Target pixel buffer */
static uint32        guicore_pixels[640U * 280U];

/* Physical Uzebox status LED (ATmega644 PD4), composited after the normal
** HUD/game render into the final texture. */
static boole         guicore_status_led_on = FALSE;

/* Logical 320x280 HUD position, in the unused gap between 100% and 1P. */
#define GUICORE_STATUS_LED_X 142U
#define GUICORE_STATUS_LED_Y   2U

/* Uzebox palette */
static uint32        guicore_palette[256];

/* Current 16 x 16 Uzebox palette window icon */
static uint8         guicore_window_icon[256];
static boole         guicore_window_icon_valid = FALSE;
static SDL_Surface*  guicore_icon_surface = NULL;

/* Fallback generic Uzebox window icon (RGBA8888, 32 x 32) */
static const uint32  guicore_generic_icon_rgba[32U * 32U] = {
 0x000000FFU, 0xB60000FFU, 0xB30000FFU, 0x000000FFU, 0xB30000FFU, 0x000000FFU, 0xB90000FFU, 0x000000FFU, 
 0xB60000FFU, 0xBC0D0DFFU, 0xC10D0DFFU, 0x000000FFU, 0xBD1616FFU, 0xBD1C1CFFU, 0xC22828FFU, 0x000000FFU, 
 0xC12424FFU, 0xC13232FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0xB93E3EFFU, 0x000000FFU, 0x000000FFU, 
 0xBF5252FFU, 0x000000FFU, 0xB44040FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x090909FFU, 0x515151FFU, 
 0xAB0000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0xB10000FFU, 0x220000FFU, 0xB60303FFU, 0x000000FFU, 
 0x220000FFU, 0x220000FFU, 0xBB1A1AFFU, 0x310000FFU, 0xB92020FFU, 0x220000FFU, 0x220000FFU, 0x220000FFU, 
 0xBF2A2AFFU, 0x000000FFU, 0xBE3232FFU, 0x020202FFU, 0xBF3F3FFFU, 0x020202FFU, 0xBD4444FFU, 0x010101FFU, 
 0xB74A4AFFU, 0x000000FFU, 0xB03A3AFFU, 0x000000FFU, 0x1D1D1DFFU, 0x5A5A5AFFU, 0xD4C3C3FFU, 0x515151FFU, 
 0x9E0000FFU, 0x000000FFU, 0x220000FFU, 0x310000FFU, 0xA50303FFU, 0x470000FFU, 0xAA1818FFU, 0x470000FFU, 
 0x470000FFU, 0xAA2F2FFFU, 0x470000FFU, 0x560000FFU, 0xAF3535FFU, 0xAD3C3CFFU, 0x310000FFU, 0x470000FFU, 
 0xB03A3AFFU, 0xAF4343FFU, 0x3D0000FFU, 0x560000FFU, 0xAF4C4CFFU, 0x470000FFU, 0xB04D4DFFU, 0x320303FFU, 
 0x230303FFU, 0xA74545FFU, 0x202020FFU, 0xC1C1C1FFU, 0xDCD1D1FFU, 0xD6CACAFFU, 0x707070FFU, 0x000000FFU, 
 0xAB0000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0xB51B1BFFU, 0x220000FFU, 0xBC2626FFU, 0x000000FFU, 
 0xBC3636FFU, 0x220000FFU, 0x220000FFU, 0x310000FFU, 0xC03C3CFFU, 0x220000FFU, 0x220000FFU, 0x220000FFU, 
 0xC54343FFU, 0x060606FFU, 0xC44A4AFFU, 0x250909FFU, 0xC24C4CFFU, 0x260D0DFFU, 0xBE4646FFU, 0x270F0FFFU, 
 0xB44040FFU, 0xD4D2D2FFU, 0xAC2828FFU, 0xDED8D8FFU, 0xD8D1D1FFU, 0xC4BFBFFFU, 0x080808FFU, 0x000000FFU, 
 0x000000FFU, 0xB61E1EFFU, 0xB52B2BFFU, 0x000000FFU, 0xB73434FFU, 0xC13F3FFFU, 0xBF4141FFU, 0x000000FFU, 
 0xBB3C3CFFU, 0xC64949FFU, 0xC94A4AFFU, 0x000000FFU, 0xC14040FFU, 0xC95050FFU, 0xC94C4CFFU, 0x121212FFU, 
 0xCA4C4CFFU, 0xC64949FFU, 0x0D0D0DFFU, 0x391818FFU, 0x350D0DFFU, 0xBB4646FFU, 0x4A4040FFU, 0xDEDCDCFFU, 
 0xBC4A4AFFU, 0xE2E0E0FFU, 0xB44040FFU, 0xDBD9D9FFU, 0xD5D3D3FFU, 0x222222FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x010101FFU, 0x010101FFU, 0x230404FFU, 0x220101FFU, 0x260C0CFFU, 0x191919FFU, 0x2E1D1DFFU, 
 0x151515FFU, 0x3D2222FFU, 0x3D2222FFU, 0x541919FFU, 0x615454FFU, 0xD5D5D5FFU, 0xE0E0E0FFU, 0xE4E0E0FFU, 
 0xE2E0E0FFU, 0xDEDEDEFFU, 0xD9D9D9FFU, 0xDBD6D6FFU, 0x3A3A3AFFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x161616FFU, 
 0x141414FFU, 0x141414FFU, 0x000000FFU, 0x232323FFU, 0x220000FFU, 0x381616FFU, 0x342525FFU, 0x4A2828FFU, 
 0x3D2222FFU, 0x472323FFU, 0x686262FFU, 0xD9CDCDFFU, 0xDED8D8FFU, 0xDCDADAFFU, 0xE0E0E0FFU, 0xE0E0E0FFU, 
 0xE0E0E0FFU, 0xE0DBDBFFU, 0xDDD8D8FFU, 0x6C6C6CFFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x141414FFU, 0x000000FFU, 0x171717FFU, 0x000000FFU, 0x090909FFU, 
 0x220000FFU, 0x291414FFU, 0x312121FFU, 0x3F2424FFU, 0x332424FFU, 0x453030FFU, 0x262626FFU, 0x422A2AFFU, 
 0x606060FFU, 0xDED6D6FFU, 0xE0D6D6FFU, 0xDFD4D4FFU, 0xDDDBDBFFU, 0xE3E3E3FFU, 0xE3E3E3FFU, 0xE5E1E1FFU, 
 0xE5DFDFFFU, 0xE4DCDCFFU, 0xD9D7D7FFU, 0x050505FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x171717FFU, 0x0F0F0FFFU, 0x232323FFU, 
 0x141414FFU, 0x141414FFU, 0x291414FFU, 0x362828FFU, 0x2F1E1EFFU, 0x423737FFU, 0x777777FFU, 0xDDD9D9FFU, 
 0xDAD1D1FFU, 0xD8CDCDFFU, 0xDCD1D1FFU, 0xDFD7D7FFU, 0xE0DDDDFFU, 0xDEDEDEFFU, 0xDADADAFFU, 0xE0D9D9FFU, 
 0xE3DDDDFFU, 0xE0DCDCFFU, 0x0E0E0EFFU, 0x020202FFU, 0x010101FFU, 0x101010FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x1B1B1BFFU, 0x000000FFU, 0x000000FFU, 0x020202FFU, 0x090909FFU, 
 0x220000FFU, 0x2C1919FFU, 0x141414FFU, 0x2B2B2BFFU, 0xC7C7C7FFU, 0xDCDADAFFU, 0xD7D5D5FFU, 0xDCD3D3FFU, 
 0xD1CECEFFU, 0xD7D1D1FFU, 0xE1DDDDFFU, 0xE1DFDFFFU, 0xDFDFDFFFU, 0xDCDADAFFU, 0xDDDDDDFFU, 0xE3DFDFFFU, 
 0xE5E3E3FFU, 0x2F2F2FFFU, 0x191919FFU, 0x260B0BFFU, 0x191919FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x0A0A0AFFU, 0x242424FFU, 0xC9C6C6FFU, 0xD2D2D2FFU, 0xD4D4D4FFU, 0xD8D6D6FFU, 0xD5D0D0FFU, 0x625C5CFFU, 
 0x555555FFU, 0xE5E5E5FFU, 0xE3E3E3FFU, 0xE2E2E2FFU, 0xE0E0E0FFU, 0xE4E4E4FFU, 0xE6E6E6FFU, 0xE9E7E7FFU, 
 0x656565FFU, 0x2D1A1AFFU, 0x1F1F1FFFU, 0x090909FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x020202FFU, 0x6E6E6EFFU, 
 0xBDBABAFFU, 0xC5C5C5FFU, 0xCDCDCDFFU, 0xD7D7D7FFU, 0xCECECEFFU, 0x4E4646FFU, 0x2F1E1EFFU, 0x432C2CFFU, 
 0xE4E4E4FFU, 0xE2E2E2FFU, 0xE4E4E4FFU, 0xE3E3E3FFU, 0xE5E5E5FFU, 0xE6E6E6FFU, 0xE7E7E7FFU, 0xD0D0D0FFU, 
 0x222222FFU, 0x232323FFU, 0x111111FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x6C6C6CFFU, 0xA9A9A9FFU, 0xB3B3B3FFU, 
 0xBFBFBFFFU, 0xCCCCCCFFU, 0xC5C5C5FFU, 0x434343FFU, 0x171717FFU, 0x1A1A1AFFU, 0x2C2C2CFFU, 0xD7D7D7FFU, 
 0xDFDFDFFFU, 0xDFDFDFFFU, 0xDDDDDDFFU, 0xDEDEDEFFU, 0xDFDFDFFFU, 0xE2E2E2FFU, 0xDBDBDBFFU, 0x252525FFU, 
 0x1F1F1FFFU, 0x010101FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x818181FFU, 0x9E9E9EFFU, 0xA8A8A8FFU, 0xAFAFAFFFU, 0xBFBFBFFFU, 
 0x646464FFU, 0x2C2C2CFFU, 0x0F0F0FFFU, 0x0F0F0FFFU, 0x111111FFU, 0x252525FFU, 0xB5B5B5FFU, 0xDDDDDDFFU, 
 0xD9D9D9FFU, 0xD8D8D8FFU, 0xD7D7D7FFU, 0xDBDBDBFFU, 0xDFDFDFFFU, 0xE2E2E2FFU, 0x393939FFU, 0x1F1F1FFFU, 
 0x050505FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x5B5B5BFFU, 0x999999FFU, 0xA1A1A1FFU, 0x9C9C9CFFU, 0xA5A5A5FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x090909FFU, 0x232323FFU, 0x575757FFU, 0xE1E1E1FFU, 0xDFDFDFFFU, 
 0xDBDBDBFFU, 0xDBDBDBFFU, 0xDDDDDDFFU, 0xE0E0E0FFU, 0xE3E3E3FFU, 0x585858FFU, 0x191919FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x141414FFU, 0x6D6D6DFFU, 0xC2C2C2FFU, 0xB4B4B4FFU, 0x969696FFU, 
 0x858585FFU, 0x858585FFU, 0x030303FFU, 0x030303FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x070707FFU, 0x262626FFU, 0x373737FFU, 0xDEDEDEFFU, 0xD9D9D9FFU, 0xD7D7D7FFU, 
 0xD7D7D7FFU, 0xDADADAFFU, 0xDBDBDBFFU, 0xDDDDDDFFU, 0xB7B7B7FFU, 0x1B1B1BFFU, 0x040404FFU, 0x030303FFU, 
 0x010101FFU, 0x4B4B4BFFU, 0xD6D6D6FFU, 0xCACACAFFU, 0xBFBFBFFFU, 0xABABABFFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x131313FFU, 0x272727FFU, 0x2B2B2BFFU, 0xD8D8D8FFU, 0xD5D5D5FFU, 0xD5D5D5FFU, 0xD5D5D5FFU, 
 0xD5D5D5FFU, 0xD7D7D7FFU, 0xD9D9D9FFU, 0xD5D5D5FFU, 0x1B1B1BFFU, 0x070707FFU, 0x393939FFU, 0xE1E1E1FFU, 
 0xDBDBDBFFU, 0xD5D5D5FFU, 0xCBCBCBFFU, 0xBFBFBFFFU, 0x181818FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x151515FFU, 0x292929FFU, 0x1E1E1EFFU, 0xB7B7B7FFU, 0xDBDBDBFFU, 0xDADADAFFU, 0xDADADAFFU, 0xD8D8D8FFU, 
 0xDDDDDDFFU, 0xDEDEDEFFU, 0xDBDBDBFFU, 0x4E4E4EFFU, 0x434343FFU, 0xD9D9D9FFU, 0xDDDDDDFFU, 0xDEDEDEFFU, 
 0xD4D4D4FFU, 0x3A3A3AFFU, 0x080808FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x010101FFU, 0x181818FFU, 
 0x191919FFU, 0x050505FFU, 0x494949FFU, 0xD6D6D6FFU, 0xD5D5D5FFU, 0xD4D4D4FFU, 0xD4D4D4FFU, 0xDBDBDBFFU, 
 0xDCDCDCFFU, 0xDADADAFFU, 0xD5D5D5FFU, 0xD5D5D5FFU, 0xD7D7D7FFU, 0xDDDDDDFFU, 0xD0D0D0FFU, 0x3D3D3DFFU, 
 0x020202FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x111111FFU, 0x040404FFU, 
 0x000000FFU, 0x2C2C2CFFU, 0xD5D5D5FFU, 0xD0D0D0FFU, 0xD1D1D1FFU, 0xD2D2D2FFU, 0xD7D7D7FFU, 0xD7D7D7FFU, 
 0xD3D3D3FFU, 0xCECECEFFU, 0xCDCDCDFFU, 0xD3D3D3FFU, 0xC1C1C1FFU, 0x393939FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x020202FFU, 0x0D0D0DFFU, 0x220202FFU, 0x040404FFU, 
 0x040404FFU, 0xC9C9C9FFU, 0xCBCBCBFFU, 0xD0CECEFFU, 0xD3D1D1FFU, 0xD4D4D4FFU, 0xD2D2D2FFU, 0xCDCDCDFFU, 
 0xCFCACAFFU, 0xD0C9C9FFU, 0x7C7C7CFFU, 0x2D1B1BFFU, 0x230404FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0xB6B6B6FFU, 0xCEC7C7FFU, 0xD2CDCDFFU, 0xD5D3D3FFU, 0xD4D4D4FFU, 0xD3D3D3FFU, 0xD3D0D0FFU, 0xCDC8C8FFU, 
 0x818181FFU, 0x250B0BFFU, 0x310000FFU, 0x000000FFU, 0x310000FFU, 0x000000FFU, 0x3D0000FFU, 0x310000FFU, 
 0x220000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x4C4C4CFFU, 
 0xD0C2C2FFU, 0xCBC6C6FFU, 0xCECBCBFFU, 0xD0CECEFFU, 0xCFCFCFFFU, 0xCBCBCBFFU, 0x535353FFU, 0x3F0C0CFFU, 
 0x531717FFU, 0x310000FFU, 0x480808FFU, 0x3F0A0AFFU, 0x500808FFU, 0x340A0AFFU, 0x3F0A0AFFU, 0x310000FFU, 
 0x320000FFU, 0x000000FFU, 0x0A0A0AFFU, 0x0A0A0AFFU, 0x220000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x242424FFU, 0xD1BEBEFFU, 
 0xCAC3C3FFU, 0xCDC9C9FFU, 0xCECCCCFFU, 0xCCCCCCFFU, 0x4D3B3BFFU, 0x0B0B0BFFU, 0x3E0505FFU, 0x500505FFU, 
 0x580D0DFFU, 0x240606FFU, 0x490E0EFFU, 0x281111FFU, 0x340C0CFFU, 0x260D0DFFU, 0x330707FFU, 0x220000FFU, 
 0x000000FFU, 0x000000FFU, 0x220000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x090909FFU, 0xC7B5B5FFU, 0xC9BDBDFFU, 
 0xC7C2C2FFU, 0xBFBFBFFFU, 0x382B2BFFU, 0x330606FFU, 0x521212FFU, 0x220101FFU, 0x421414FFU, 0x500808FFU, 
 0x531818FFU, 0x371313FFU, 0x3F0A0AFFU, 0x310000FFU, 0x3F0808FFU, 0x080808FFU, 0x220000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x020202FFU, 0xB3AAAAFFU, 0xC0B6B6FFU, 0xA7A7A7FFU, 
 0x382B2BFFU, 0x171717FFU, 0x191919FFU, 0x191919FFU, 0x3D0101FFU, 0x040404FFU, 0x3E0404FFU, 0x3E0707FFU, 
 0x240808FFU, 0x060606FFU, 0x220202FFU, 0x060606FFU, 0x060606FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x979797FFU, 0x606060FFU, 0x282828FFU, 0x010101FFU, 
 0x030303FFU, 0x000000FFU, 0x220000FFU, 0x220000FFU, 0x2B1717FFU, 0x010101FFU, 0x010101FFU, 0x010101FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x171717FFU, 0x4E4E4EFFU, 0xC77A7AFFU, 0xD27979FFU, 0xD37777FFU, 
 0x230303FFU, 0xC86F6FFFU, 0xD26A6AFFU, 0xD67373FFU, 0x000000FFU, 0xD56D6DFFU, 0xD47474FFU, 0x000000FFU, 
 0x000000FFU, 0xD46969FFU, 0xCE6161FFU, 0xCC5A5AFFU, 0x000000FFU, 0xC94242FFU, 0xC03A3AFFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0xBC0202FFU, 0xB60000FFU, 0x000000FFU, 0x000000FFU, 0xB30000FFU, 0xB10000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0xCB7979FFU, 0x111111FFU, 0x000000FFU, 
 0x220000FFU, 0xC06262FFU, 0x220000FFU, 0x220000FFU, 0x3D0000FFU, 0xD47474FFU, 0x220000FFU, 0xD37272FFU, 
 0x3D0000FFU, 0xCA5C5CFFU, 0x310000FFU, 0xBC3E3EFFU, 0x3D0000FFU, 0x000000FFU, 0x310000FFU, 0xBC1010FFU, 
 0x3D0000FFU, 0xBC0000FFU, 0x220000FFU, 0x000000FFU, 0x220000FFU, 0xAE0000FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0xBC6F6FFFU, 0xC16E6EFFU, 0x220000FFU, 
 0x3D0000FFU, 0x3D0000FFU, 0xBB6161FFU, 0x3D0000FFU, 0x4F0000FFU, 0xC26B6BFFU, 0xBE6969FFU, 0x470000FFU, 
 0x470000FFU, 0xB34646FFU, 0xAD3939FFU, 0xAC2A2AFFU, 0x3D0000FFU, 0x220000FFU, 0xA20000FFU, 0x310000FFU, 
 0x4F0000FFU, 0xA80000FFU, 0xA50000FFU, 0xA20000FFU, 0x220000FFU, 0xA20000FFU, 0xA20000FFU, 0x9B0000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0xBE5D5DFFU, 0x000000FFU, 0x000000FFU, 
 0x220000FFU, 0x220000FFU, 0x000000FFU, 0xCC5B5BFFU, 0x310000FFU, 0xCC5B5BFFU, 0x220000FFU, 0x220000FFU, 
 0x310000FFU, 0xBF2929FFU, 0x220000FFU, 0xB90707FFU, 0x310000FFU, 0xBC0000FFU, 0x310000FFU, 0x220000FFU, 
 0x3D0000FFU, 0xBC0000FFU, 0x220000FFU, 0xB10000FFU, 0x220000FFU, 0xAE0000FFU, 0x000000FFU, 0xA80000FFU, 
 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0x000000FFU, 0xB04545FFU, 0xBC3E3EFFU, 0xBF3737FFU, 
 0x000000FFU, 0xBA4343FFU, 0xC74D4DFFU, 0xCA4C4CFFU, 0x000000FFU, 0xC94242FFU, 0x000000FFU, 0x000000FFU, 
 0x000000FFU, 0xC10505FFU, 0xBC0000FFU, 0xBC0000FFU, 0x000000FFU, 0xC30000FFU, 0xB90000FFU, 0xC30000FFU, 
 0x000000FFU, 0x000000FFU, 0xB90000FFU, 0xB60000FFU, 0x000000FFU, 0x000000FFU, 0xB10000FFU, 0xB10000FFU

};
static boole         guicore_window_icon_generic = FALSE;

/* Alpha mask, to cancel any alpha the destination might have */
static uint32        guicore_amask;

/* Initialization flags */
static auint         guicore_flags;

/* String constant for SDL error output */
#ifndef HEADLESS
static const char    guicore_sdlerr[] = "SDL Error: %s\n";
#endif

/* Pixel format */
static guicore_pixfmt_t guicore_pixfmt;

/* Render / filter state */
static auint               guicore_render_path = RENDER_PATH_STAGED;
static auint               guicore_jamma_flags = 0U;
#ifdef ENABLE_DISPLAY_FILTERS
static filter_ctx_t        guicore_filter_ctx;
static filter_pre_mode_t   guicore_filter_pre_mode;
static filter_scale_mode_t guicore_filter_scale_mode;
static filter_crt_params_t guicore_filter_crt;
static boole               guicore_filter_init_done = FALSE;

#define GUICORE_STAGE_SRCW_MAX 320U
#define GUICORE_STAGE_SRCH_MAX 246U
#define GUICORE_STAGE_DSTW_MAX (GUICORE_STAGE_SRCW_MAX * 2U)
#define GUICORE_STAGE_DSTH_MAX (GUICORE_STAGE_SRCH_MAX * 2U)
static uint32              guicore_stage_src32[GUICORE_STAGE_SRCW_MAX * GUICORE_STAGE_SRCH_MAX];
static uint8               guicore_stage_src8[GUICORE_STAGE_SRCW_MAX * GUICORE_STAGE_SRCH_MAX];
static uint32              guicore_stage_dst32[GUICORE_STAGE_DSTW_MAX * GUICORE_STAGE_DSTH_MAX];
#endif


static void guicore_applyicon(void)
{
 auint        sx;
 auint        sy;
 auint        dx;
 auint        dy;
 uint8        idx;
 uint8        r;
 uint8        g;
 uint8        b;
 uint32       rgba;
 Uint32       pixel;
 Uint32*      pix;

#ifdef HEADLESS
 return;
#endif

 if (guicore_icon_surface != NULL){
  SDL_FreeSurface(guicore_icon_surface);
  guicore_icon_surface = NULL;
 }

#ifdef USE_SDL1
 if (guicore_surface == NULL){ return; }
#else
 if (guicore_window == NULL){ return; }
#endif


 if ((!guicore_window_icon_valid) && (!guicore_window_icon_generic)){
  return;
 }

 guicore_icon_surface = SDL_CreateRGBSurface(0U, 32, 32, 32, 0U, 0U, 0U, 0U);
 if (guicore_icon_surface == NULL){
  return;
 }

 if (SDL_MUSTLOCK(guicore_icon_surface)){
  if (SDL_LockSurface(guicore_icon_surface) != 0){
   SDL_FreeSurface(guicore_icon_surface);
   guicore_icon_surface = NULL;
   return;
  }
 }

 pix = (Uint32*)(guicore_icon_surface->pixels);
 if (guicore_window_icon_generic){
  for (sy = 0U; sy < 32U; sy++){
   for (sx = 0U; sx < 32U; sx++){
    rgba = guicore_generic_icon_rgba[(sy * 32U) + sx];
    r = (uint8)((rgba >> 24) & 0xFFU);
    g = (uint8)((rgba >> 16) & 0xFFU);
    b = (uint8)((rgba >> 8)  & 0xFFU);
    idx = (uint8)(rgba & 0xFFU);
    pixel = SDL_MapRGBA(guicore_icon_surface->format, r, g, b, idx);
    pix[(sy * 32U) + sx] = pixel;
   }
  }
 }else{
  for (sy = 0U; sy < 16U; sy++){
   for (sx = 0U; sx < 16U; sx++){
    idx = guicore_window_icon[(sy * 16U) + sx];
    r = (uint8)((((idx >> 0) & 7U) * 255U) / 7U);
    g = (uint8)((((idx >> 3) & 7U) * 255U) / 7U);
    b = (uint8)((((idx >> 6) & 3U) * 255U) / 3U);
    pixel = SDL_MapRGBA(guicore_icon_surface->format, r, g, b, 255U);
    dx = sx * 2U;
    dy = sy * 2U;
    pix[(dy * 32U) + dx] = pixel;
    pix[(dy * 32U) + dx + 1U] = pixel;
    pix[((dy + 1U) * 32U) + dx] = pixel;
    pix[((dy + 1U) * 32U) + dx + 1U] = pixel;
   }
  }
 }

 if (SDL_MUSTLOCK(guicore_icon_surface)){
  SDL_UnlockSurface(guicore_icon_surface);
 }

#ifdef USE_SDL1
 SDL_WM_SetIcon(guicore_icon_surface, NULL);
#else
 SDL_SetWindowIcon(guicore_window, guicore_icon_surface);
#endif
}

static void guicore_render_2x(uint32* dest, auint dpitch,
                              auint xs, auint ys,
                              auint w,  auint h)
{
 auint  i;
 auint  j;
 auint  destoff0;
 auint  destoff1;
 auint  srcoff;
 uint32 col;

 for (i = 0U; i < (h << 1); i += 2U){
  destoff0 = (dpitch * (i     ));
  destoff1 = (dpitch * (i + 1U));
  srcoff   = (640U * (ys + (i >> 1))) + (xs << 1);
  for (j = 0U; j < (w << 1); j ++){
   col = guicore_pixels[srcoff + j] | guicore_amask;
   dest[destoff0 + j] = col;
   dest[destoff1 + j] = col;
  }
 }
}


static void guicore_render_1x(uint32* dest, auint dpitch,
                              auint xs, auint ys,
                              auint w,  auint h)
{
 auint  i;
 auint  j;
 auint  destoff;
 auint  srcoff;

 for (i = 0U; i < h; i ++){
  destoff = (dpitch * i);
  srcoff  = (640U * (ys + i)) + (xs << 1);
  for (j = 0U; j < w; j ++){
   dest[destoff + j] = guicore_pixels[srcoff + (j << 1)] | guicore_amask;
  }
 }
}


static boole guicore_use_direct_present(void)
{
 if (guicore_jamma_flags != 0U){
  return FALSE;
 }
 if (!RENDER_PATH_IS_STAGED(guicore_render_path)){
  return TRUE;
 }
#ifdef ENABLE_DISPLAY_FILTERS
 if ((guicore_filter_pre_mode == FILTER_PRE_NONE) &&
     (guicore_filter_scale_mode == FILTER_SCALE_NONE) &&
     (guicore_filter_crt.mode == FILTER_CRT_NONE)){
  return TRUE;
 }
#endif
 return FALSE;
}

static void guicore_calc_dims(auint flags, auint* wndw, auint* wndh, auint* texw, auint* texh)
{
 switch (flags & (GUICORE_SMALL | GUICORE_GAMEONLY)){
  case 0U:
   if (wndw != NULL){ *wndw = WND_W; }
   if (wndh != NULL){ *wndh = WND_H; }
   if (texw != NULL){ *texw = TEX_W; }
   if (texh != NULL){ *texh = TEX_H; }
   break;
  case GUICORE_SMALL:
   if (wndw != NULL){ *wndw = WNDS_W; }
   if (wndh != NULL){ *wndh = WNDS_H; }
   if (texw != NULL){ *texw = TEXS_W; }
   if (texh != NULL){ *texh = TEXS_H; }
   break;
  case GUICORE_GAMEONLY:
   if (wndw != NULL){ *wndw = WNDG_W; }
   if (wndh != NULL){ *wndh = WNDG_H; }
   if (texw != NULL){ *texw = TEXG_W; }
   if (texh != NULL){ *texh = TEXG_H; }
   break;
  default:
   if (wndw != NULL){ *wndw = WNDSG_W; }
   if (wndh != NULL){ *wndh = WNDSG_H; }
   if (texw != NULL){ *texw = TEXSG_W; }
   if (texh != NULL){ *texh = TEXSG_H; }
   break;
 }
}


static void guicore_getrendergeom(auint* texw, auint* texh, auint* offx, auint* offy)
{
	if ((guicore_flags & GUICORE_GAMEONLY) != 0U){
		*texw = TEXSG_W;
		*texh = TEXSG_H;
		*offx = TGOG_X;
		*offy = TGOG_Y;
	}else{
		*texw = TEXS_W;
		*texh = TEXS_H;
		*offx = 0U;
		*offy = 0U;
	}

	if ((guicore_flags & GUICORE_SMALL) == 0U){
		*texw <<= 1;
		*texh <<= 1;
		*offx <<= 1;
		*offy <<= 1;
	}
}




/*
** Renders double scan output. The source is the target pixel buffer
** (guicore_pixels). Locations are as for single scan 320 x 270 output.
*/

#ifdef ENABLE_DISPLAY_FILTERS
static void guicore_filter_init_defaults(void)
{
 if (guicore_filter_init_done){ return; }

 filter_init(&guicore_filter_ctx);

 guicore_filter_pre_mode = FILTER_PRE_NONE;
 guicore_filter_scale_mode = FILTER_SCALE_NONE;
 guicore_filter_crt.mode = FILTER_CRT_NONE;
 guicore_render_path = RENDER_PATH_STAGED;
 guicore_filter_crt.scanline_dark = 192;
 guicore_filter_crt.mask_strength = 56;
 guicore_filter_crt.curvature_strength = 24;
 guicore_filter_crt.composite_strength = 40;
 guicore_filter_crt.vignette_strength = 40;

 guicore_filter_init_done = TRUE;
}
#else
static void guicore_filter_init_defaults(void)
{
}
#endif

typedef struct{
 auint srcx;
 auint srcy;
 auint srcw;
 auint srch;
 auint availx;
 auint availy;
 auint availw;
 auint availh;
 boole copy_top;
 auint top_h;
 boole copy_bottom;
 auint bottom_y;
 auint bottom_h;
}guicore_view_t;

static void guicore_get_content_rect(guicore_view_t const* view,
                                     auint* dstx, auint* dsty,
                                     auint* dstw, auint* dsth,
                                     auint* rw,   auint* rh)
{
 auint scale = ((guicore_flags & GUICORE_SMALL) != 0U) ? 1U : 2U;
 auint aw = view->availw * scale;
 auint ah = view->availh * scale;
 auint rot;
 auint flags = guicore_jamma_flags;

 if ((flags & JAMMA_ROTATE_90) != 0U){ rot = 1U; }
 else if ((flags & JAMMA_ROTATE_180) != 0U){ rot = 2U; }
 else if ((flags & JAMMA_ROTATE_270) != 0U){ rot = 3U; }
 else{ rot = 0U; }

 if ((rot & 1U) != 0U){
  *rw = view->srch;
  *rh = view->srcw;
 }else{
  *rw = view->srcw;
  *rh = view->srch;
 }

 if (((*rw) == 0U) || ((*rh) == 0U) || (aw == 0U) || (ah == 0U)){
  *dstx = 0U;
  *dsty = 0U;
  *dstw = 0U;
  *dsth = 0U;
  return;
 }

 if (((*rw) * ah) > ((*rh) * aw)){
  *dstw = aw;
  *dsth = ((*rh) * aw) / (*rw);
 }else{
  *dsth = ah;
  *dstw = ((*rw) * ah) / (*rh);
 }
 if ((*dstw) == 0U){ *dstw = 1U; }
 if ((*dsth) == 0U){ *dsth = 1U; }

 *dstx = (view->availx * scale) + ((aw - (*dstw)) >> 1);
 *dsty = (view->availy * scale) + ((ah - (*dsth)) >> 1);
}


static uint32 guicore_srcpix(auint sx, auint sy)
{
 return guicore_pixels[(sy * 640U) + (sx << 1)] | guicore_amask;
}


static void guicore_fill(uint32* dest, auint dpitch, auint w, auint h, uint32 col)
{
 auint y;
 auint x;
 for (y = 0U; y < h; ++y){
  auint off = y * dpitch;
  for (x = 0U; x < w; ++x){
   dest[off + x] = col;
  }
 }
}


static void guicore_getview(guicore_view_t* view)
{
 if ((guicore_flags & GUICORE_GAMEONLY) != 0U){
  view->srcx = 5U;
  view->srcy = 19U;
  view->srcw = 310U;
  view->srch = 228U;
  view->availx = 0U;
  view->availy = 0U;
  view->availw = 310U;
  view->availh = 228U;
  view->copy_top = FALSE;
  view->top_h = 0U;
  view->copy_bottom = FALSE;
  view->bottom_y = 0U;
  view->bottom_h = 0U;
 }else{
  view->srcx = 0U;
  view->srcy = 19U;
  view->srcw = 320U;
  view->srch = 246U;
  view->availx = 0U;
  view->availy = 19U;
  view->availw = 320U;
  view->availh = 246U;
  view->copy_top = TRUE;
  view->top_h = 19U;
  view->copy_bottom = TRUE;
  view->bottom_y = 265U;
  view->bottom_h = 15U;
 }
}


static void guicore_blit_region(uint32* dest, auint dpitch,
                                auint dstx, auint dsty,
                                auint dstw, auint dsth,
                                auint srcx, auint srcy,
                                auint srcw, auint srch)
{
 auint dy;
 auint dx;
 auint sy;
 auint sx;
 if ((dstw == 0U) || (dsth == 0U) || (srcw == 0U) || (srch == 0U)){ return; }
 for (dy = 0U; dy < dsth; ++dy){
  sy = srcy + (((dy * srch) + (dsth >> 1)) / dsth);
  if (sy >= (srcy + srch)){ sy = (srcy + srch) - 1U; }
  for (dx = 0U; dx < dstw; ++dx){
   sx = srcx + (((dx * srcw) + (dstw >> 1)) / dstw);
   if (sx >= (srcx + srcw)){ sx = (srcx + srcw) - 1U; }
   dest[((dsty + dy) * dpitch) + dstx + dx] = guicore_srcpix(sx, sy);
  }
 }
}


static uint32 guicore_native_to_argb(uint32 c)
{
 auint r = (c >> guicore_pixfmt.rsh) & 0xFFU;
 auint g = (c >> guicore_pixfmt.gsh) & 0xFFU;
 auint b = (c >> guicore_pixfmt.bsh) & 0xFFU;
 return 0xFF000000U | (r << 16) | (g << 8) | b;
}

static uint32 guicore_argb_to_native(uint32 c)
{
 return (((c >> 16) & 0xFFU) << guicore_pixfmt.rsh) |
        (((c >>  8) & 0xFFU) << guicore_pixfmt.gsh) |
        (((c      ) & 0xFFU) << guicore_pixfmt.bsh) |
        guicore_amask;
}

static uint8 guicore_quantize_332(uint32 c)
{
 auint r = (c >> 16) & 0xFFU;
 auint g = (c >>  8) & 0xFFU;
 auint b = c & 0xFFU;
 auint ri = ((r * 7U) + 127U) / 255U;
 auint gi = ((g * 7U) + 127U) / 255U;
 auint bi = ((b * 3U) + 127U) / 255U;
 return (uint8)(((ri & 7U) << 5) | ((gi & 7U) << 2) | (bi & 3U));
}

static void guicore_capture_view32(uint32* dst, auint pitch, guicore_view_t const* view)
{
 auint y;
 auint x;
 for (y = 0U; y < view->srch; ++y){
  auint doff = y * pitch;
  for (x = 0U; x < view->srcw; ++x){
   dst[doff + x] = guicore_native_to_argb(guicore_srcpix(view->srcx + x, view->srcy + y));
  }
 }
}

static void guicore_quantize_buf(uint8* dst, uint32 const* src, auint w, auint h, auint pitch)
{
 auint y;
 auint x;
 for (y = 0U; y < h; ++y){
  for (x = 0U; x < w; ++x){
   dst[(y * w) + x] = guicore_quantize_332(src[(y * pitch) + x]);
  }
 }
}

static void guicore_blit_content_buf(uint32* dest, auint dpitch,
                                     uint32 const* src, auint srcpitch,
                                     auint srcw, auint srch,
                                     auint scale,
                                     guicore_view_t const* view)
{
 auint rot;
 boole flip_h;
 boole flip_v;
 auint rw;
 auint rh;
 auint aw;
 auint ah;
 auint dstw;
 auint dsth;
 auint dstx;
 auint dsty;
 auint dx;
 auint dy;
 auint ox;
 auint srcax;
 auint srcay;
 auint flags;

 flags = guicore_jamma_flags;
 if ((flags & JAMMA_ROTATE_90) != 0U){ rot = 1U; }
 else if ((flags & JAMMA_ROTATE_180) != 0U){ rot = 2U; }
 else if ((flags & JAMMA_ROTATE_270) != 0U){ rot = 3U; }
 else{ rot = 0U; }
 flip_h = ((flags & JAMMA_FLIP_H) != 0U) ? TRUE : FALSE;
 flip_v = ((flags & JAMMA_FLIP_V) != 0U) ? TRUE : FALSE;

 aw = view->availw * scale;
 ah = view->availh * scale;
 if ((rot & 1U) != 0U){
  rw = srch;
  rh = srcw;
 }else{
  rw = srcw;
  rh = srch;
 }
 if ((rw == 0U) || (rh == 0U) || (aw == 0U) || (ah == 0U)){ return; }
 if ((rw * ah) > (rh * aw)){
  dstw = aw;
  dsth = (rh * aw) / rw;
 }else{
  dsth = ah;
  dstw = (rw * ah) / rh;
 }
 if (dstw == 0U){ dstw = 1U; }
 if (dsth == 0U){ dsth = 1U; }
 dstx = (view->availx * scale) + ((aw - dstw) >> 1);
 dsty = (view->availy * scale) + ((ah - dsth) >> 1);

 for (dy = 0U; dy < dsth; ++dy){
  auint oy_base = (dy * rh) / dsth;
  if (oy_base >= rh){ oy_base = rh - 1U; }
  for (dx = 0U; dx < dstw; ++dx){
   auint oy_cur;
   ox = (dx * rw) / dstw;
   if (ox >= rw){ ox = rw - 1U; }
   oy_cur = oy_base;
   if (flip_h){ ox = rw - 1U - ox; }
   if (flip_v){ oy_cur = rh - 1U - oy_cur; }
   switch (rot){
    default:
    case 0U: srcax = ox; srcay = oy_cur; break;
    case 1U: srcax = oy_cur; srcay = srch - 1U - ox; break;
    case 2U: srcax = srcw - 1U - ox; srcay = srch - 1U - oy_cur; break;
    case 3U: srcax = srcw - 1U - oy_cur; srcay = ox; break;
   }
   dest[((dsty + dy) * dpitch) + dstx + dx] = guicore_argb_to_native(src[(srcay * srcpitch) + srcax]);
  }
 }
}

#ifdef ENABLE_DISPLAY_FILTERS
static void guicore_render_staged(uint32* dest, auint dpitch, auint texw, auint texh)
{
 guicore_view_t view;
 auint scale = ((guicore_flags & GUICORE_SMALL) != 0U) ? 1U : 2U;
 uint32 bg = guicore_palette[0] | guicore_amask;
 uint32* out32 = guicore_stage_src32;
 auint out_pitch = 0U;
 auint outw = 0U;
 auint outh = 0U;

 (void)texw;
 (void)texh;
 guicore_getview(&view);
 guicore_fill(dest, dpitch, texw, texh, bg);

 if (view.copy_top){
  guicore_blit_region(dest, dpitch, 0U, 0U, texw, view.top_h * scale, 0U, 0U, 320U, view.top_h);
 }
 if (view.copy_bottom){
  guicore_blit_region(dest, dpitch, 0U, view.bottom_y * scale, texw, view.bottom_h * scale, 0U, view.bottom_y, 320U, view.bottom_h);
 }

 if ((view.srcw > GUICORE_STAGE_SRCW_MAX) || (view.srch > GUICORE_STAGE_SRCH_MAX)){
  return;
 }

 guicore_capture_view32(guicore_stage_src32, view.srcw, &view);
 if (guicore_filter_pre_mode != FILTER_PRE_NONE){
  filter_apply_pre(guicore_stage_src32, (int)view.srcw, (int)view.srch, (int)view.srcw, guicore_filter_pre_mode, &guicore_filter_crt);
 }

 if (((scale > 1U) && (guicore_filter_crt.mode != FILTER_CRT_NONE)) ||
     (guicore_filter_scale_mode != FILTER_SCALE_NONE)){
  filter_scale_mode_t smode = guicore_filter_scale_mode;
  guicore_quantize_buf(guicore_stage_src8, guicore_stage_src32, view.srcw, view.srch, view.srcw);
  if (smode > FILTER_SCALE_XBR2X){ smode = FILTER_SCALE_NONE; }
  filter_scale_2x(guicore_stage_src32, guicore_stage_src8, (int)view.srcw, (int)view.srch, (int)view.srcw, guicore_stage_dst32, (int)(view.srcw * 2U), &guicore_filter_ctx, smode);
  out32 = guicore_stage_dst32;
  out_pitch = view.srcw * 2U;
  outw = view.srcw * 2U;
  outh = view.srch * 2U;
 }else{
  out32 = guicore_stage_src32;
  out_pitch = view.srcw;
  outw = view.srcw;
  outh = view.srch;
 }

 if (guicore_filter_crt.mode != FILTER_CRT_NONE){
  filter_apply_crt(out32, (int)outw, (int)outh, (int)out_pitch, &guicore_filter_crt);
 }

 guicore_blit_content_buf(dest, dpitch, out32, out_pitch, outw, outh, scale, &view);
}

#endif

static void guicore_blit_content(uint32* dest, auint dpitch,
                                 auint scale,
                                 guicore_view_t const* view)
{
 auint rot;
 boole flip_h;
 boole flip_v;
 auint sw;
 auint sh;
 auint rw;
 auint rh;
 auint aw;
 auint ah;
 auint dstw;
 auint dsth;
 auint dstx;
 auint dsty;
 auint dx;
 auint dy;
 auint ox;
 auint sx;
 auint sy;
 auint srcax;
 auint srcay;
 auint flags;

 flags = guicore_jamma_flags;
 if ((flags & JAMMA_ROTATE_90) != 0U){ rot = 1U; }
 else if ((flags & JAMMA_ROTATE_180) != 0U){ rot = 2U; }
 else if ((flags & JAMMA_ROTATE_270) != 0U){ rot = 3U; }
 else{ rot = 0U; }
 flip_h = ((flags & JAMMA_FLIP_H) != 0U) ? TRUE : FALSE;
 flip_v = ((flags & JAMMA_FLIP_V) != 0U) ? TRUE : FALSE;

 sw = view->srcw * scale;
 sh = view->srch * scale;
 aw = view->availw * scale;
 ah = view->availh * scale;

 if ((rot & 1U) != 0U){
  rw = sh;
  rh = sw;
 }else{
  rw = sw;
  rh = sh;
 }

 if ((rw == 0U) || (rh == 0U) || (aw == 0U) || (ah == 0U)){ return; }
 if ((rw * ah) > (rh * aw)){
  dstw = aw;
  dsth = (rh * aw) / rw;
 }else{
  dsth = ah;
  dstw = (rw * ah) / rh;
 }
 if (dstw == 0U){ dstw = 1U; }
 if (dsth == 0U){ dsth = 1U; }

 dstx = (view->availx * scale) + ((aw - dstw) >> 1);
 dsty = (view->availy * scale) + ((ah - dsth) >> 1);

 for (dy = 0U; dy < dsth; ++dy){
  auint oy_base = (dy * rh) / dsth;
  if (oy_base >= rh){ oy_base = rh - 1U; }
  for (dx = 0U; dx < dstw; ++dx){
   auint oy_cur;
   ox = (dx * rw) / dstw;
   if (ox >= rw){ ox = rw - 1U; }

   oy_cur = oy_base;
   if (flip_h){ ox = rw - 1U - ox; }
   if (flip_v){ oy_cur = rh - 1U - oy_cur; }

   switch (rot){
    default:
    case 0U:
     srcax = ox;
     srcay = oy_cur;
     break;
    case 1U:
     srcax = oy_cur;
     srcay = sh - 1U - ox;
     break;
    case 2U:
     srcax = sw - 1U - ox;
     srcay = sh - 1U - oy_cur;
     break;
    case 3U:
     srcax = sw - 1U - oy_cur;
     srcay = ox;
     break;
   }

   sx = view->srcx + (srcax / scale);
   sy = view->srcy + (srcay / scale);
   dest[((dsty + dy) * dpitch) + dstx + dx] = guicore_srcpix(sx, sy);
  }
 }
}


static void guicore_render(uint32* dest, auint dpitch, auint texw, auint texh)
{
 guicore_view_t view;
 auint scale = ((guicore_flags & GUICORE_SMALL) != 0U) ? 1U : 2U;
 uint32 bg = guicore_palette[0] | guicore_amask;

#ifdef ENABLE_DISPLAY_FILTERS
 if (RENDER_PATH_IS_STAGED(guicore_render_path)){
  guicore_render_staged(dest, dpitch, texw, texh);
  return;
 }
#endif

 guicore_getview(&view);
 guicore_fill(dest, dpitch, texw, texh, bg);

 if (view.copy_top){
  guicore_blit_region(dest, dpitch,
      0U, 0U,
      texw, view.top_h * scale,
      0U, 0U,
      320U, view.top_h);
 }
 if (view.copy_bottom){
  guicore_blit_region(dest, dpitch,
      0U, view.bottom_y * scale,
      texw, view.bottom_h * scale,
      0U, view.bottom_y,
      320U, view.bottom_h);
 }

 guicore_blit_content(dest, dpitch, scale, &view);
}


#ifdef ENABLE_DISPLAY_FILTERS
static void guicore_apply_prefx(uint32* pixels, auint pitch, auint texw, auint texh)
{
 (void)pixels;
 (void)pitch;
 (void)texw;
 (void)texh;
}

static void guicore_apply_postfx(uint32* pixels, auint pitch, auint texw, auint texh)
{
 (void)pixels;
 (void)pitch;
 (void)texw;
 (void)texh;
}
#else
static void guicore_apply_prefx(uint32* pixels, auint pitch, auint texw, auint texh)
{
 (void)pixels;
 (void)pitch;
 (void)texw;
 (void)texh;
}

static void guicore_apply_postfx(uint32* pixels, auint pitch, auint texw, auint texh)
{
 (void)pixels;
 (void)pitch;
 (void)texw;
 (void)texh;
}
#endif


/*
** Attempts to initialize the GUI. Returns TRUE on success. This must be
** called first before any platform specific elements as it initializes
** those. It can be recalled to change the display's properties (flags).
** Note that if it fails, it tears fown the GUI and any platform specific
** extensions!
*/
boole guicore_init(auint flags, const char* title)
{
#ifdef HEADLESS
 goto skipwnd;
#endif
 auint i;
 auint r;
 auint g;
 auint b;
 auint wndw;
 auint wndh;
 auint texw;
 auint texh;
#ifndef USE_SDL1
 auint res;
 auint renflags;
 auint wndflags;
 boole wndnew = TRUE;
 SDL_RendererInfo reninfo;
#endif


 guicore_filter_init_defaults();

 /* Prepare parameters */

#ifdef USE_SDL1

#else

 wndflags = 0U;
 if ((flags & GUICORE_FULLSCREEN) != 0U){ wndflags |= SDL_WINDOW_FULLSCREEN_DESKTOP; }
 else{ wndflags |= SDL_WINDOW_RESIZABLE; }

 renflags = SDL_RENDERER_ACCELERATED;
 if ((flags & GUICORE_NOVSYNC) == 0U){ renflags |= SDL_RENDERER_PRESENTVSYNC; }

#endif

 guicore_calc_dims(flags, &wndw, &wndh, &texw, &texh);

 /* Check whether initializing or doing a partial reinit. */

 if ((guicore_flags & GUICORE_INIT) == 0U){ /* If it wasn't initialized yet, init */

#ifdef __EMSCRIPTEN__

  EM_ASM(
   SDL.defaults.copyOnLock = false;
   SDL.defaults.discardOnLock = true;
   SDL.defaults.opaqueFrontBuffer = false;
  );

#endif

#ifdef USE_SDL1

  if (SDL_Init(SDL_INIT_VIDEO) != 0){
   print_error(guicore_sdlerr, SDL_GetError());
   goto fail_n;
  }

#else

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0){
   print_error(guicore_sdlerr, SDL_GetError());
   goto fail_n;
  }

#endif

 }else{ /* Already initialized */

#ifdef USE_SDL1

#else

 SDL_DestroyTexture(guicore_texture);
 if (((guicore_flags ^ flags) & ~(GUICORE_INIT |
                                  GUICORE_SMALL |
                                  GUICORE_GAMEONLY)) != 0U){
  SDL_DestroyRenderer(guicore_renderer);
  SDL_DestroyWindow(guicore_window);
 }else{
  wndnew = FALSE; /* Don't need to create and init new window */
 }

#endif

 }

 /* Perform initializations */

#ifdef USE_SDL1

 guicore_surface = SDL_SetVideoMode(texw, texh, 32U, SDL_HWSURFACE); /* SDL1 doesn't support scaling */
 if (guicore_surface == NULL){
  print_error(guicore_sdlerr, SDL_GetError());
  goto fail_qt;
 }
 guicore_applyicon();

#ifdef __EMSCRIPTEN__

  /* Scale output so the game wouldn't have to be played on a tiny stamp */

  EM_ASM_({
    var canvas = Module['canvas'];
    canvas.style.setProperty("width", $0 + "px", "important");
    canvas.style.setProperty("height", $1 + "px", "important");
  }, wndw, wndh);

#endif

 /* For some reason in Emscripten the shifts from the format are missing. Work
 ** it around by determining them using the masks */

 if      ((guicore_surface->format->Rmask & 0x00FFFFFFU) == 0U){ guicore_pixfmt.rsh = 24U; }
 else if ((guicore_surface->format->Rmask & 0x0000FFFFU) == 0U){ guicore_pixfmt.rsh = 16U; }
 else if ((guicore_surface->format->Rmask & 0x000000FFU) == 0U){ guicore_pixfmt.rsh =  8U; }
 else                                                          { guicore_pixfmt.rsh =  0U; }
 if      ((guicore_surface->format->Gmask & 0x00FFFFFFU) == 0U){ guicore_pixfmt.gsh = 24U; }
 else if ((guicore_surface->format->Gmask & 0x0000FFFFU) == 0U){ guicore_pixfmt.gsh = 16U; }
 else if ((guicore_surface->format->Gmask & 0x000000FFU) == 0U){ guicore_pixfmt.gsh =  8U; }
 else                                                          { guicore_pixfmt.gsh =  0U; }
 if      ((guicore_surface->format->Bmask & 0x00FFFFFFU) == 0U){ guicore_pixfmt.bsh = 24U; }
 else if ((guicore_surface->format->Bmask & 0x0000FFFFU) == 0U){ guicore_pixfmt.bsh = 16U; }
 else if ((guicore_surface->format->Bmask & 0x000000FFU) == 0U){ guicore_pixfmt.bsh =  8U; }
 else                                                          { guicore_pixfmt.bsh =  0U; }
 guicore_amask = guicore_surface->format->Amask;

#else

 /* Create window and its renderer */
 if (wndnew){

  guicore_window = SDL_CreateWindow(
      title,
      SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED,
      wndw,
      wndh,
      wndflags);
  if (guicore_window == NULL){
   print_error(guicore_sdlerr, SDL_GetError());
   goto fail_qt;
  }
  SDL_SetWindowMinimumSize(guicore_window, wndw, wndh);
  guicore_applyicon();

  guicore_renderer = SDL_CreateRenderer(
      guicore_window,
      -1,
      renflags);
  if (guicore_renderer == NULL){
   print_error(guicore_sdlerr, SDL_GetError());
   goto fail_wnd;
  }

 }

 SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
 if (SDL_RenderSetLogicalSize(guicore_renderer, texw, texh) != 0U){
  print_error(guicore_sdlerr, SDL_GetError());
  goto fail_ren;
 }

 /* Find out renderer's pixel format */

 SDL_GetRendererInfo(guicore_renderer, &reninfo);

 res = SDL_PIXELFORMAT_UNKNOWN;
 for (i = 0U; i < reninfo.num_texture_formats; i++){
  if ( (reninfo.texture_formats[i] == SDL_PIXELFORMAT_RGBX8888) ||
       (reninfo.texture_formats[i] == SDL_PIXELFORMAT_BGRX8888) ){
   res = reninfo.texture_formats[i];
   break;
  }
 }
 if (res == SDL_PIXELFORMAT_UNKNOWN){
  for (i = 0U; i < reninfo.num_texture_formats; i++){
   if ( (reninfo.texture_formats[i] == SDL_PIXELFORMAT_RGBA8888) ||
        (reninfo.texture_formats[i] == SDL_PIXELFORMAT_BGRA8888) ||
        (reninfo.texture_formats[i] == SDL_PIXELFORMAT_ARGB8888) ||
        (reninfo.texture_formats[i] == SDL_PIXELFORMAT_ABGR8888) ){
    res = reninfo.texture_formats[i];
    break;
   }
  }
 }
 if (res == SDL_PIXELFORMAT_UNKNOWN){
  print_error("Warning: Display doesn't support a known 32 bpp format.");
  res = SDL_PIXELFORMAT_RGBX8888;
 }

 /* Note: There --might-- be byte order problems here across Big Endian and
 ** Little Endian machines. Use the SDL_BYTEORDER macro then to fix this part
 ** proper to generate the appropriate pixel format! */

 switch (res){
  case SDL_PIXELFORMAT_RGBX8888:
  case SDL_PIXELFORMAT_RGBA8888:
   guicore_pixfmt.rsh = 24U;
   guicore_pixfmt.gsh = 16U;
   guicore_pixfmt.bsh =  8U;
   guicore_amask = 0x000000FFU;
   break;
  case SDL_PIXELFORMAT_BGRX8888:
  case SDL_PIXELFORMAT_BGRA8888:
   guicore_pixfmt.rsh =  8U;
   guicore_pixfmt.gsh = 16U;
   guicore_pixfmt.bsh = 24U;
   guicore_amask = 0x000000FFU;
   break;
  case SDL_PIXELFORMAT_ARGB8888:
   guicore_pixfmt.rsh = 16U;
   guicore_pixfmt.gsh =  8U;
   guicore_pixfmt.bsh =  0U;
   guicore_amask = 0xFF000000U;
   break;
  default:
   guicore_pixfmt.rsh =  0U;
   guicore_pixfmt.gsh =  8U;
   guicore_pixfmt.bsh = 16U;
   guicore_amask = 0xFF000000U;
   break;
 }

 /* Generate hopefully optimal texture to fit the renderer */

 guicore_texture = SDL_CreateTexture(
     guicore_renderer,
     res,
     SDL_TEXTUREACCESS_STREAMING,
     texw,
     texh);
 if (guicore_texture == NULL){
  print_error(guicore_sdlerr, SDL_GetError());
  goto fail_ren;
 }

 if (wndnew){
  SDL_RenderClear(guicore_renderer);
  SDL_RenderPresent(guicore_renderer);
 }

#endif

 /* Generate palette */

 for (i = 0U; i < 256U; i++){
  r = (((i >> 0) & 7U) * 255U) / 7U;
  g = (((i >> 3) & 7U) * 255U) / 7U;
  b = (((i >> 6) & 3U) * 255U) / 3U;
  guicore_palette[i] = (r << guicore_pixfmt.rsh) |
                       (g << guicore_pixfmt.gsh) |
                       (b << guicore_pixfmt.bsh);
 }
#ifdef HEADLESS
skipwnd:
#endif
 guicore_flags = flags | GUICORE_INIT;
 return TRUE;

#ifdef USE_SDL1

#else

fail_ren:
 SDL_DestroyRenderer(guicore_renderer);
fail_wnd:
 SDL_DestroyWindow(guicore_window);

#endif

fail_qt:
 SDL_Quit();
fail_n:
 guicore_flags = 0U;
 return FALSE;
}



/*
** Tears down the GUI.
*/
void  guicore_quit(void)
{
 if ((guicore_flags & GUICORE_INIT) != 0U){

#ifdef USE_SDL1

#else

  SDL_DestroyTexture(guicore_texture);
  SDL_DestroyRenderer(guicore_renderer);
  SDL_DestroyWindow(guicore_window);

#endif

  SDL_Quit();

 }

 guicore_flags = 0U;
}



/*
** Gets current GUI flags.
*/
auint guicore_getflags(void)
{
 return guicore_flags;
}


boole guicore_setfullscreen(boole enable)
{
#ifdef HEADLESS
 unused(enable);
 return TRUE;
#else
#ifdef USE_SDL1
 auint flags;
 if (enable){ flags = guicore_flags | GUICORE_FULLSCREEN; }
 else{ flags = guicore_flags & ~GUICORE_FULLSCREEN; }
 return guicore_init(flags & ~GUICORE_INIT, "CUzeBox");
#else
 auint wndw;
 auint wndh;
 Uint32 fsflags;
 if ((guicore_flags & GUICORE_INIT) == 0U){ return FALSE; }
 if (guicore_window == NULL){ return FALSE; }
 fsflags = enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0U;
 if (SDL_SetWindowFullscreen(guicore_window, fsflags) != 0){
  print_error(guicore_sdlerr, SDL_GetError());
  return FALSE;
 }
 guicore_calc_dims(guicore_flags, &wndw, &wndh, NULL, NULL);
 SDL_SetWindowResizable(guicore_window, enable ? SDL_FALSE : SDL_TRUE);
 SDL_SetWindowMinimumSize(guicore_window, wndw, wndh);
 if (!enable){
  SDL_SetWindowSize(guicore_window, wndw, wndh);
  SDL_SetWindowPosition(guicore_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
 }
 if (enable){ guicore_flags |= GUICORE_FULLSCREEN; }
 else{ guicore_flags &= ~GUICORE_FULLSCREEN; }
 return TRUE;
#endif
#endif
}


/*
** Retrieves 640 x 280 GUI surface's pixel buffer.
*/
uint32* guicore_getpixbuf(void)
{
 return &(guicore_pixels[0]);
}


/*
** Retrieves dimensions of the logical GUI pixel buffer.
*/
void guicore_getpixbufsize(auint* pixw, auint* pixh)
{
 if (pixw != NULL){ *pixw = 640U; }
 if (pixh != NULL){ *pixh = 280U; }
}


/*
** Retrieves the pixel format of the pixel buffer.
*/
void  guicore_getpixfmt(guicore_pixfmt_t* pixfmt)
{
 pixfmt->rsh = guicore_pixfmt.rsh;
 pixfmt->gsh = guicore_pixfmt.gsh;
 pixfmt->bsh = guicore_pixfmt.bsh;
}



/*
** Retrieves pitch of GUI surface
*/
auint guicore_getpitch(void)
{
 return 640U;
}



/*
** Retrieves 256 color Uzebox palette which should be used to generate
** pixels. Algorithms can assume some 8 bit per channel representation, but
** channel order might vary.
*/
uint32 const* guicore_getpalette(void)
{
 return &guicore_palette[0];
}

uint32 const* guicore_get_default_icon_rgba(void)
{
 return &guicore_generic_icon_rgba[0];
}


auint guicore_get_filter_pre_mode(void)
{
#ifdef ENABLE_DISPLAY_FILTERS
	return (auint)guicore_filter_pre_mode;
#else
	return 0U;
#endif
}


void guicore_set_filter_pre_mode(auint mode)
{
#ifdef ENABLE_DISPLAY_FILTERS
	if (mode > (auint)FILTER_PRE_BLACK_WHITE){
		mode = (auint)FILTER_PRE_NONE;
	}
	guicore_filter_pre_mode = (filter_pre_mode_t)mode;
#else
	(void)mode;
#endif
}


auint guicore_get_filter_scale_mode(void)
{
#ifdef ENABLE_DISPLAY_FILTERS
	return (auint)guicore_filter_scale_mode;
#else
	return 0U;
#endif
}


void guicore_set_filter_scale_mode(auint mode)
{
#ifdef ENABLE_DISPLAY_FILTERS
	if (mode > (auint)FILTER_SCALE_XBR2X){
		mode = (auint)FILTER_SCALE_NONE;
	}
	guicore_filter_scale_mode = (filter_scale_mode_t)mode;
#else
	(void)mode;
#endif
}


auint guicore_get_filter_crt_mode(void)
{
#ifdef ENABLE_DISPLAY_FILTERS
	return (auint)guicore_filter_crt.mode;
#else
	return 0U;
#endif
}


void guicore_set_filter_crt_mode(auint mode)
{
#ifdef ENABLE_DISPLAY_FILTERS
	if ((mode == 7U) || (mode == 8U) || (mode > (auint)FILTER_CRT_HEATWAVE)){
		mode = (auint)FILTER_CRT_NONE;
	}
	guicore_filter_crt.mode = (filter_crt_mode_t)mode;
#else
	(void)mode;
#endif
}

auint guicore_get_render_path(void)
{
	return guicore_render_path;
}

void guicore_set_render_path(auint mode)
{
	if (mode > RENDER_PATH_STAGED){
		mode = RENDER_PATH_STAGED;
	}
	guicore_render_path = mode;
}


void guicore_set_jamma(auint flags)
{
	guicore_jamma_flags = flags & (JAMMA_ROTATE_90 | JAMMA_ROTATE_180 | JAMMA_ROTATE_270 |
	                               JAMMA_FLIP_H | JAMMA_FLIP_V);
}


auint guicore_get_jamma(void)
{
	return guicore_jamma_flags;
}


uint32 guicore_packrgb(auint r, auint g, auint b)
{
	return ((r & 0xFFU) << guicore_pixfmt.rsh) |
	       ((g & 0xFFU) << guicore_pixfmt.gsh) |
	       ((b & 0xFFU) << guicore_pixfmt.bsh) |
	       guicore_amask;
}


void guicore_set_status_led(boole on)
{
 guicore_status_led_on = on ? TRUE : FALSE;
}


void guicore_gettexsize(auint* texw, auint* texh)
{
	auint offx;
	auint offy;

	guicore_getrendergeom(texw, texh, &offx, &offy);
}


boole guicore_window_to_tex(int wx, int wy, int* tx, int* ty)
{
#ifdef HEADLESS
	(void)wx;
	(void)wy;
	if (tx != NULL){ *tx = 0; }
	if (ty != NULL){ *ty = 0; }
	return FALSE;
#else
	auint texw;
	auint texh;
	auint offx;
	auint offy;
	int   wndw;
	int   wndh;
	int   rx;
	int   ry;

	guicore_getrendergeom(&texw, &texh, &offx, &offy);

	/* SDL_RenderSetLogicalSize() can already remap mouse coordinates into
	** logical render coordinates. In that case, using window size here would
	** scale them a second time, which breaks clicks after resizing the window.
	** Prefer already-logical coordinates when they are in range, and only fall
	** back to window-space scaling when values are outside the logical surface.
	*/
	if ((wx >= 0) && (wy >= 0) && (wx < (int)texw) && (wy < (int)texh)){
		rx = wx;
		ry = wy;
		if (tx != NULL){ *tx = rx; }
		if (ty != NULL){ *ty = ry; }
		return TRUE;
	}

#ifdef USE_SDL1
	wndw = (int)guicore_surface->w;
	wndh = (int)guicore_surface->h;
#else
	SDL_GetWindowSize(guicore_window, &wndw, &wndh);
#endif
	if ((wndw <= 0) || (wndh <= 0)){
		if (tx != NULL){ *tx = 0; }
		if (ty != NULL){ *ty = 0; }
		return FALSE;
	}
	rx = (wx * (int)texw) / wndw;
	ry = (wy * (int)texh) / wndh;
	if (tx != NULL){ *tx = rx; }
	if (ty != NULL){ *ty = ry; }
	return (rx >= 0) && (ry >= 0) && (rx < (int)texw) && (ry < (int)texh);
#endif
}

boole guicore_window_to_source(int wx, int wy, int* sx, int* sy)
{
#ifdef HEADLESS
 (void)wx;
 (void)wy;
 if (sx != NULL){ *sx = 0; }
 if (sy != NULL){ *sy = 0; }
 return FALSE;
#else
 guicore_view_t view;
 auint dstx;
 auint dsty;
 auint dstw;
 auint dsth;
 auint rw;
 auint rh;
 auint ox;
 auint oy;
 auint srcax;
 auint srcay;
 auint rot;
 auint flags = guicore_jamma_flags;
 boole flip_h = FALSE;
 boole flip_v = FALSE;
 int tx;
 int ty;

 if (!guicore_window_to_tex(wx, wy, &tx, &ty)){
  if (sx != NULL){ *sx = 0; }
  if (sy != NULL){ *sy = 0; }
  return FALSE;
 }

 guicore_getview(&view);
 guicore_get_content_rect(&view, &dstx, &dsty, &dstw, &dsth, &rw, &rh);
 if ((dstw == 0U) || (dsth == 0U)){
  if (sx != NULL){ *sx = 0; }
  if (sy != NULL){ *sy = 0; }
  return FALSE;
 }
 if ((tx < (int)dstx) || (ty < (int)dsty) ||
     (tx >= (int)(dstx + dstw)) || (ty >= (int)(dsty + dsth))){
  if (sx != NULL){ *sx = 0; }
  if (sy != NULL){ *sy = 0; }
  return FALSE;
 }

 ox = (((auint)(tx - (int)dstx)) * rw) / dstw;
 oy = (((auint)(ty - (int)dsty)) * rh) / dsth;
 if (ox >= rw){ ox = rw - 1U; }
 if (oy >= rh){ oy = rh - 1U; }

 if ((flags & JAMMA_ROTATE_90) != 0U){ rot = 1U; }
 else if ((flags & JAMMA_ROTATE_180) != 0U){ rot = 2U; }
 else if ((flags & JAMMA_ROTATE_270) != 0U){ rot = 3U; }
 else{ rot = 0U; }
 flip_h = ((flags & JAMMA_FLIP_H) != 0U) ? TRUE : FALSE;
 flip_v = ((flags & JAMMA_FLIP_V) != 0U) ? TRUE : FALSE;

 if (flip_h){ ox = rw - 1U - ox; }
 if (flip_v){ oy = rh - 1U - oy; }

 switch (rot){
  default:
  case 0U:
   srcax = ox;
   srcay = oy;
   break;
  case 1U:
   srcax = oy;
   srcay = view.srch - 1U - ox;
   break;
  case 2U:
   srcax = view.srcw - 1U - ox;
   srcay = view.srch - 1U - oy;
   break;
  case 3U:
   srcax = view.srcw - 1U - oy;
   srcay = ox;
   break;
 }

 if (sx != NULL){ *sx = (int)(view.srcx + srcax); }
 if (sy != NULL){ *sy = (int)(view.srcy + srcay); }
 return TRUE;
#endif
}

uint32 guicore_get_source_pixel(auint sx, auint sy)
{
 if ((sx >= 320U) || (sy >= 270U)){
  return 0xFF000000U;
 }
 return guicore_native_to_argb(guicore_srcpix(sx, sy));
}


void guicore_get_source_bounds(int* sx, int* sy, auint* sw, auint* sh)
{
 guicore_view_t view;

 guicore_getview(&view);
 if (sx != NULL){ *sx = (int)view.srcx; }
 if (sy != NULL){ *sy = (int)view.srcy; }
 if (sw != NULL){ *sw = view.srcw; }
 if (sh != NULL){ *sh = view.srch; }
}


/*
** Draws the physical board LED into the FINAL display texture. This is
** deliberately later than textgui_draw(): the top 1bpp-style HUD has already
** been converted/copied into the texture at this point, so neither the text
** renderer nor the direct/staged render path can erase the indicator.
*/
static void guicore_draw_status_led(uint32* dest, auint dpitch,
                                    auint texw, auint texh)
{
 static uint8 const lamp[5U * 5U] = {
  0U, 1U, 1U, 1U, 0U,
  1U, 3U, 2U, 2U, 1U,
  1U, 2U, 2U, 2U, 1U,
  1U, 2U, 2U, 2U, 1U,
  0U, 1U, 1U, 1U, 0U
 };
 auint scale;
 auint x0;
 auint y0;
 auint x;
 auint y;
 auint xx;
 auint yy;
 uint32 edge;
 uint32 fill;
 uint32 glint;
 uint32 col;
 uint8 p;

 if ((guicore_flags & GUICORE_GAMEONLY) != 0U){ return; }

 scale = ((guicore_flags & GUICORE_SMALL) != 0U) ? 1U : 2U;
 x0 = GUICORE_STATUS_LED_X * scale;
 y0 = GUICORE_STATUS_LED_Y * scale;

 if (((x0 + (5U * scale)) > texw) ||
     ((y0 + (5U * scale)) > texh)){
  return;
 }

 edge  = guicore_packrgb(88U, 96U, 88U);
 fill  = guicore_status_led_on ? guicore_packrgb(0U, 255U, 36U)
                               : guicore_packrgb(0U, 38U, 6U);
 glint = guicore_status_led_on ? guicore_packrgb(200U, 255U, 204U)
                               : guicore_packrgb(34U, 72U, 36U);

 for (y = 0U; y < 5U; ++y){
  for (x = 0U; x < 5U; ++x){
   p = lamp[(y * 5U) + x];
   if (p == 0U){ continue; }
   col = (p == 1U) ? edge : ((p == 3U) ? glint : fill);
   for (yy = 0U; yy < scale; ++yy){
    for (xx = 0U; xx < scale; ++xx){
     dest[((y0 + (y * scale) + yy) * dpitch) +
          x0 + (x * scale) + xx] = col;
    }
   }
  }
 }
}


/*
** Updates display. Normally this sticks to the display's refresh rate. If
** drop is TRUE, it does nothing (drops the frame).
*/
void guicore_update(boole drop)
{
#ifdef HEADLESS
 return;
#endif
#ifdef USE_SDL1

 auint   texw;
 auint   texh;
 auint   offx;
 auint   offy;

 if (drop){ return; }

 if ((guicore_flags & GUICORE_GAMEONLY) != 0U){
  texw = TEXSG_W;
  texh = TEXSG_H;
  offx = TGOG_X;
  offy = TGOG_Y;
 }else{
  texw = TEXS_W;
  texh = TEXS_H;
  offx = 0U;
  offy = 0U;
 }

 if (SDL_LockTexture(guicore_texture, NULL, &pixels, &pitch) == 0){
  auint ppitch = (auint)(pitch >> 2);

  guicore_render((uint32*)pixels, ppitch, texw, texh);

  guicore_apply_prefx((uint32*)pixels, ppitch, texw, texh);
  guicore_apply_postfx((uint32*)pixels, ppitch, texw, texh);
  guicore_draw_status_led((uint32*)pixels, ppitch, texw, texh);

  SDL_UnlockTexture(guicore_texture);
 }

 SDL_UpdateRect(guicore_surface, 0, 0, 0, 0);

#else

 auint   texw;
 auint   texh;
 auint   offx;
 auint   offy;
 void*   pixels;
 int     pitch;

 if (drop){ return; }

 guicore_getrendergeom(&texw, &texh, &offx, &offy);

 if (SDL_LockTexture(guicore_texture, NULL, &pixels, &pitch) == 0){
	auint ppitch = (auint)(pitch >> 2);
	if (guicore_use_direct_present()){
		if ((guicore_flags & GUICORE_SMALL) != 0U){
			guicore_render_1x((uint32*)pixels, ppitch, offx, offy, texw, texh);
		}else{
			guicore_render_2x((uint32*)pixels, ppitch, offx >> 1, offy >> 1, texw >> 1, texh >> 1);
		}
	}else{
		guicore_render((uint32*)pixels, ppitch, texw, texh);
	}
	guicore_apply_prefx((uint32*)pixels, ppitch, texw, texh);
	guicore_apply_postfx((uint32*)pixels, ppitch, texw, texh);
	guicore_draw_status_led((uint32*)pixels, ppitch, texw, texh);
#ifdef ENABLE_MICROUI
	mui_render_overlay((uint32*)pixels, ppitch, texw, texh);
#endif
	SDL_UnlockTexture(guicore_texture);
 }

 SDL_RenderClear(guicore_renderer);
 SDL_RenderCopy(guicore_renderer, guicore_texture, NULL, NULL);
 SDL_RenderPresent(guicore_renderer);

#endif
}



/*
** Sets the window caption (if any can be displayed)
*/
void guicore_setcaption(const char* title)
{
#ifdef HEADLESS
 return;
#endif
#ifndef __EMSCRIPTEN__

#ifdef USE_SDL1

 SDL_WM_SetCaption(title, NULL);

#else

 SDL_SetWindowTitle(guicore_window, title);

#endif

#endif
}


void guicore_seticon_uze(uint8 const* icon)
{
 auint i;
 boole allblack = TRUE;

 if (icon == NULL){
  guicore_window_icon_valid = FALSE;
  guicore_window_icon_generic = TRUE;
 }else{
  for (i = 0U; i < sizeof(guicore_window_icon); i++){
   if (icon[i] != 0U){
    allblack = FALSE;
    break;
   }
  }
  if (allblack){
   guicore_window_icon_valid = FALSE;
   guicore_window_icon_generic = TRUE;
  }else{
   memcpy(&(guicore_window_icon[0]), icon, sizeof(guicore_window_icon));
   guicore_window_icon_valid = TRUE;
   guicore_window_icon_generic = FALSE;
  }
 }

#if !defined(HEADLESS) && !defined(__EMSCRIPTEN__)
 guicore_applyicon();
#endif
}
