#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "CH583SFR.h"
#include "keyboard_logic.h"
/* Use the SDK's actual bit definitions with RAM-backed registers. */
#undef R8_UEP0_CTRL
#undef R8_UEP1_CTRL
#undef R8_UEP0_T_LEN
#undef R8_UEP1_T_LEN
#undef R8_USB_DEV_AD
#undef R8_USB_INT_FG
#undef R8_USB_INT_ST
#undef R8_USB_RX_LEN
#undef R8_USB_MIS_ST
#undef R8_USB_CTRL
#undef R8_UEP4_1_MOD
#undef R8_UEP2_3_MOD
#undef R8_USB_INT_EN
#undef R8_UDEV_CTRL
#undef R16_UEP0_DMA
#undef R16_UEP1_DMA
#undef R16_PIN_ANALOG_IE
static uint8_t R8_UEP0_CTRL,R8_UEP1_CTRL,R8_UEP0_T_LEN,R8_UEP1_T_LEN;
static uint8_t R8_USB_DEV_AD,R8_USB_INT_FG,R8_USB_INT_ST,R8_USB_RX_LEN;
static uint8_t R8_USB_MIS_ST,R8_USB_CTRL,R8_UEP4_1_MOD,R8_UEP2_3_MOD;
static uint8_t R8_USB_INT_EN,R8_UDEV_CTRL;
static uint16_t R16_UEP0_DMA,R16_UEP1_DMA,R16_PIN_ANALOG_IE;
static uint32_t ticks;
static uint32_t SYS_GetSysTickCnt(void) { return ticks; }
#define PFIC_DisableIRQ(x) ((void)0)
#include "usb_keyboard.h"

static void event(uint8_t st,uint8_t length)
{
    R8_USB_INT_FG=RB_UIF_TRANSFER; R8_USB_INT_ST=st; R8_USB_RX_LEN=length;
    USB_Poll(); R8_USB_INT_FG=0; /* Simulate write-one-to-clear hardware. */
}
static void setup(uint8_t type,uint8_t request,uint16_t value,uint16_t index,uint16_t length)
{
    uint8_t packet[]={type,request,value,value>>8,index,index>>8,length,length>>8};
    memcpy(EP0_Databuf,packet,8); event(RB_UIS_SETUP_ACT,8);
}
static void mapping_tests(void)
{
    uint8_t held[8][7]={{0}},out[8][7],report[8];
    const uint8_t right[4][5]={{0x18,0x0c,0x12,0x13,0x2a},
        {0x0d,0x0e,0x0f,0x33,0x28},{0x10,0x36,0x37,0x52,0x38},
        {0x2c,0,0x50,0x51,0x4f}};
    unsigned r,c,n=0;
    for(r=0;r<8;r++) for(c=0;c<7;c++) {
        memset(held,0,sizeof(held));
        ResolveTouches(1u<<r,1u<<c,held,out);
        if(KeyValid(r,c)) { assert(out[r][c]); n++; }
        else assert(!out[r][c]);
    }
    assert(n==48);
    for(r=0;r<4;r++) for(c=0;c<5;c++) {
        memset(held,0,sizeof(held)); held[r+4][6-c]=1;
        BuildReport(held,report); assert(report[2]==right[r][c]);
    }
    memset(held,0,sizeof(held)); held[7][5]=1; held[4][6]=1;
    BuildReport(held,report); assert(report[2]==0x24); /* Fn+U=7 */
    held[7][5]=0; held[3][3]=1;
    BuildReport(held,report); assert(report[2]==0x24);
    memset(held,0,sizeof(held)); held[3][4]=held[3][5]=held[7][6]=1;
    BuildReport(held,report); assert(report[2]==0x2c && report[3]==0);
    memset(held,0,sizeof(held));
    ResolveTouches(3,3,held,out); assert(!memcmp(held,out,sizeof(held)));
    held[2][0]=1;
    ResolveTouches((1<<2)|1,3,held,out);
    assert(out[2][0] && out[0][1] && !out[2][1] && !out[0][0]);
    BuildReport(out,report); assert(report[0]==2 && report[2]==0x14);
    ResolveTouches(0,0,out,held); BuildReport(held,report);
    for(c=0;c<8;c++) assert(report[c]==0);
    memset(held,1,sizeof(held)); BuildReport(held,report);
    assert(report[0]==15); for(c=2;c<8;c++) assert(report[c]==1);
}
static void usb_tests(void)
{
    uint8_t saved[8],big[64]={0};
    USB_Init(); assert(R8_UEP4_1_MOD==RB_UEP1_TX_EN && R8_UEP2_3_MOD==0);
    assert(sizeof(MyCfgDescr)==MyCfgDescr[2]);
    assert(sizeof(KeyboardReportDesc)==MyCfgDescr[25]);
    setup(0x80,6,0x100,0,8); assert(R8_UEP0_T_LEN==8 && EP0_Databuf[7]==64);
    event(UIS_TOKEN_IN,0); assert(ctrl_state==CTRL_STATUS_OUT);
    event(UIS_TOKEN_OUT|RB_UIS_TOG_OK,0); assert(ctrl_state==CTRL_IDLE);
    setup(0,5,7,0,0); assert(R8_USB_DEV_AD==0);
    event(UIS_TOKEN_IN,0); assert(R8_USB_DEV_AD==7);
    setup(0x80,6,0x200,0,255); assert(R8_UEP0_T_LEN==34);
    setup(0x81,6,0x2200,0,255); assert(R8_UEP0_T_LEN==sizeof(KeyboardReportDesc));
    setup(0x80,6,0x600,0,10); assert((R8_UEP0_CTRL & MASK_UEP_T_RES)==UEP_T_RES_STALL);
    setup(0,9,1,0,0); event(UIS_TOKEN_IN,0); assert(usb_config==1);
    setup(0x21,9,0x200,0,1); assert(ctrl_state==CTRL_LED_OUT);
    EP0_Databuf[0]=2; event(UIS_TOKEN_OUT|RB_UIS_TOG_OK,1); assert(hid_leds==2);
    event(UIS_TOKEN_IN,0); assert(ctrl_state==CTRL_IDLE);
    setup(0xa1,1,0x200,0,1); assert(EP0_Databuf[0]==2);
    setup(0x21,11,0,0,0); assert(hid_protocol==0);
    setup(0xa1,3,0,0,1); assert(EP0_Databuf[0]==0);
    memset(current_report,0,8); current_report[2]=4;
    USB_SendReport(); assert(ep1_busy && EP1_Databuf[2]==4);
    memcpy(saved,EP1_Databuf,8);
    memset(current_report,0,8); USB_SendReport(); assert(!memcmp(saved,EP1_Databuf,8));
    event(UIS_TOKEN_IN|1,0); assert(!ep1_busy && last_report[2]==4);
    USB_SendReport(); assert(ep1_busy && EP1_Databuf[2]==0); /* release not lost */
    event(UIS_TOKEN_IN|1,0); USB_SendReport(); assert(!ep1_busy);
    setup(0x21,10,1<<8,0,0); event(UIS_TOKEN_IN,0);
    ticks+=240000; USB_SendReport(); assert(ep1_busy);
    event(UIS_TOKEN_IN|1,0);
    setup(2,3,0,0x81,0); assert(ep1_halted);
    USB_SendReport(); assert(!ep1_busy);
    setup(2,1,0,0x81,0); assert(!ep1_halted && !(R8_UEP1_CTRL & RB_UEP_T_TOG));
    R8_USB_INT_FG=RB_UIF_SUSPEND; R8_USB_MIS_ST=RB_UMS_SUSPEND; USB_Poll();
    USB_SendReport(); assert(!ep1_busy);
    R8_USB_INT_FG=RB_UIF_SUSPEND; R8_USB_MIS_ST=0; USB_Poll();
    USB_SendReport(); assert(ep1_busy);
    R8_USB_INT_FG=RB_UIF_BUS_RST; USB_Poll(); assert(!usb_config && !ep1_busy);
    /* Control endpoint full packet followed by short termination ZLP. */
    R8_UEP0_CTRL=RB_UEP_T_TOG|RB_UEP_R_TOG|UEP_R_RES_ACK|UEP_T_RES_NAK;
    EP0_Reply(big,64,128); assert(R8_UEP0_T_LEN==64);
    event(UIS_TOKEN_IN,0); assert(R8_UEP0_T_LEN==0 && ctrl_state==CTRL_IN);
    event(UIS_TOKEN_IN,0); assert(ctrl_state==CTRL_STATUS_OUT);
    setup(0xa1,3,0,1,1); assert((R8_UEP0_CTRL & MASK_UEP_T_RES)==UEP_T_RES_STALL);
}
int main(void)
{
    mapping_tests(); usb_tests();
    puts("PASS: 48 intersections, layout/Fn/modifiers/ghost suppression, USB enumeration/control/IN/release/idle/reset/suspend");
    return 0;
}
