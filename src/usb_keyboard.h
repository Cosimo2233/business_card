#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H
/* CH58x USB register sequencing follows WCH CompoundDev example.
 * Only EP0 control and EP1 IN are enabled. No ISP library handles enumeration. */
#define EP0_SIZE 64
static const uint8_t KeyboardReportDesc[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,
    0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
    0x95,1,0x75,8,0x81,1,
    0x95,5,0x75,1,0x05,8,0x19,1,0x29,5,0x91,2,
    0x95,1,0x75,3,0x91,1,
    0x95,6,0x75,8,0x15,0,0x25,0x65,0x05,7,0x19,0,0x29,0x65,0x81,0,0xc0
};
static const uint8_t MyDevDescr[] = {
    18,1,0x10,1,0,0,0,EP0_SIZE,0x86,0x1a,0x24,0x55,0,1,0,0,0,1
};
static const uint8_t MyCfgDescr[] = {
    9,2,34,0,1,1,0,0x80,50,
    9,4,0,0,1,3,1,1,0,
    9,0x21,0x11,1,0,1,0x22,sizeof(KeyboardReportDesc),0,
    7,5,0x81,3,8,0,10
};
static const uint8_t LangDescr[]={4,3,9,4};
__attribute__((aligned(4))) static uint8_t EP0_Databuf[EP0_SIZE];
/* IN-only, single buffer: IN data starts at offset zero, not +64. */
__attribute__((aligned(4))) static uint8_t EP1_Databuf[64];
static uint8_t usb_config,usb_suspended,ep1_busy,ep1_halted;
static uint8_t hid_idle,hid_protocol=1,hid_leds;
static uint8_t current_report[8],last_report[8],report_sent;
static uint32_t last_report_tick;
static const uint8_t *ctrl_data;
static uint16_t ctrl_remaining;
static uint8_t ctrl_reply[8],ctrl_zlp,pending_address;
enum { CTRL_IDLE, CTRL_IN, CTRL_STATUS_OUT, CTRL_STATUS_IN, CTRL_LED_OUT };
static uint8_t ctrl_state;

static void EP0_Stall(void)
{
    ctrl_state=CTRL_IDLE;
    pending_address=0xff;
    R8_UEP0_CTRL=RB_UEP_R_TOG|RB_UEP_T_TOG|UEP_R_RES_STALL|UEP_T_RES_STALL;
}
static void EP0_Packet(void)
{
    uint8_t n=ctrl_remaining>EP0_SIZE ? EP0_SIZE : ctrl_remaining;
    if(n) { memcpy(EP0_Databuf,ctrl_data,n); ctrl_data+=n; ctrl_remaining-=n; }
    else ctrl_zlp=0;
    R8_UEP0_T_LEN=n;
    R8_UEP0_CTRL=(R8_UEP0_CTRL & ~MASK_UEP_T_RES)|UEP_T_RES_ACK;
}
static void EP0_Reply(const uint8_t *data,uint16_t n,uint16_t requested)
{
    ctrl_data=data;
    ctrl_remaining=n<requested ? n : requested;
    ctrl_zlp=(n<requested && !(n%EP0_SIZE));
    if(!requested) {
        ctrl_state=CTRL_STATUS_OUT;
        return;
    }
    ctrl_state=CTRL_IN;
    EP0_Packet();
}
static void EP0_Status(void)
{
    ctrl_state=CTRL_STATUS_IN;
    R8_UEP0_T_LEN=0;
    R8_UEP0_CTRL=RB_UEP_R_TOG|RB_UEP_T_TOG|UEP_R_RES_NAK|UEP_T_RES_ACK;
}
static void USB_ResetState(void)
{
    usb_config=usb_suspended=ep1_busy=ep1_halted=report_sent=0;
    hid_idle=hid_leds=0; hid_protocol=1;
    ctrl_state=CTRL_IDLE; pending_address=0xff;
    R8_USB_DEV_AD=0;
    R8_UEP0_T_LEN=R8_UEP1_T_LEN=0;
    R8_UEP0_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK;
    R8_UEP1_CTRL=UEP_R_RES_NAK|UEP_T_RES_NAK;
}
static void USB_Setup(void)
{
    uint8_t type=EP0_Databuf[0],req=EP0_Databuf[1];
    uint16_t value=EP0_Databuf[2]|((uint16_t)EP0_Databuf[3]<<8);
    uint16_t index=EP0_Databuf[4]|((uint16_t)EP0_Databuf[5]<<8);
    uint16_t length=EP0_Databuf[6]|((uint16_t)EP0_Databuf[7]<<8);
    const uint8_t *data=0;
    uint16_t n=0;
    ctrl_state=CTRL_IDLE; pending_address=0xff; ctrl_zlp=0;
    R8_UEP0_CTRL=RB_UEP_R_TOG|RB_UEP_T_TOG|UEP_R_RES_ACK|UEP_T_RES_NAK;
    if(req==6 && (type==0x80 || type==0x81)) {
        if(type==0x80 && value==0x0100 && !index) { data=MyDevDescr; n=sizeof(MyDevDescr); }
        else if(type==0x80 && value==0x0200 && !index) { data=MyCfgDescr; n=sizeof(MyCfgDescr); }
        else if(type==0x80 && value==0x0300 && !index) { data=LangDescr; n=sizeof(LangDescr); }
        else if(type==0x81 && !index && value==0x2100) { data=MyCfgDescr+18; n=9; }
        else if(type==0x81 && !index && value==0x2200) { data=KeyboardReportDesc; n=sizeof(KeyboardReportDesc); }
        if(data) { EP0_Reply(data,n,length); return; }
    }
    else if(type==0 && req==5 && value<128 && !index && !length && !usb_config) {
        pending_address=value; EP0_Status(); return;
    }
    else if(type==0 && req==9 && value<=1 && !index && !length) {
        usb_config=value; ep1_busy=ep1_halted=report_sent=0;
        R8_UEP1_T_LEN=0; R8_UEP1_CTRL=UEP_R_RES_NAK|UEP_T_RES_NAK;
        EP0_Status(); return;
    }
    else if(type==0x80 && req==8 && !value && !index && length==1) {
        ctrl_reply[0]=usb_config; EP0_Reply(ctrl_reply,1,length); return;
    }
    else if(req==0 && !value && length==2 &&
            ((type==0x80 && !index) || (type==0x81 && !index && usb_config) ||
             (type==0x82 && (index==0 || index==0x80 || (index==0x81 && usb_config))))) {
        ctrl_reply[0]=(type==0x82 && index==0x81) ? ep1_halted : 0;
        ctrl_reply[1]=0; EP0_Reply(ctrl_reply,2,length); return;
    }
    else if(type==2 && (req==1 || req==3) && !value && index==0x81 && !length && usb_config) {
        ep1_halted=(req==3); ep1_busy=0; report_sent=0;
        R8_UEP1_CTRL=UEP_R_RES_NAK|(ep1_halted ? UEP_T_RES_STALL : UEP_T_RES_NAK);
        EP0_Status(); return;
    }
    else if(type==0x81 && req==10 && !value && !index && length==1 && usb_config) {
        ctrl_reply[0]=0; EP0_Reply(ctrl_reply,1,length); return;
    }
    else if(type==1 && req==11 && !value && !index && !length && usb_config) {
        ep1_busy=ep1_halted=report_sent=0;
        R8_UEP1_CTRL=UEP_R_RES_NAK|UEP_T_RES_NAK; EP0_Status(); return;
    }
    else if(!index && usb_config && type==0xa1) {
        if(req==1 && value==0x0100) { memcpy(ctrl_reply,current_report,8); n=8; }
        else if(req==1 && value==0x0200) { ctrl_reply[0]=hid_leds; n=1; }
        else if(req==2 && !value && length==1) { ctrl_reply[0]=hid_idle; n=1; }
        else if(req==3 && !value && length==1) { ctrl_reply[0]=hid_protocol; n=1; }
        if(n) { EP0_Reply(ctrl_reply,n,length); return; }
    }
    else if(!index && usb_config && type==0x21) {
        if(req==9 && value==0x0200 && length==1) {
            ctrl_state=CTRL_LED_OUT; return;
        }
        if(req==10 && !(value&0xff) && !length) {
            hid_idle=value>>8; last_report_tick=SYS_GetSysTickCnt(); EP0_Status(); return;
        }
        if(req==11 && value<=1 && !length) {
            hid_protocol=value; report_sent=0; EP0_Status(); return;
        }
    }
    EP0_Stall();
}

/* Polling keeps USB and report state in one context; call between ADC samples.
 * RB_UC_INT_BUSY makes hardware NAK until this function clears the event. */
static void USB_Poll(void)
{
    uint8_t flags=R8_USB_INT_FG,st=R8_USB_INT_ST;
    if(flags & RB_UIF_BUS_RST) {
        USB_ResetState();
        R8_USB_INT_FG=flags;
        return;
    }
    if(flags & RB_UIF_TRANSFER) {
        if(st & RB_UIS_SETUP_ACT) {
            if(R8_USB_RX_LEN==8) USB_Setup(); else EP0_Stall();
        }
        else switch(st & (MASK_UIS_TOKEN|MASK_UIS_ENDP)) {
            case UIS_TOKEN_IN:
                if(ctrl_state==CTRL_IN) {
                    if(ctrl_remaining || ctrl_zlp) { R8_UEP0_CTRL^=RB_UEP_T_TOG; EP0_Packet(); }
                    else { ctrl_state=CTRL_STATUS_OUT; R8_UEP0_CTRL=RB_UEP_R_TOG|UEP_R_RES_ACK|UEP_T_RES_NAK; }
                }
                else if(ctrl_state==CTRL_STATUS_IN) {
                    if(pending_address!=0xff) R8_USB_DEV_AD=pending_address;
                    pending_address=0xff; ctrl_state=CTRL_IDLE;
                    R8_UEP0_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK;
                }
                break;
            case UIS_TOKEN_OUT:
                if(!(st & RB_UIS_TOG_OK)) break;
                if(ctrl_state==CTRL_LED_OUT && R8_USB_RX_LEN==1) {
                    hid_leds=EP0_Databuf[0]&0x1f; EP0_Status();
                }
                else if((ctrl_state==CTRL_STATUS_OUT || ctrl_state==CTRL_IN) && !R8_USB_RX_LEN) {
                    ctrl_state=CTRL_IDLE; R8_UEP0_CTRL=UEP_R_RES_ACK|UEP_T_RES_NAK;
                }
                else EP0_Stall();
                break;
            case UIS_TOKEN_IN|1:
                R8_UEP1_CTRL^=RB_UEP_T_TOG;
                R8_UEP1_CTRL=(R8_UEP1_CTRL & ~MASK_UEP_T_RES)|UEP_T_RES_NAK;
                memcpy(last_report,EP1_Databuf,8);
                report_sent=1; ep1_busy=0; last_report_tick=SYS_GetSysTickCnt();
                break;
            default: break;
        }
        R8_USB_INT_FG=RB_UIF_TRANSFER;
    }
    if(flags & RB_UIF_SUSPEND) {
        usb_suspended=!!(R8_USB_MIS_ST & RB_UMS_SUSPEND);
        if(!usb_suspended) report_sent=0;
        R8_USB_INT_FG=RB_UIF_SUSPEND;
    }
    if(flags & RB_UIF_FIFO_OV) R8_USB_INT_FG=RB_UIF_FIFO_OV;
}
static void USB_SendReport(void)
{
    uint8_t idle_due=hid_idle && (uint32_t)(SYS_GetSysTickCnt()-last_report_tick)>=(uint32_t)hid_idle*240000u;
    if(!usb_config || usb_suspended || ep1_busy || ep1_halted) return;
    if(report_sent && !idle_due && !memcmp(current_report,last_report,8)) return;
    memcpy(EP1_Databuf,current_report,8);
    ep1_busy=1;
    R8_UEP1_T_LEN=8;
    R8_UEP1_CTRL=(R8_UEP1_CTRL & ~MASK_UEP_T_RES)|UEP_T_RES_ACK;
}
static void USB_Init(void)
{
    PFIC_DisableIRQ(USB_IRQn);
    R8_USB_CTRL=0;
    R8_UEP4_1_MOD=RB_UEP1_TX_EN;
    R8_UEP2_3_MOD=0;
    R16_UEP0_DMA=(uint16_t)(uintptr_t)EP0_Databuf;
    R16_UEP1_DMA=(uint16_t)(uintptr_t)EP1_Databuf;
    USB_ResetState();
    R8_USB_INT_EN=0;
    R8_USB_INT_FG=0xff;
    R16_PIN_ANALOG_IE|=RB_PIN_USB_IE|RB_PIN_USB_DP_PU;
    R8_UDEV_CTRL=RB_UD_PD_DIS|RB_UD_PORT_EN;
    R8_USB_CTRL=RB_UC_DEV_PU_EN|RB_UC_INT_BUSY|RB_UC_DMA_EN;
}
#endif
