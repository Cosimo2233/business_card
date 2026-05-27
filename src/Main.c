/*******************************************************************************
 * CH582M 电容触摸键盘固件
 * 48键 USB HID 键盘
 * 矩阵: 左边4行(R1~R4)x7列(C1~C7) + 右边4行(R5~R8)x5列(C3~C7)
 * 检测: CH582M 内置 TouchKey (列 TKEY0~6, 行 TKEY7~13)
 *       R8行 (Space/Fn/←/↓/→) 用 PB8+PB9 RC充放电检测
 ******************************************************************************/
#include "CH58x_common.h"

/* ======================== USB 描述符 ======================== */
/* USB HID 键盘报告描述符 - 8字节: [修饰键][保留][按键0]...[按键5] */
const uint8_t KeyboardReportDesc[] = 
{
    0x05,0x01,0x09,0x06,0xA1,0x01,
    0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,
    0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,
    0x95,0x01,0x75,0x03,0x91,0x01,
    0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,0x19,0x00,
    0x29,0x65,0x81,0x00,0xC0
};

/* USB DMA 缓冲区 (需 4 字节对齐) */
__attribute__((aligned(4))) uint8_t EP0_Databuf[192], EP1_Databuf[128];
__attribute__((aligned(4))) uint8_t HIDKeyBuff[8], HIDKeyBuffPrev[8];

/* USB 设备描述符 */
const uint8_t MyDevDescr[] = {0x12,0x01,0x10,0x01,0x00,0x00,0x00,0x08,
    0x86,0x1A,0x24,0x55,0x00,0x01,0x01,0x02,0x00,0x01};
/* USB 配置描述符 (含接口/HID/端点描述符) */
const uint8_t MyCfgDescr[] = {
    0x09,0x02,0x3B,0x00,0x01,0x01,0x00,0x80,0x32,
    0x09,0x04,0x00,0x00,0x01,0x03,0x01,0x01,0x00,
    0x09,0x21,0x10,0x01,0x00,0x01,0x22,0x45,0x00,
    0x07,0x05,0x81,0x03,0x08,0x00,0x0A};

/* ======================== 矩阵定义 ======================== */
#define COL_CNT 7
#define ROW_CNT 8
#define KEY_CNT 48

/* 列引脚 (TouchKey 通道 TKEY0~6) */
const uint32_t col_pin[COL_CNT] = {GPIO_Pin_4,GPIO_Pin_5,GPIO_Pin_12,GPIO_Pin_13,
                                   GPIO_Pin_14,GPIO_Pin_15,GPIO_Pin_3};
/* 行引脚 (TouchKey 通道 TKEY7~13, 不含 PB8/PB9) */
const uint32_t row_pin[ROW_CNT-1] = {GPIO_Pin_6,GPIO_Pin_0,GPIO_Pin_1,GPIO_Pin_2,
                                     GPIO_Pin_7,GPIO_Pin_8,GPIO_Pin_9};
/* 行对应的 TouchKey 通道号 */
const uint8_t row_ch[ROW_CNT-1] = {10,9,8,7,11,12,13};

/* ======================== 键值表 ======================== */
/* HID Usage ID 键值表 [行][列] */
const uint8_t KTab[ROW_CNT][COL_CNT]=
{
    {0x29,0x14,0x1A,0x08,0x15,0x17,0x1C}, /* R1: Esc Q W E R T Y */
    {0x2B,0x04,0x16,0x07,0x09,0x0A,0x0B}, /* R2: Tab A S D F G H */
    {0xE1,0x1D,0x1B,0x06,0x19,0x05,0x11}, /* R3: LShift Z X C V B N */
    {0xE0,0xE3,0xE2,0x00,0x2C,0x2C,0x2C}, /* R4: Ctrl Win Alt Fn Space Space Space */
    {0x18,0x0C,0x12,0x13,0x2A,0x00,0x00}, /* R5: U I O P Bksp */
    {0x0D,0x0E,0x0F,0x33,0x28,0x00,0x00}, /* R6: J K L ; Enter */
    {0x10,0x36,0x37,0x52,0x38,0x00,0x00}, /* R7: M , . Up / */
    {0x2C,0x00,0x50,0x51,0x4F,0x00,0x00}  /* R8: Space Fn Left Down Right */
};
/* Fn层键值表: Fn+Q/W/E/R/T/Y/U/I/O/P = 1/2/3/4/5/6/7/8/9/0 */
const uint8_t FTab[ROW_CNT][COL_CNT]=
{
    {0x00,0x1E,0x1F,0x20,0x21,0x22,0x23},
    {0},{0},{0},
    {0x24,0x25,0x26,0x27,0x00,0,0},{0},{0},{0}
};

/* 按键索引 [0~47] → 矩阵坐标 [行, 列] */
const uint8_t KMap[KEY_CNT][2]=
{
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
uint16_t tkey_base[ROW_CNT][COL_CNT]={0};  /* 各交叉点 TouchKey 基准值 */

/* ---------- 可调参数 ---------- */
#define TK_THRESH   80   /* TouchKey 检测阈值: ADC 差值超过此值判定为触摸 */
#define R8_THRESH   20   /* R8 行充电计时阈值: 计时超过此值判定为触摸 */
#define R8_TIMEOUT  5000 /* R8 行充电计时上限 (防卡死) */

/* ======================== TouchKey 底层函数 ======================== */
/**
 * TK_InitHW() - 初始化 TouchKey 硬件模块
 * 配置 ADC 和 TouchKey 寄存器, 启用所有 14 个 TouchKey 通道的模拟功能
 * 调用时机: TK_Calib() 中调用, 只需调用一次
 */
static void TK_InitHW(void)
{
    R8_ADC_CFG = RB_ADC_POWER_ON | RB_ADC_BUF_EN | (1<<4);
    R8_TKEY_CFG |= RB_TKEY_PWR_ON;

    GPIOAGPPCfg(ENABLE, RB_PIN_ADC0_IE|RB_PIN_ADC1_IE|RB_PIN_ADC2_3_IE|
                        RB_PIN_ADC4_5_IE|RB_PIN_ADC6_7_IE|RB_PIN_ADC8_9_IE|
                        RB_PIN_ADC10_IE|RB_PIN_ADC11_IE|
                        RB_PIN_ADC12_IE|RB_PIN_ADC13_IE);
}

/**
 * TK_Read(ch, cg, dcg) - 读取指定 TouchKey 通道的 ADC 值
 * @param ch   TouchKey 通道号 (0~13, 对应 CH_EXTIN_0~13)
 * @param cg   充电时间参数 (0~31)
 * @param dcg  放电时间参数 (0~7)
 * @return     12 位 ADC 转换值 (0~4095)
 * 使用方式: TK_Read(0,8,1) = 读通道0, 充电时间8, 放电时间1
 */
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
/**
 * TK_Calib() - TouchKey 校准
 * 初始化 TouchKey 硬件, 遍历所有交叉点 (行x列) 获取 ADC 基准值
 * 每个点采样 2 次取平均作为基准
 * 调用时机: main() 开始时调用一次
 */
void TK_Calib(void)
{
    uint8_t r,c;
    TK_InitHW();
    for(r=0;r<ROW_CNT-1;r++)
    {
        TK_Read(row_ch[r],8,1);  /* 预热行通道 */
        DelayUs(5);
        for(c=0;c<COL_CNT;c++)
        {
            tkey_base[r][c] = TK_Read(c,8,1);
            DelayUs(2);
            tkey_base[r][c] = (tkey_base[r][c]+TK_Read(c,8,1))>>1;
        }
    }
}

/* ======================== R8 行检测 ======================== */
/**
 * R8_Init() - 初始化 R8 检测引脚
 * PB8 配置为推挽输出, 初始低电平
 * 调用时机: main() 开始时调用一次
 */
void R8_Init(void)
{
    GPIOB_ModeCfg(GPIO_Pin_8, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(GPIO_Pin_8);
}

/**
 * R8_Detect() - 检测 R8 行是否有触摸
 * 原理: PB8 输出高 → 经 1MΩ 给触摸焊盘充电 → PB9 读电平
 *       触摸时人体电容并联, 充电变慢, 计时值变大
 * @return 1=有触摸, 0=无触摸
 * 调用时机: MatrixScan() 中每轮扫描调用一次
 */
uint8_t R8_Detect(void)
{
    uint16_t t;
    GPIOB_SetBits(GPIO_Pin_8);
    for(t=0; t<R8_TIMEOUT; t++)
    {
        if(GPIOB_ReadPortPin(GPIO_Pin_9)) break;
    }
    GPIOB_ResetBits(GPIO_Pin_8);
    { 
        volatile uint16_t d; for(d=0;d<1000;d++); 
    }  /* 放电等待 */
    return (t>R8_THRESH) ? 1 : 0;
}

/* ======================== 矩阵扫描 ======================== */
/**
 * MatrixScan() - 逐行扫描矩阵, 检测触摸按键
 * R0~R6: 拉低当前行 → 读各列 TouchKey ADC 值 → 与基准值比较
 * R7 (R8): 使用 RC 充放电法检测 PB8+PB9
 * 检测到的按键设置在 key_state[] 中
 * 调用时机: main() 主循环中每轮调用一次
 */
void MatrixScan(void)
{
    uint8_t r,c,idx;
    uint16_t v,d;

    for(r=0;r<ROW_CNT-1;r++)
    {
        R32_PA_CLR = row_pin[r];
        DelayUs(3);
        for(c=0;c<COL_CNT;c++)
        {
            v = TK_Read(c,8,1);
            d = (v>tkey_base[r][c]) ? (v-tkey_base[r][c]) : 0;
            idx = r*COL_CNT+c;
            if(idx>=KEY_CNT) continue;
            if((r<4 || c>=2) && d>TK_THRESH) key_state[idx]=1;
        }
        R32_PA_OUT |= row_pin[r];
    }

    /* R8 行 */
    if(R8_Detect())
    {
        for(c=2;c<COL_CNT;c++)
        {
            v = TK_Read(c,8,1);
            d = (v>tkey_base[7][c]) ? (v-tkey_base[7][c]) : 0;
            idx = 7*COL_CNT+c;
            if(d>TK_THRESH) key_state[idx]=1;
        }
    }
}

/* ======================== HID 报告发送 ======================== */
/**
 * SendReport() - 发送 USB HID 键盘报告
 * 遍历 key_state[] → 映射 HID Usage ID → 打包 8 字节报告 → 通过 EP1 IN 发送
 * 先去重: 和上一包完全相同则不发送
 * 调用时机: main() 主循环中检测到按键变化时调用
 */
void SendReport(void)
{
    uint8_t i,idx,r,c,k,mod=0,ri=2;
    for(i=0;i<8;i++) HIDKeyBuff[i]=0;

    if(key_state[3*COL_CNT+3] || key_state[7*COL_CNT+1]) fn_pressed=1;

    for(idx=0;idx<KEY_CNT && ri<8;idx++){
        if(!key_state[idx]) continue;
        r=KMap[idx][0]; c=KMap[idx][1];
        if((r==3&&c==3)||(r==7&&c==1)) continue;  /* 跳过 Fn 键本身 */
        k = fn_pressed ? FTab[r][c] : KTab[r][c];
        if(fn_pressed && k==0) k=KTab[r][c];
        if(k==0xE0)      mod|=0x01;  /* Ctrl 左 */
        else if(k==0xE1) mod|=0x02;  /* Shift 左 */
        else if(k==0xE2) mod|=0x04;  /* Alt 左 */
        else if(k==0xE3) mod|=0x08;  /* Win 左 */
        else HIDKeyBuff[ri++]=k;
    }
    HIDKeyBuff[0]=mod;

    /* 去重 */
    for(i=0;i<8;i++) if(HIDKeyBuff[i]!=HIDKeyBuffPrev[i]) break;
    if(i==8) return;
    for(i=0;i<8;i++) HIDKeyBuffPrev[i]=HIDKeyBuff[i];

    /* 写入 EP1 IN 缓冲区并触发发送 */
    for(i=0;i<8;i++) EP1_Databuf[64+i]=HIDKeyBuff[i];
    R8_UEP1_T_LEN=8;
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

/* ======================== 主函数 ======================== */
/**
 * main() - 程序入口
 * 1. 系统时钟 60MHz
 * 2. TouchKey 校准
 * 3. R8 引脚初始化
 * 4. USB 设备初始化 (直接操作寄存器)
 * 5. 主循环: 扫描矩阵 → 检测变化 → 发送 HID 报告
 */
int main(void)
{
    uint8_t i;
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    TK_Calib();
    R8_Init();

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

    while(1)
    {
        for(i=0;i<KEY_CNT;i++){ key_prev[i]=key_state[i]; key_state[i]=0; }
        fn_pressed=0;
        MatrixScan();
        for(i=0;i<KEY_CNT;i++) if(key_state[i]!=key_prev[i]) break;
        if(i<KEY_CNT) SendReport();
        DelayMs(5);
    }
}

/* ======================== USB 中断处理 ======================== */
/**
 * USB_IRQHandler() - USB 中断服务函数
 * 处理总线复位和 USB 传输事件
 * 注意: 完整的 USB 枚举处理由 MRS 库 libISP583.a 自动完成
 */
void USB_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USB_IRQHandler(void)
{
    uint8_t f=R8_USB_INT_FG;
    if(f&RB_UIE_SUSPEND) {}
    if(f&RB_UIE_TRANSFER) {}
    if(f&RB_UIE_BUS_RST){
        R8_USB_DEV_AD=0;
        R8_UEP0_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK;
        R8_UEP1_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK|RB_UEP_AUTO_TOG;
        R8_USB_INT_FG=RB_UIE_BUS_RST;
    }
}
