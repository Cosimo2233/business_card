/* CH582M capacitive PCB keyboard, schematic dated 2026-09-11.
 * Self-capacitance row/column sensing. See README for ambiguity limitations.
 */
#include "CH58x_common.h"
#include "keyboard_logic.h"
#include "usb_keyboard.h"

#define TOUCH_CHANNELS 15
#define CALIB_SAMPLES 32
#define PRESS_DELTA 80
#define RELEASE_DELTA 45
#define R8_PRESS_US 8
#define R8_RELEASE_US 4
#define DEBOUNCE_FRAMES 3
#define SCAN_TICKS (60000u * 5u)
#define TOUCH_TIMEOUT_TICKS 60000u
#define R8_TIMEOUT_US 500
#define TOUCH_PINS (GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15)

/* Columns C1..7 then rows R1..7. R8 is PB8 with PB9 auxiliary. */
static const uint8_t touch_channel[14]={0,1,2,3,4,5,6,10,9,8,7,11,12,13};
/* Visible in debugger; channel 14 is R8 discharge time in microseconds.
 * Other raw/base values are ADC counts. Deltas are signed raw minus base. */
volatile uint16_t touch_raw[TOUCH_CHANNELS],touch_base[TOUCH_CHANNELS];
volatile int16_t touch_delta[TOUCH_CHANNELS];
volatile uint32_t touch_faults;
volatile uint8_t touch_ready;
static int32_t baseline_q8[TOUCH_CHANNELS];
static uint8_t electrode_state[TOUCH_CHANNELS],debounce[TOUCH_CHANNELS];
static uint8_t key_state[ROW_CNT][COL_CNT];

static uint8_t TK_Read(uint8_t ch,uint16_t *value)
{
    uint32_t start;
    R8_ADC_CHANNEL=ch;
    DelayUs(2);
    R8_TKEY_COUNT=(1u<<5)|8u;
    R8_TKEY_CONVERT=RB_TKEY_START;
    start=SYS_GetSysTickCnt();
    while(R8_TKEY_CONVERT & RB_TKEY_START) {
        if((uint32_t)(SYS_GetSysTickCnt()-start)>TOUCH_TIMEOUT_TICKS) {
            R8_TKEY_CONVERT=0;
            touch_faults++;
            return 0;
        }
    }
    *value=R16_ADC_DATA & RB_ADC_DATA;
    return 1;
}

static uint8_t R8_Read(uint16_t *value)
{
    uint32_t start,elapsed;
    /* PB9 AUX stays low. Charge PB8 directly, then release it to discharge
     * through R7/R8. This avoids the VDD/2 ceiling of charging via R7. */
    GPIOB_SetBits(GPIO_Pin_8);
    GPIOB_ModeCfg(GPIO_Pin_8,GPIO_ModeOut_PP_5mA);
    DelayUs(10);
    GPIOB_ModeCfg(GPIO_Pin_8,GPIO_ModeIN_Floating);
    start=SYS_GetSysTickCnt();
    while(GPIOB_ReadPortPin(GPIO_Pin_8)) {
        elapsed=(uint32_t)(SYS_GetSysTickCnt()-start);
        if(elapsed>=R8_TIMEOUT_US*60u) {
            touch_faults++;
            return 0;
        }
    }
    *value=(uint32_t)(SYS_GetSysTickCnt()-start)/60u;
    return 1;
}

static uint8_t ReadElectrodes(void)
{
    uint8_t i,j,ok=1;
    uint16_t value=0;
    uint32_t sum;
    for(i=0;i<14;i++) {
        /* Discard the first conversion after switching the ADC channel. */
        if(!TK_Read(touch_channel[i],&value)) { ok=0; continue; }
        sum=0;
        for(j=0;j<4;j++) {
            if(!TK_Read(touch_channel[i],&value)) ok=0;
            sum+=value;
            USB_Poll();
        }
        touch_raw[i]=sum/4;
    }
    USB_Poll();
    if(!R8_Read(&value)) ok=0;
    touch_raw[14]=value;
    USB_Poll();
    return ok;
}

static void Touch_Init(void)
{
    GPIOA_ModeCfg(TOUCH_PINS,GPIO_ModeIN_Floating);
    GPIOAGPPCfg(ENABLE,RB_PIN_ADC0_IE|RB_PIN_ADC1_IE|RB_PIN_ADC2_3_IE|
        RB_PIN_ADC4_5_IE|RB_PIN_ADC6_7_IE|RB_PIN_ADC8_9_IE|RB_PIN_ADC10_IE|
        RB_PIN_ADC11_IE|RB_PIN_ADC12_IE|RB_PIN_ADC13_IE);
    GPIOB_ResetBits(GPIO_Pin_9);
    GPIOB_ModeCfg(GPIO_Pin_9,GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(GPIO_Pin_8,GPIO_ModeIN_Floating);
    TouchKey_ChSampInit();
}

static void Touch_Calibrate(void)
{
    uint8_t i,n;
    uint32_t sums[TOUCH_CHANNELS]={0};
    touch_ready=0;
    memset(electrode_state,0,sizeof(electrode_state));
    memset(debounce,0,sizeof(debounce));
    memset(key_state,0,sizeof(key_state));
    memset(current_report,0,sizeof(current_report));
    for(n=0;n<CALIB_SAMPLES;n++) {
        if(!ReadElectrodes()) return;
        for(i=0;i<TOUCH_CHANNELS;i++) sums[i]+=touch_raw[i];
    }
    for(i=0;i<TOUCH_CHANNELS;i++) {
        touch_base[i]=sums[i]/CALIB_SAMPLES;
        baseline_q8[i]=(int32_t)touch_base[i]*256;
        touch_delta[i]=0;
    }
    touch_ready=1;
}

static void MatrixScan(void)
{
    uint8_t i,rows=0,cols=0,next[ROW_CNT][COL_CNT];
    if(!ReadElectrodes()) {
        /* Sensor failure must release keys, never turn a timeout into a touch. */
        memset(key_state,0,sizeof(key_state));
        memset(electrode_state,0,sizeof(electrode_state));
        memset(debounce,0,sizeof(debounce));
        memset(current_report,0,sizeof(current_report));
        return;
    }
    for(i=0;i<TOUCH_CHANNELS;i++) {
        int32_t delta=(int32_t)touch_raw[i]-(baseline_q8[i]>>8);
        uint16_t magnitude=i==14 ? (delta>0 ? delta : 0) : (delta<0 ? -delta : delta);
        uint16_t on=i==14 ? R8_PRESS_US : PRESS_DELTA;
        uint16_t off=i==14 ? R8_RELEASE_US : RELEASE_DELTA;
        uint8_t candidate=magnitude>=(electrode_state[i] ? off : on);
        touch_delta[i]=delta;
        if(candidate!=electrode_state[i]) {
            if(++debounce[i]>=DEBOUNCE_FRAMES) { electrode_state[i]=candidate; debounce[i]=0; }
        } else debounce[i]=0;
        /* Follow slow drift only well below release threshold. Fixed-point
         * accumulator avoids rounding away small baseline corrections. */
        if(!candidate && !electrode_state[i] && magnitude<off/2)
            baseline_q8[i]+=(((int32_t)touch_raw[i]*256)-baseline_q8[i])/128;
        touch_base[i]=baseline_q8[i]>>8;
        if(electrode_state[i]) {
            if(i<7) cols|=1u<<i;
            else rows|=1u<<(i-7);
        }
    }
    ResolveTouches(rows,cols,key_state,next);
    memcpy(key_state,next,sizeof(key_state));
    BuildReport(key_state,current_report);
}

int main(void)
{
    uint32_t last_scan,last_retry;
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    /* Free running 60 MHz counter, no SysTick interrupt. */
    SysTick->CTLR=0;
    SysTick->CNT=0;
    SysTick->CMP=~(uint64_t)0;
    SysTick->CTLR=SysTick_CTLR_STCLK|SysTick_CTLR_STE;
    Touch_Init();
    USB_Init();
    /* Settle after power-up while continuing to answer enumeration. */
    last_scan=SYS_GetSysTickCnt();
    while((uint32_t)(SYS_GetSysTickCnt()-last_scan)<60000u*300u) USB_Poll();
    Touch_Calibrate();
    last_scan=last_retry=SYS_GetSysTickCnt();
    while(1) {
        USB_Poll();
        if((uint32_t)(SYS_GetSysTickCnt()-last_scan)>=SCAN_TICKS) {
            last_scan=SYS_GetSysTickCnt();
            if(touch_ready) MatrixScan();
            else if((uint32_t)(last_scan-last_retry)>=60000000u) {
                last_retry=last_scan;
                Touch_Calibrate();
            }
        }
        USB_SendReport();
    }
}
