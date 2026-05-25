/*******************************************************************************
 * 文件名: Main.c
 * 描述: CH582M 电容触摸键盘固件
 *       共 48键，使用 CH582M 内置 TouchKey 模块
 *       USB HID Keyboard 协议
 ******************************************************************************/
#include "CH58x_common.h"
#include "CH58x_sys.h"
#include "CH58x_gpio.h"
#include "CH58x_usbdev.h"

/* ======================== USB HID 描述符 ======================== */
/* USB 键盘报告描述符 */
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

__attribute__((aligned(4))) uint8_t EP0_Databuf[64+64+64];
__attribute__((aligned(4))) uint8_t EP1_Databuf[64+64];
__attribute__((aligned(4))) uint8_t HIDKeyBuff[8]={0};

uint8_t DevConfig=0, Ready=0;
uint8_t SetupReqCode;
uint16_t SetupReqLen;
const uint8_t *pDescr;

/* USB 设备描述符 */
const uint8_t MyDevDescr[] = {
    0x12,0x01,0x10,0x01,0x00,0x00,0x00,0x08,
    0x86,0x1A,0x24,0x55,0x00,0x01,0x01,0x02,0x00,0x01
};
/* USB 配置描述符 (含 HID, 端点) */
const uint8_t MyCfgDescr[] = {
    0x09,0x02,0x3B,0x00,0x01,0x01,0x00,0x80,0x32,
    0x09,0x04,0x00,0x00,0x01,0x03,0x01,0x01,0x00,
    0x09,0x21,0x10,0x01,0x00,0x01,0x22,0x45,0x00,
    0x07,0x05,0x81,0x03,0x08,0x00,0x0A
};

/* ======================== 引脚定义 ======================== */
#define COL1_PIN    GPIO_Pin_4
#define COL2_PIN    GPIO_Pin_5
#define COL3_PIN    GPIO_Pin_12
#define COL4_PIN    GPIO_Pin_13
#define COL5_PIN    GPIO_Pin_14
#define COL6_PIN    GPIO_Pin_15
#define COL7_PIN    GPIO_Pin_3
#define COL_ALL     (COL1_PIN|COL2_PIN|COL3_PIN|COL4_PIN|COL5_PIN|COL6_PIN|COL7_PIN)
#define COL_COUNT   7

#define R1_PIN      GPIO_Pin_6
#define R2_PIN      GPIO_Pin_0
#define R3_PIN      GPIO_Pin_1
#define R4_PIN      GPIO_Pin_2
#define R5_PIN      GPIO_Pin_7
#define R6_PIN      GPIO_Pin_8
#define R7_PIN      GPIO_Pin_9
#define R8_PIN      GPIO_Pin_9   /* PB9 */
#define ROW_COUNT   8

const uint32_t row_pin[8]={R1_PIN,R2_PIN,R3_PIN,R4_PIN,R5_PIN,R6_PIN,R7_PIN,R8_PIN};

/* TouchKey 通道: CH_EXTIN_0 ~ 6 */
const uint8_t TKC[7]={0,1,2,3,4,5,6};

/* ======================== 键值表 ======================== */
#define KEY_COUNT 48
const uint8_t KeyTable[ROW_COUNT][COL_COUNT]={
    {0x29,0x14,0x1A,0x08,0x15,0x17,0x1C},
    {0x2B,0x04,0x16,0x07,0x09,0x0A,0x0B},
    {0xE1,0x1D,0x1B,0x06,0x19,0x05,0x11},
    {0xE0,0xE3,0xE2,0x00,0x2C,0x2C,0x2C},
    {0x18,0x0C,0x12,0x13,0x2A,0x00,0x00},
    {0x0D,0x0E,0x0F,0x33,0x28,0x00,0x00},
    {0x10,0x36,0x37,0x52,0x38,0x00,0x00},
    {0x2C,0x00,0x50,0x51,0x4F,0x00,0x00}
};
const uint8_t FnTable[ROW_COUNT][COL_COUNT]={
    {0x00,0x1E,0x1F,0x20,0x21,0x22,0x23},
    {0,0,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,0,0,0},
    {0x24,0x25,0x26,0x27,0x00,0,0},{0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0},{0,0,0,0,0,0,0}
};
const uint8_t KeyMap[KEY_COUNT][2]={
    {0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},
    {1,0},{1,1},{1,2},{1,3},{1,4},{1,5},{1,6},
    {2,0},{2,1},{2,2},{2,3},{2,4},{2,5},{2,6},
    {3,0},{3,1},{3,2},{3,3},{3,4},{3,5},{3,6},
    {4,2},{4,3},{4,4},{4,5},{4,6},
    {5,2},{5,3},{5,4},{5,5},{5,6},
    {6,2},{6,3},{6,4},{6,5},{6,6},
    {7,2},{7,3},{7,4},{7,5},{7,6}
};
#define FN_R1 3
#define FN_C1 3
#define FN_R2 7
#define FN_C2 1

/* ======================== 全局变量 ======================== */
volatile uint8_t fn_pressed=0;
volatile uint8_t key_state[KEY_COUNT]={0};
volatile uint8_t key_prev[KEY_COUNT]={0};
uint16_t tkey_base[COL_COUNT]={0};

/* ======================== 内联 TouchKey 函数 ======================== */
static void TKey_ChSampInit(void)
{
    R8_ADC_CFG = RB_ADC_POWER_ON | RB_ADC_BUF_EN | (1<<4);
    R8_TKEY_CFG |= RB_TKEY_PWR_ON;
}

static uint16_t TKey_Convert(uint8_t charg, uint8_t disch)
{
    R8_TKEY_COUNT = (disch<<5) | (charg&0x1f);
    R8_TKEY_CONVERT = RB_TKEY_START;
    while(R8_TKEY_CONVERT & RB_TKEY_START);
    return (R16_ADC_DATA & RB_ADC_DATA);
}

/* ======================== 初始化 ======================== */
void TKey_Init(void)
{
    uint8_t i;
    GPIOAGPPCfg(ENABLE, RB_PIN_ADC0_IE);
    GPIOAGPPCfg(ENABLE, RB_PIN_ADC1_IE);
    GPIOAGPPCfg(ENABLE, RB_PIN_ADC2_3_IE);
    GPIOAGPPCfg(ENABLE, RB_PIN_ADC4_5_IE);
    GPIOAGPPCfg(ENABLE, RB_PIN_ADC6_7_IE);
    TKey_ChSampInit();
    for(i=0;i<COL_COUNT;i++){
        R8_ADC_CHANNEL = TKC[i];
        DelayUs(20);
        tkey_base[i] = TKey_Convert(8,1);
        DelayUs(5);
        tkey_base[i] = (tkey_base[i] + TKey_Convert(8,1)) >> 1;
    }
}

void GPIOInit(void)
{
    GPIOA_ModeCfg(R1_PIN|R2_PIN|R3_PIN|R4_PIN|R5_PIN|R6_PIN|R7_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(COL_ALL, GPIO_ModeIN_Floating);
    GPIOA_SetBits(R1_PIN|R2_PIN|R3_PIN|R4_PIN|R5_PIN|R6_PIN|R7_PIN);
}

/* ======================== 矩阵扫描 ======================== */
void MatrixScan(void)
{
    uint8_t r,c,idx;
    uint16_t val,diff;
    for(r=0;r<ROW_COUNT;r++){
        if(r<7) GPIOA_ResetBits(row_pin[r]);
        else    GPIOB_ResetBits(R8_PIN);
        DelayUs(3);
        for(c=0;c<COL_COUNT;c++){
            R8_ADC_CHANNEL = TKC[c];
            DelayUs(2);
            val = TKey_Convert(8,1);
            diff = (val>tkey_base[c])?(val-tkey_base[c]):0;
            idx = r*COL_COUNT+c;
            if(idx>=KEY_COUNT) continue;
            if(r<4){ if(diff>80) key_state[idx]=1; }
            else if(c>=2){ if(diff>80) key_state[idx]=1; }
        }
        if(r<7) GPIOA_SetBits(row_pin[r]);
        else    GPIOB_SetBits(R8_PIN);
    }
}

/* ======================== HID 报告 ======================== */
void SendHIDReport(void)
{
    uint8_t i,idx,r,c,kc,mod=0,ri=2;
    for(i=0;i<8;i++) HIDKeyBuff[i]=0;
    if(key_state[FN_R1*COL_COUNT+FN_C1] || key_state[FN_R2*COL_COUNT+FN_C2])
        fn_pressed=1;
    for(idx=0;idx<KEY_COUNT && ri<8;idx++){
        if(!key_state[idx]) continue;
        r=KeyMap[idx][0]; c=KeyMap[idx][1];
        if((r==FN_R1&&c==FN_C1)||(r==FN_R2&&c==FN_C2)) continue;
        kc = fn_pressed?FnTable[r][c]:KeyTable[r][c];
        if(fn_pressed && kc==0) kc=KeyTable[r][c];
        if(kc==0xE0) mod|=0x01;
        else if(kc==0xE1) mod|=0x02;
        else if(kc==0xE2) mod|=0x04;
        else if(kc==0xE3) mod|=0x08;
        else HIDKeyBuff[ri++]=kc;
    }
    HIDKeyBuff[0]=mod;

    /* 直接写入 EP1_Databuf (IN 缓冲区在后 64 字节) */
    EP1_Databuf[64] = HIDKeyBuff[0];
    EP1_Databuf[65] = HIDKeyBuff[1];
    EP1_Databuf[66] = HIDKeyBuff[2];
    EP1_Databuf[67] = HIDKeyBuff[3];
    EP1_Databuf[68] = HIDKeyBuff[4];
    EP1_Databuf[69] = HIDKeyBuff[5];
    EP1_Databuf[70] = HIDKeyBuff[6];
    EP1_Databuf[71] = HIDKeyBuff[7];

    /* EP1 IN 发送 */
    R8_UEP1_T_LEN = 8;
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

/* ======================== 主函数 ======================== */
int main(void)
{
    uint8_t i;
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    GPIOInit();
    TKey_Init();

    /* USB 初始化 - 直接操作寄存器, 不依赖 usbdev.c */
    R8_USB_CTRL = 0x00;
    R8_UEP4_1_MOD = RB_UEP4_RX_EN | RB_UEP4_TX_EN | RB_UEP1_RX_EN | RB_UEP1_TX_EN;
    R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN | RB_UEP3_RX_EN | RB_UEP3_TX_EN;
    R16_UEP0_DMA = (uint16_t)(uint32_t)EP0_Databuf;
    R16_UEP1_DMA = (uint16_t)(uint32_t)EP1_Databuf;
    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_USB_DEV_AD = 0x00;
    R8_USB_CTRL = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R16_PIN_ANALOG_IE |= RB_PIN_USB_IE | RB_PIN_USB_DP_PU;
    R8_USB_INT_FG = 0xFF;
    R8_UDEV_CTRL = RB_UD_PD_DIS | RB_UD_PORT_EN;
    R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;

    PFIC_EnableIRQ(USB_IRQn);
    DelayMs(100);

    while(1){
        for(i=0;i<KEY_COUNT;i++){ key_prev[i]=key_state[i]; key_state[i]=0; }
        fn_pressed=0;
        MatrixScan();
        for(i=0;i<KEY_COUNT;i++) if(key_state[i]!=key_prev[i]) break;
        if(i<KEY_COUNT) SendHIDReport();
        DelayMs(5);
    }
}

/* ======================== USB 中断 ======================== */
void USB_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USB_IRQHandler(void)
{
    uint8_t f = R8_USB_INT_FG;
    if(f & RB_UIE_SUSPEND)  {}
    if(f & RB_UIE_TRANSFER) {}
    if(f & RB_UIE_BUS_RST) {
        R8_USB_DEV_AD = 0;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        R8_USB_INT_FG = RB_UIE_BUS_RST;
    }
}
