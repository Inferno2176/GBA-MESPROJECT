#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_dma.h>
#include "tiles.h"
#include "townmap.h"
#include "player.h"

// Hardware Registers
#define REG_DISPLAYCONTROL  *((volatile u16*)(0x04000000))
#define REG_BG0_CONTROL     *((volatile u16*)(0x04000008))
#define REG_BG0HOFS         *((volatile u16*)(0x04000010)) // Camera X
#define REG_BG0VOFS         *((volatile u16*)(0x04000012)) // Camera Y
#define REG_KEYINPUT        *((volatile u16*)(0x04000130)) // Buttons

// Button Definitions
#define KEY_RIGHT           (1 << 4)
#define KEY_LEFT            (1 << 5)
#define KEY_UP              (1 << 6)
#define KEY_DOWN            (1 << 7)

// OAM (Object Attribute Memory) Constants
#define ATTR0_DISABLE       (2 << 8) // Bit flag to disable sprite rendering

typedef u16 ScreenBlock[1024];
typedef u32 TileBlock[256][8];

#define MEM_TILE            ((TileBlock*)0x06000000)
#define MEM_SCREENBLOCKS    ((ScreenBlock*)0x06000000)
#define MEM_BG_PALETTE      ((u16*)0x05000000)

// ---------------------------------------------------------
// COLLISION DETECTION SYSTEM
// ---------------------------------------------------------
// ---------------------------------------------------------
// COLLISION DETECTION SYSTEM (Whitelist Approach)
// ---------------------------------------------------------
int isSolid(int worldX, int worldY) {
    int tileX = worldX / 8;
    int tileY = worldY / 8;

    if (tileX < 0 || tileX >= MAP_W || tileY < 0 || tileY >= MAP_H) {
        return 1; // Off the map is solid!
    }

    u16 tileData = townMap[tileY][tileX];
    u16 rawTileID = tileData & 0x03FF; 

    // Check if the tile matches ANY of your walkable IDs
    switch(rawTileID) {
        case 8: case 9: case 10: case 15: case 16: case 17: case 18: case 19:
        case 20: case 21: case 22: case 23: case 32: case 33: case 34: case 39:
        case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
        case 54: case 55: case 56: case 57: case 66: case 67: case 68: case 69:
        case 70: case 71: case 78: case 79: case 80: case 81: case 90: case 91:
        case 92: case 93: case 94: case 95: case 114: case 115: case 116: case 117:
        case 118: case 119: case 138: case 139: case 140: case 141: case 142: case 143:
        case 144: case 145: case 146: case 147: case 148: case 149: case 168: case 169:
        case 170: case 171: case 172: case 173: case 192: case 193: case 194: case 195:
        case 196: case 197: case 216: case 217: case 218: case 219: case 220: case 221:
        case 240: case 241: case 242: case 243: case 244: case 245: case 264: case 265:
        case 266: case 267: case 268: case 269: case 291: case 315: case 337: case 338:
        case 340: case 350: case 351: case 352: case 353: case 361: case 362: case 364:
        case 374: case 375: case 376: case 377: case 390: case 391: case 398: case 399:
        case 400: case 401: case 414: case 415: case 422: case 423: case 424: case 425:
            return 0; // It's in the list! Safe to walk.
        default:
            return 1; // It wasn't in the list! Block movement.
    }
}

int main(void) {
    irqInit();
    irqEnable(IRQ_VBLANK);

    // Load Background Palettes and Tiles
    CpuFastSet(tilesPal,   MEM_BG_PALETTE,  tilesPalLen / 4);
    CpuFastSet(tilesTiles, &MEM_TILE[0][0], tilesTilesLen / 4);

    // ---------------------------------------------------------
    // CLEAR BACKGROUND VRAM FIRST
    // Zero out Screen Blocks 16, 17, 18, 19 to prevent garbage data
    // ---------------------------------------------------------
    for (int sb = 16; sb <= 19; sb++) {
        for (int i = 0; i < 1024; i++) {
            MEM_SCREENBLOCKS[sb][i] = 0; // Fallback transparent/grass tile
        }
    }

    // ---------------------------------------------------------
    // LOAD MAP DATA INTO SCREEN BLOCKS (With Tiled Offset Fix)
    // ---------------------------------------------------------
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            int sbx = x / 32;
            int sby = y / 32;
            int sb = 16 + sby * 2 + sbx;
            int tx = x % 32;
            int ty = y % 32;

            // Fetch the raw tile ID from Tiled CSV
            u16 tileID = townMap[y][x];

            // Fix the Tiled 1-Index offset
            if (tileID > 0) {
                tileID = tileID - 1;
            } else {
                tileID = 0; // Safety fallback to tile 0
            }

            ((volatile u16*)&MEM_SCREENBLOCKS[sb])[ty * 32 + tx] = townMap[y][x];
        }
    }

    // Setup Background 0 (64x64 size)
    REG_BG0_CONTROL = (0) | (1 << 7) | (16 << 8) | (3 << 14);

    // Enable BG0 + OBJ + 1D mapping
    REG_DISPLAYCONTROL = 0x0000 | 0x0100 | 0x1000 | 0x0040;

    // Load player palette into OBJ palette RAM
    volatile u16* objPal = (volatile u16*)0x05000200;
    for (int i = 0; i < 16; i++) objPal[i] = playerPal[i];

    // Load player tiles into OBJ VRAM
    volatile u32* objVram = (volatile u32*)0x06010000;
    for (int i = 0; i < 32; i++) objVram[i] = playerTiles[i];

    // ---------------------------------------------------------
    // PROPERLY DISABLE UNUSED SPRITES
    // ---------------------------------------------------------
    volatile u16* oam = (volatile u16*)0x07000000;
    for (int i = 0; i < 128 * 4; i += 4) {
        oam[i]   = 160 | ATTR0_DISABLE; // Move offscreen AND explicitly disable
        oam[i+1] = 0;
        oam[i+2] = 0;
        oam[i+3] = 0;
    }

    // ---------------------------------------------------------
    // SET OAM ENTRY 0 - PLAYER SPRITE (Fixed 8x8 Size)
    // ---------------------------------------------------------
    int spriteX = 116; // Centered X for an 8px wide sprite (120 - 4)
    int spriteY = 76;  // Centered Y for an 8px high sprite (80 - 4)
    oam[0] = spriteY;                 // Y position, shape=00 (square)
    oam[1] = spriteX | (0 << 14);     // X position, size=00 (8x8 pixels)
    oam[2] = 0;                       // tile 0, palette 0

// ---------------------------------------------------------
    // GAME EXECUTION LOOP (Grid Movement)
    // ---------------------------------------------------------
    int bgScrollX = 0;
    int bgScrollY = 0;
    
    // State Machine Variables
    int isMoving = 0;
    int targetScrollX = 0;
    int targetScrollY = 0;
    int moveSpeed = 2; // Pixels per frame. (2 divides perfectly into 16)

    while (1) {
        VBlankIntrWait();
        u16 keys = ~REG_KEYINPUT;

        // STATE 1: WE ARE CURRENTLY WALKING TO A GRID SQUARE
        if (isMoving) {
            // Move the camera smoothly towards the target
            if (bgScrollX < targetScrollX) bgScrollX += moveSpeed;
            if (bgScrollX > targetScrollX) bgScrollX -= moveSpeed;
            if (bgScrollY < targetScrollY) bgScrollY += moveSpeed;
            if (bgScrollY > targetScrollY) bgScrollY -= moveSpeed;

            // Did we reach the target grid square exactly?
            if (bgScrollX == targetScrollX && bgScrollY == targetScrollY) {
                isMoving = 0; // Stop moving and wait for the next button press!
            }
        } 
        
        // STATE 2: WE ARE STANDING STILL, WAITING FOR INPUT
        else {
            int nextX = bgScrollX;
            int nextY = bgScrollY;

            // Grid size is 16 pixels (2 tiles)
            if (keys & KEY_RIGHT)      nextX += 16;
            else if (keys & KEY_LEFT)  nextX -= 16;
            else if (keys & KEY_DOWN)  nextY += 16;
            else if (keys & KEY_UP)    nextY -= 16;

            // If a button was pressed and we want to move
            if (nextX != bgScrollX || nextY != bgScrollY) {
                
                // Calculate the world coordinate of the grid square we want to step on
                int destWorldX = nextX + 120;
                int destWorldY = nextY + 80;

                // Check collision FIRST
                if (isSolid(destWorldX, destWorldY) == 0) {
                    // Path is clear! Lock controls and start walking
                    targetScrollX = nextX;
                    targetScrollY = nextY;
                    isMoving = 1;
                }
            }
        }

        // Write the new camera position to the hardware
        REG_BG0HOFS = bgScrollX;
        REG_BG0VOFS = bgScrollY;
    }

    return 0;
}