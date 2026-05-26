/*******************************************************************************
 * 文件名: Main.c
 * 描述: CH582M 电容触摸键盘固件 - 48键 USB HID 键盘
 *       矩阵: 左边4行x7列 + 右边4行x5列, CH582M TouchKey + RC 检测
 ******************************************************************************/
#include "CH58x_common.h"

/* ======================== USB HID 描述符 ======================== */
const uint8_t KeyboardReportDesc[] = {
    0x05,0x01,0x09,0x06,0xA1,0x01,
    0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,
    0x95,0x01,0x75,0x03,0x91,0x01,
    0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,
    0x29,0x65,0x81,0x00,0xC0
};

__attribute__((aligned(4))) uint8_t EP0_Databuf[192], EP1_Databuf[128];
__attribute__((aligned(4))) uint8_t HIDKeyBuff[8], HIDKeyBuffPrev[8];

const uint8_t MyDevDescr[] = {0x12,0x01,0x10,0x01,0x00,0x00,0x00,0x08,
    0x86,0x1A,0x24,0x55,0x00,0x01,0x01,0x02,0x00,0x01};
const uint8_t MyCfgDescr[] = {
    0x09,0x02,0x3B,0x00,0x01,0x01,0x00,0x80,0x32,
    0x09,0x04,0x00,0x00,0x01,0x03,0x01,0x01,0x00,
    0x09,0x21,0x10,0x01,0x00,0x01,0x22,0x45,0x00,
    0x07,0x05,0x81,0x03,0x08,0x00,0x0A};

/* ======================== 引脚与矩阵定义 ======================== */
#define COL_CNT 7
#define ROW_CNT 8
#define KEY_CNT 48

/* 列引脚 PA4/5/12/13/14/15/3 → TKEY0~6 */
const uint32_t col_pin[COL_CNT] = {GPIO_Pin_4,GPIO_Pin_5,GPIO_Pin_12,GPIO_Pin_13,
                                   GPIO_Pin_14,GPIO_Pin_15,GPIO_Pin_3};
/* 行引脚 PA6/0/1/2/7/8/9 → TKEY10/9/8/7/11/12/13 */
const uint32_t row_pin[ROW_CNT-1] = {GPIO_Pin_6,GPIO_Pin_0,GPIO_Pin_1,GPIO_Pin_2,
                                     GPIO_Pin_7,GPIO_Pin_8,GPIO_Pin_9};
/* 各行对应的 TKEY 通道号 */
const uint8_t row_ch[ROW_CNT-1] = {10,9,8,7,11,12,13};

/* 键值表: [row][col] */
const uint8_t KTab[ROW_CNT][COL_CNT]={
    {0x29,0x14,0x1A,0x08,0x15,0x17,0x1C},
    {0x2B,0x04,0x16,0x07,0x09,0x0A,0x0B},
    {0xE1,0x1D,0x1B,0x06,0x19,0x05,0x11},
    {0xE0,0xE3,0xE2,0x00,0x2C,0x2C,0x2C},
    {0x18,0x0C,0x12,0x13,0x2A,0x00,0x00},
    {0x0D,0x0E,0x0F,0x33,0x28,0x00,0x00},
    {0x10,0x36,0x37,0x52,0x38,0x00,0x00},
    {0x2C,0x00,0x50,0x51,0x4F,0x00,0x00}
};
/* Fn+Q/W/E/R/T/Y/U/I/O/P = 1~0 */
const uint8_t FTab[ROW_CNT][COL_CNT]={
    {0x00,0x1E,0x1F,0x20,0x21,0x22,0x23},
    {0},{0},{0},
    {0x24,0x25,0x26,0x27,0x00,0,0},{0},{0},{0}
};

const uint8_t KMap[KEY_CNT][2]={
    {0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},
    {1,0},{1,1},{1,2},{1,3},{1,4},{1,5},{1,6},
    {2,0},{2,1},{2,2},{2,3},{2,4},{2,5},{2,6},
    {3,0},{3,1},{3,2},{3,3},{3,4},{3,5},{3,6},
    {4,2},{4,3},{4,4},{4,5},{4,6},
    {5,2},{5,3},{5,4},{5,5},{5,6},
    {6,2},{6,3},{6,4},{6,5},{6,6},
    {7,2},{7,3},{7,4},{7,5},{7,6}
};

/* ======================== 全局变量 ======================== */
volatile uint8_t fn_pressed=0, key_state[KEY_CNT]={0}, key_prev[KEY_CNT]={0};
uint16_t tkey_base[ROW_CNT][COL_CNT]={0};

/* 可调参数 */
#define TK_THRESH  80    /* TouchKey 触摸阈值 */
#define R8_THRESH  20    /* R8 行 RC 充电阈值 */
#define R8_TIMEOUT 5000

/* ======================== TouchKey 底层 ======================== */
static void TK_InitHW(void)
{
    R8_ADC_CFG = RB_ADC_POWER_ON | RB_ADC_BUF_EN | (1<<4);
    R8_TKEY_CFG |= RB_TKEY_PWR_ON;

    GPIOAGPPCfg(ENABLE, RB_PIN_ADC0_IE|RB_PIN_ADC1_IE|RB_PIN_ADC2_3_IE|
                        RB_PIN_ADC4_5_IE|RB_PIN_ADC6_7_IE|RB_PIN_ADC8_9_IE|
                        RB_PIN_ADC10_IE|RB_PIN_ADC11_IE|
                        RB_PIN_ADC12_IE|RB_PIN_ADC13_IE);
}

static uint16_t TK_Read(uint8_t ch, uint8_t cg, uint8_t dcg)
{
    R8_ADC_CHANNEL = ch;
    DelayUs(2);
    R8_TKEY_COUNT = (dcg<<5) | (cg&0x1f);
    R8_TKEY_CONVERT = RB_TKEY_START;
    while(R8_TKEY_CONVERT & RB_TKEY_START);
    return R16_ADC_DATA & RB_ADC_DATA;
}

/* ======================== 初始化 ======================== */
void TK_Calib(void)
{
    uint8_t r,c;
    TK_InitHW();
    for(r=0;r<ROW_CNT-1;r++){
        TK_Read(row_ch[r],8,1);
        DelayUs(5);
        for(c=0;c<COL_CNT;c++){
            tkey_base[r][c] = TK_Read(c,8,1);
            DelayUs(2);
            tkey_base[r][c] = (tkey_base[r][c]+TK_Read(c,8,1))>>1;
        }
    }
}

/* ======================== R8 行检测 (PB8/PB9 RC 充放电) ======================== */
void R8_Init(void)
{
    GPIOB_ModeCfg(GPIO_Pin_8, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(GPIO_Pin_8);
}

uint8_t R8_Detect(void)
{
    uint16_t t;
    GPIOB_SetBits(GPIO_Pin_8);
    for(t=0; t<R8_TIMEOUT; t++){
        if(GPIOB_ReadPortPin(GPIO_Pin_9)) break;
    }
    GPIOB_ResetBits(GPIO_Pin_8);
    { volatile uint16_t d; for(d=0;d<1000;d++); }
    return (t>R8_THRESH) ? 1 : 0;
}

/* ======================== 矩阵扫描 ======================== */
void MatrixScan(void)
{
    uint8_t r,c,idx;
    uint16_t v,d;

    for(r=0;r<ROW_CNT-1;r++){
        R32_PA_CLR = row_pin[r];
        DelayUs(3);
        for(c=0;c<COL_CNT;c++){
            v = TK_Read(c,8,1);
            d = (v>tkey_base[r][c]) ? (v-tkey_base[r][c]) : 0;
            idx = r*COL_CNT+c;
            if(idx>=KEY_CNT) continue;
            if((r<4 || c>=2) && d>TK_THRESH) key_state[idx]=1;
        }
        R32_PA_OUT |= row_pin[r];
    }

    /* R8 行 */
    if(R8_Detect()){
        for(c=2;c<COL_CNT;c++){
            v = TK_Read(c,8,1);
            d = (v>tkey_base[7][c]) ? (v-tkey_base[7][c]) : 0;
            idx = 7*COL_CNT+c;
            if(d>TK_THRESH) key_state[idx]=1;
        }
    }
}

/* ======================== HID 报告 ======================== */
void SendReport(void)
{
    uint8_t i,idx,r,c,k,mod=0,ri=2;
    for(i=0;i<8;i++) HIDKeyBuff[i]=0;

    if(key_state[3*COL_CNT+3] || key_state[7*COL_CNT+1]) fn_pressed=1;

    for(idx=0;idx<KEY_CNT && ri<8;idx++){
        if(!key_state[idx]) continue;
        r=KMap[idx][0]; c=KMap[idx][1];
        if((r==3&&c==3)||(r==7&&c==1)) continue;
        k = fn_pressed ? FTab[r][c] : KTab[r][c];
        if(fn_pressed && k==0) k=KTab[r][c];
        if(k==0xE0)      mod|=0x01;
        else if(k==0xE1) mod|=0x02;
        else if(k==0xE2) mod|=0x04;
        else if(k==0xE3) mod|=0x08;
        else HIDKeyBuff[ri++]=k;
    }
    HIDKeyBuff[0]=mod;

    for(i=0;i<8;i++) if(HIDKeyBuff[i]!=HIDKeyBuffPrev[i]) break;
    if(i==8) return;
    for(i=0;i<8;i++) HIDKeyBuffPrev[i]=HIDKeyBuff[i];
    for(i=0;i<8;i++) EP1_Databuf[64+i]=HIDKeyBuff[i];
    R8_UEP1_T_LEN=8;
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

/* ======================== 主函数 ======================== */
int main(void)
{
    uint8_t i;
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    TK_Calib();
    R8_Init();

    /* USB 初始化 */
    R8_USB_CTRL=0;
    R8_UEP4_1_MOD=RB_UEP4_RX_EN|RB_UEP4_TX_EN|RB_UEP1_RX_EN|RB_UEP1_TX_EN;
    R8_UEP2_3_MOD=RB_UEP2_RX_EN|RB_UEP2_TX_EN|RB_UEP3_RX_EN|RB_UEP3_TX_EN;
    R16_UEP0_DMA=(uint16_t)(uint32_t)EP0_Databuf;
    R16_UEP1_DMA=(uint16_t)(uint32_t)EP1_Databuf;
    R8_UEP0_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK;
    R8_UEP1_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK|RB_UEP_AUTO_TOG;
    R8_USB_DEV_AD=0;
    R8_USB_CTRL=RB_UC_DEV_PU_EN|RB_UC_INT_BUSY|RB_UC_DMA_EN;
    R16_PIN_ANALOG_IE|=RB_PIN_USB_IE|RB_PIN_USB_DP_PU;
    R8_USB_INT_FG=0xFF;
    R8_UDEV_CTRL=RB_UD_PD_DIS|RB_UD_PORT_EN;
    R8_USB_INT_EN=RB_UIE_SUSPEND|RB_UIE_BUS_RST|RB_UIE_TRANSFER;
    PFIC_EnableIRQ(USB_IRQn);
    DelayMs(100);

    while(1){
        for(i=0;i<KEY_CNT;i++){ key_prev[i]=key_state[i]; key_state[i]=0; }
        fn_pressed=0;
        MatrixScan();
        for(i=0;i<KEY_CNT;i++) if(key_state[i]!=key_prev[i]) break;
        if(i<KEY_CNT) SendReport();
        DelayMs(5);
    }
}

/* ======================== USB 中断 ======================== */
void USB_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USB_IRQHandler(void)
{
    uint8_t f=R8_USB_INT_FG;
    if(f&RB_UIE_SUSPEND);
    if(f&RB_UIE_TRANSFER);
    if(f&RB_UIE_BUS_RST){
        R8_USB_DEV_AD=0;
        R8_UEP0_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK;
        R8_UEP1_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK|RB_UEP_AUTO_TOG;
        R8_USB_INT_FG=RB_UIE_BUS_RST;
    }
}
