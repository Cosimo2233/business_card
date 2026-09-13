#ifndef KEYBOARD_LOGIC_H
#define KEYBOARD_LOGIC_H
#include <stdint.h>
#include <string.h>

#define ROW_CNT 8
#define COL_CNT 7
/* Coordinates always mean schematic ROW1..8 / COLUMN1..7, zero based. */
static const uint8_t KTab[ROW_CNT][COL_CNT] = {
    {0x29,0x14,0x1a,0x08,0x15,0x17,0x1c},
    {0x2b,0x04,0x16,0x07,0x09,0x0a,0x0b},
    {0xe1,0x1d,0x1b,0x06,0x19,0x05,0x11},
    {0xe0,0xe3,0xe2,0,0x2c,0x2c,0x2c},
    {0,0,0x2a,0x13,0x12,0x0c,0x18},
    {0,0,0x28,0x33,0x0f,0x0e,0x0d},
    {0,0,0x38,0x52,0x37,0x36,0x10},
    {0,0,0x4f,0x51,0x50,0,0x2c}
};
static const uint8_t FTab[ROW_CNT][COL_CNT] = {
    {0,0x1e,0x1f,0x20,0x21,0x22,0x23},
    {0},{0},{0},
    {0,0,0,0x27,0x26,0x25,0x24},
    {0},{0},{0}
};

static uint8_t KeyValid(uint8_t r, uint8_t c)
{
    return r < ROW_CNT && c < COL_CNT && (r < 4 || c >= 2);
}

/* Self capacitance gives row/column sets, not individual intersections.
 * Do not emit every intersection of a multi-row/multi-column rectangle.
 * Incremental pairing assumes previously held fingers have not moved;
 * arbitrary simultaneous touches or replacement of fingers remain ambiguous. */
static void ResolveTouches(uint8_t rows, uint8_t cols,
                           const uint8_t held[ROW_CNT][COL_CNT],
                           uint8_t out[ROW_CNT][COL_CNT])
{
    uint8_t r,c,nr=0,nc=0,known_rows=0,known_cols=0;
    memset(out,0,ROW_CNT*COL_CNT);
    for(r=0;r<ROW_CNT;r++) if(rows & (1u<<r)) nr++;
    for(c=0;c<COL_CNT;c++) if(cols & (1u<<c)) nc++;
    for(r=0;r<ROW_CNT;r++) for(c=0;c<COL_CNT;c++) {
        if(!KeyValid(r,c) || !(rows & (1u<<r)) || !(cols & (1u<<c))) continue;
        if(nr==1 || nc==1 || held[r][c]) out[r][c]=1;
        if(out[r][c]) { known_rows |= 1u<<r; known_cols |= 1u<<c; }
    }
    /* Add exactly one unexplained row/column pair; all other new ambiguous
     * touches remain suppressed until released or uniquely identifiable. */
    rows &= (uint8_t)~known_rows;
    cols &= (uint8_t)~known_cols;
    if(rows && !(rows & (rows-1)) && cols && !(cols & (cols-1))) {
        for(r=0;r<ROW_CNT;r++) for(c=0;c<COL_CNT;c++)
            if((rows & (1u<<r)) && (cols & (1u<<c)) && KeyValid(r,c)) out[r][c]=1;
    }
}

static void BuildReport(const uint8_t keys[ROW_CNT][COL_CNT], uint8_t report[8])
{
    uint8_t r,c,k,i,n=2,overflow=0;
    uint8_t fn=keys[3][3] || keys[7][5];
    memset(report,0,8);
    for(r=0;r<ROW_CNT;r++) for(c=0;c<COL_CNT;c++) {
        if(!KeyValid(r,c) || !keys[r][c]) continue;
        k=fn && FTab[r][c] ? FTab[r][c] : KTab[r][c];
        if(!k) continue;
        if(k>=0xe0 && k<=0xe7) { report[0] |= 1u<<(k-0xe0); continue; }
        for(i=2;i<n;i++) if(report[i]==k) break;
        if(i<n) continue; /* Several space electrodes represent one key. */
        if(n<8) report[n++]=k;
        else overflow=1;
    }
    if(overflow) memset(report+2,1,6); /* HID ErrorRollOver */
}
#endif
