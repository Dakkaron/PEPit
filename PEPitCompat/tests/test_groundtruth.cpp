/*
 * PEPitCompat — Unit tests: RGB565 buffer byte order (TFT_eSPI + gfxHandler)
 *
 * Pinned convention: every 16bpp TFT_eSprite pixel buffer (the display sprite
 * as well as sprites filled by gfxHandler loadBmp) holds RGB565 words in
 * byte-swapped order relative to the native color565 layout. Evidence:
 * TFT_eSprite::fillRect(TFT_RED=0xF800) stores 0x00F8, gfxHandler loadBmp
 * stores SWAP_BYTES(COMPOSE_RGB565(r,g,b)) = 0x00F8 for pure red, and
 * pushSpriteFast pushes buffer words verbatim to the 8-bit parallel bus.
 * engine3d samples textures and fills the display through getPointer(), so it
 * must honour this layout at every buffer interface.
 */

#include "catch_amalgamated.hpp"

#include <cstdint>
#include <string>

#include "Arduino.h"
#include "TFT_eSPI.h"
#include "gfxHandler.hpp"
#include "test_helpers.h"

using pepit_test::makeSdRoot;
using pepit_test::writeSdFile;

namespace {

// 1x1 pure red BMP (B,G,R per spec)
std::string redBmp1x1() {
    std::string bmp(54 + 4, 0);
    bmp[0] = 'B'; bmp[1] = 'M';
    auto p32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) bmp[o + i] = char((v >> (8 * i)) & 0xFF); };
    auto p16 = [&](size_t o, uint16_t v) { bmp[o] = char(v & 0xFF); bmp[o + 1] = char((v >> 8) & 0xFF); };
    p32(2, bmp.size()); p32(10, 54); p32(14, 40); p32(18, 1); p32(22, 1);
    p16(26, 1); p16(28, 24); p32(34, 4);
    bmp[54] = 0x00; // B
    bmp[55] = 0x00; // G
    bmp[56] = 0xFF; // R
    return bmp;
}

}  // namespace

TEST_CASE("PINED CONVENTION: 16bpp sprite buffers store RGB565 byteswapped", "[gfx][buffer-layout]") {
    auto root = makeSdRoot("buffer_layout");
    writeSdFile(root, "red.bmp", redBmp1x1());

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(16, 16);

    disp.fillSprite(0);
    disp.fillRect(10, 10, 4, 4, TFT_RED);
    uint16_t* fb = (uint16_t*)disp.getPointer();
    CHECK(fb[10 * 16 + 10] == 0x00F8);  // byteswapped TFT_RED, not 0xF800

    REQUIRE(loadBmp(&disp, "/red.bmp"));
    fb = (uint16_t*)disp.getPointer();  // loadBmp re-creates the sprite at BMP size
    CHECK(disp.width() == 1);
    CHECK(disp.height() == 1);
    CHECK(fb[0] == 0x00F8);            // same swapped layout as fillRect

    disp.deleteSprite();
}
