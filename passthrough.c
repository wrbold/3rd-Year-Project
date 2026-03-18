#include "edma_event.h" 
#include "interrupt.h"
#include "soc_OMAPL138.h"
#include "hw_syscfg0_OMAPL138.h"
#include "lcdkOMAPL138.h"
#include "codecif.h"
#include "mcasp.h"
#include "aic31.h"
#include "edma.h"
#include "psc.h"

#include <string.h>

//Internal Macro definitions

#define SLOT_SIZE                             (16u)


#define WORD_SIZE                             (16u)


#define SAMPLING_RATE                         (48000u)


#define NUM_I2S_CHANNELS                      (2u) 


#define NUM_SAMPLES_PER_AUDIO_BUF             (2000u)


#define NUM_BUF                               (3u)


#define NUM_PAR                               (2u)


#define PAR_ID_START                          (40u)


#define NUM_SAMPLES_LOOP_BUF                  (10u)


#define I2C_SLAVE_CODEC_AIC31                 (0x18u) 


#define INT_CHANNEL_I2C                       (2u)
#define INT_CHANNEL_MCASP                     (2u)
#define INT_CHANNEL_EDMACC                    (2u)


#define MCASP_XSER_RX                         (14u)


#define MCASP_XSER_TX                         (13u)

#define NUM_TX_SERIALIZERS                    ((NUM_I2S_CHANNELS >> 1) \
                                               + (NUM_I2S_CHANNELS & 0x01))
#define NUM_RX_SERIALIZERS                    ((NUM_I2S_CHANNELS >> 1) \
                                               + (NUM_I2S_CHANNELS & 0x01))
#define I2S_SLOTS                             ((1 << NUM_I2S_CHANNELS) - 1)

#define BYTES_PER_SAMPLE                      ((WORD_SIZE >> 3) \
                                               * NUM_I2S_CHANNELS)

#define AUDIO_BUF_SIZE                        (NUM_SAMPLES_PER_AUDIO_BUF \
                                               * BYTES_PER_SAMPLE)

#define TX_DMA_INT_ENABLE                     (EDMA3CC_OPT_TCC_SET(1) | (1 \
                                               << EDMA3CC_OPT_TCINTEN_SHIFT))
#define RX_DMA_INT_ENABLE                     (EDMA3CC_OPT_TCC_SET(0) | (1 \
                                               << EDMA3CC_OPT_TCINTEN_SHIFT))

#define PAR_RX_START                          (PAR_ID_START)
#define PAR_TX_START                          (PAR_RX_START + NUM_PAR)


#define SIZE_PARAMSET                         (32u)
#define OPT_FIFO_WIDTH                        (0x02 << 8u)

//Internal Func Prototypes
static void McASPErrorIsr(void);
static void McASPErrorIntSetup(void);
static void AIC31I2SConfigure(void);
static void McASPI2SConfigure(void);
static void McASPTxDMAComplHandler(void);
static void McASPRxDMAComplHandler(void);
static void EDMA3CCComplIsr(void);
static void I2SDataTxRxActivate(void);
static void I2SDMAParamInit(void);
static void ParamTxLoopJobSet(unsigned short parId);
static void BufferTxDMAActivate(unsigned int txBuf, unsigned short numSamples,
                                unsigned short parToUpdate, 
                                unsigned short linkAddr);
static void BufferRxDMAActivate(unsigned int rxBuf, unsigned short parId,
                                unsigned short parLink);

//Internal Variable Definitions

static unsigned char loopBuf[NUM_SAMPLES_LOOP_BUF * BYTES_PER_SAMPLE] = {0};


static unsigned char txBuf0[AUDIO_BUF_SIZE];
static unsigned char txBuf1[AUDIO_BUF_SIZE];
static unsigned char txBuf2[AUDIO_BUF_SIZE];


static unsigned char rxBuf0[AUDIO_BUF_SIZE];
static unsigned char rxBuf1[AUDIO_BUF_SIZE];
static unsigned char rxBuf2[AUDIO_BUF_SIZE];


static volatile unsigned int nxtBufToRcv = 0;


static volatile unsigned int lastFullRxBuf = 0;


static volatile unsigned short parOffRcvd = 0;


static volatile unsigned short parOffSent = 0;


static volatile unsigned short parOffTxToSend = 0;


static volatile unsigned int lastSentTxBuf = NUM_BUF - 1;

//Internal Constant Definitions

static unsigned int const rxBufPtr[NUM_BUF] =
       { 
           (unsigned int) rxBuf0,
           (unsigned int) rxBuf1,
           (unsigned int) rxBuf2
       };


static unsigned int const txBufPtr[NUM_BUF] =
       { 
           (unsigned int) txBuf0,
           (unsigned int) txBuf1,
           (unsigned int) txBuf2
       };


static struct EDMA3CCPaRAMEntry const txDefaultPar = 
       {
           (unsigned int)(EDMA3CC_OPT_DAM  | (0x02 << 8u)), /* Opt field */
           (unsigned int)loopBuf, /* source address */
           (unsigned short)(BYTES_PER_SAMPLE), /* aCnt */
           (unsigned short)(NUM_SAMPLES_LOOP_BUF), /* bCnt */ 
           (unsigned int) SOC_MCASP_0_DATA_REGS, /* dest address */
           (short) (BYTES_PER_SAMPLE), /* source bIdx */
           (short)(0), /* dest bIdx */
           (unsigned short)(PAR_TX_START * SIZE_PARAMSET), /* link address */
           (unsigned short)(0), /* bCnt reload value */
           (short)(0), /* source cIdx */
           (short)(0), /* dest cIdx */
           (unsigned short)1 /* cCnt */
       };


static struct EDMA3CCPaRAMEntry const rxDefaultPar =
       {
           (unsigned int)(EDMA3CC_OPT_SAM  | (0x02 << 8u)), /* Opt field */
           (unsigned int)SOC_MCASP_0_DATA_REGS, /* source address */
           (unsigned short)(BYTES_PER_SAMPLE), /* aCnt */
           (unsigned short)(1), /* bCnt */
           (unsigned int)rxBuf0, /* dest address */
           (short) (0), /* source bIdx */
           (short)(BYTES_PER_SAMPLE), /* dest bIdx */
           (unsigned short)(PAR_RX_START * SIZE_PARAMSET), /* link address */
           (unsigned short)(0), /* bCnt reload value */
           (short)(0), /* source cIdx */
           (short)(0), /* dest cIdx */
           (unsigned short)1 /* cCnt */
       };

//Function Definitions

static void ParamTxLoopJobSet(unsigned short parId)
{
    EDMA3CCPaRAMEntry paramSet;
    
    memcpy(&paramSet, &txDefaultPar, SIZE_PARAMSET - 2);
  
    /* link the paRAM to itself */
    paramSet.linkAddr = parId * SIZE_PARAMSET;

    EDMA3SetPaRAM(SOC_EDMA30CC_0_REGS, parId, &paramSet);
}


static void I2SDMAParamInit(void)
{
    EDMA3CCPaRAMEntry paramSet;
    int idx; 
 
   
    memcpy(&paramSet, &rxDefaultPar, SIZE_PARAMSET - 2);

    EDMA3SetPaRAM(SOC_EDMA30CC_0_REGS, EDMA3_CHA_MCASP0_RX, &paramSet);

    paramSet.opt |= RX_DMA_INT_ENABLE; 
 
    for(idx = 0 ; idx < NUM_PAR; idx++)
    {
        paramSet.destAddr = rxBufPtr[idx];

        paramSet.linkAddr = (PAR_RX_START + ((idx + 1) % NUM_PAR)) 
                             * (SIZE_PARAMSET);        

        paramSet.bCnt =  NUM_SAMPLES_PER_AUDIO_BUF;

      
        if( 0 == idx)
        {
            paramSet.destAddr += BYTES_PER_SAMPLE;
            paramSet.bCnt -= BYTES_PER_SAMPLE;
        }

        EDMA3SetPaRAM(SOC_EDMA30CC_0_REGS, (PAR_RX_START + idx), &paramSet);
    } 

   
    nxtBufToRcv = idx % NUM_BUF;
    lastFullRxBuf = NUM_BUF - 1;
    parOffRcvd = 0;

   
    memcpy(&paramSet, &txDefaultPar, SIZE_PARAMSET);

    EDMA3SetPaRAM(SOC_EDMA30CC_0_REGS, EDMA3_CHA_MCASP0_TX, &paramSet);

    
    for(idx = 0 ; idx < NUM_PAR; idx++)
    {
        ParamTxLoopJobSet(PAR_TX_START + idx);
    }
 
   
    parOffSent = 0;
    lastSentTxBuf = NUM_BUF - 1; 
}


static void AIC31I2SConfigure(void)
{
    volatile unsigned int delay = 0xFFF;

    AIC31Reset(SOC_I2C_0_REGS);
    while(delay--);

    
    AIC31DataConfig(SOC_I2C_0_REGS, AIC31_DATATYPE_I2S, SLOT_SIZE, 0);
    AIC31SampleRateConfig(SOC_I2C_0_REGS, AIC31_MODE_BOTH, SAMPLING_RATE);

   
    AIC31ADCInit(SOC_I2C_0_REGS);
    AIC31DACInit(SOC_I2C_0_REGS);
}


static void McASPI2SConfigure(void)
{
    McASPRxReset(SOC_MCASP_0_CTRL_REGS);
    McASPTxReset(SOC_MCASP_0_CTRL_REGS);

  
    McASPReadFifoEnable(SOC_MCASP_0_FIFO_REGS, 1, 1);
    McASPWriteFifoEnable(SOC_MCASP_0_FIFO_REGS, 1, 1);

   
    McASPRxFmtI2SSet(SOC_MCASP_0_CTRL_REGS, WORD_SIZE, SLOT_SIZE,
                     MCASP_RX_MODE_DMA);
    McASPTxFmtI2SSet(SOC_MCASP_0_CTRL_REGS, WORD_SIZE, SLOT_SIZE,
                     MCASP_TX_MODE_DMA);

   
    McASPRxFrameSyncCfg(SOC_MCASP_0_CTRL_REGS, 2, MCASP_RX_FS_WIDTH_WORD, 
                        MCASP_RX_FS_EXT_BEGIN_ON_FALL_EDGE);
    McASPTxFrameSyncCfg(SOC_MCASP_0_CTRL_REGS, 2, MCASP_TX_FS_WIDTH_WORD, 
                        MCASP_TX_FS_EXT_BEGIN_ON_RIS_EDGE);

   
    McASPRxClkCfg(SOC_MCASP_0_CTRL_REGS, MCASP_RX_CLK_EXTERNAL, 0, 0);
    McASPRxClkPolaritySet(SOC_MCASP_0_CTRL_REGS, MCASP_RX_CLK_POL_RIS_EDGE); 
    McASPRxClkCheckConfig(SOC_MCASP_0_CTRL_REGS, MCASP_RX_CLKCHCK_DIV32,
                          0x00, 0xFF);

   
    McASPTxClkCfg(SOC_MCASP_0_CTRL_REGS, MCASP_TX_CLK_EXTERNAL, 0, 0);
    McASPTxClkPolaritySet(SOC_MCASP_0_CTRL_REGS, MCASP_TX_CLK_POL_FALL_EDGE); 
    McASPTxClkCheckConfig(SOC_MCASP_0_CTRL_REGS, MCASP_TX_CLKCHCK_DIV32,
                          0x00, 0xFF);
 
   
    McASPTxRxClkSyncEnable(SOC_MCASP_0_CTRL_REGS);

   
    McASPRxTimeSlotSet(SOC_MCASP_0_CTRL_REGS, I2S_SLOTS);
    McASPTxTimeSlotSet(SOC_MCASP_0_CTRL_REGS, I2S_SLOTS);

   
    McASPSerializerRxSet(SOC_MCASP_0_CTRL_REGS, MCASP_XSER_RX);
    McASPSerializerTxSet(SOC_MCASP_0_CTRL_REGS, MCASP_XSER_TX);

   
    McASPPinMcASPSet(SOC_MCASP_0_CTRL_REGS, 0xFFFFFFFF);
    McASPPinDirOutputSet(SOC_MCASP_0_CTRL_REGS, MCASP_PIN_AXR(MCASP_XSER_TX));
    McASPPinDirInputSet(SOC_MCASP_0_CTRL_REGS, MCASP_PIN_AFSX 
                                               | MCASP_PIN_ACLKX
                                               | MCASP_PIN_AFSR
                                               | MCASP_PIN_ACLKR
                                               | MCASP_PIN_AXR(MCASP_XSER_RX));

    
    McASPTxIntEnable(SOC_MCASP_0_CTRL_REGS, MCASP_TX_DMAERROR 
                                            | MCASP_TX_CLKFAIL 
                                            | MCASP_TX_SYNCERROR
                                            | MCASP_TX_UNDERRUN);

    McASPRxIntEnable(SOC_MCASP_0_CTRL_REGS, MCASP_RX_DMAERROR 
                                            | MCASP_RX_CLKFAIL
                                            | MCASP_RX_SYNCERROR 
                                            | MCASP_RX_OVERRUN);
}

static void EDMA3IntSetup(void)
{
#ifdef _TMS320C6X
	IntRegister(C674X_MASK_INT5, EDMA3CCComplIsr);
	IntEventMap(C674X_MASK_INT5, SYS_INT_EDMA3_0_CC0_INT1);
	IntEnable(C674X_MASK_INT5);
#else
    IntRegister(SYS_INT_CCINT0, EDMA3CCComplIsr);
    IntChannelSet(SYS_INT_CCINT0, INT_CHANNEL_EDMACC); 
    IntSystemEnable(SYS_INT_CCINT0);
#endif
}


static void McASPErrorIntSetup(void)
{
#ifdef _TMS320C6X
	IntRegister(C674X_MASK_INT6, McASPErrorIsr);
	IntEventMap(C674X_MASK_INT6, SYS_INT_MCASP0_INT);
	IntEnable(C674X_MASK_INT6);
#else
    /* Register the error ISR for McASP */
    IntRegister(SYS_INT_MCASPINT, McASPErrorIsr);

    IntChannelSet(SYS_INT_MCASPINT, INT_CHANNEL_MCASP);
    IntSystemEnable(SYS_INT_MCASPINT);
#endif
}


static void I2SDataTxRxActivate(void)
{
    
    McASPRxClkStart(SOC_MCASP_0_CTRL_REGS, MCASP_RX_CLK_EXTERNAL);
    McASPTxClkStart(SOC_MCASP_0_CTRL_REGS, MCASP_TX_CLK_EXTERNAL);

   
    EDMA3EnableTransfer(SOC_EDMA30CC_0_REGS, EDMA3_CHA_MCASP0_RX,
                        EDMA3_TRIG_MODE_EVENT);
    EDMA3EnableTransfer(SOC_EDMA30CC_0_REGS, 
                        EDMA3_CHA_MCASP0_TX, EDMA3_TRIG_MODE_EVENT);

    
    McASPRxSerActivate(SOC_MCASP_0_CTRL_REGS);
    McASPTxSerActivate(SOC_MCASP_0_CTRL_REGS);

   
    while(McASPTxStatusGet(SOC_MCASP_0_CTRL_REGS) & MCASP_TX_STAT_DATAREADY);

   
    McASPRxEnable(SOC_MCASP_0_CTRL_REGS);
    McASPTxEnable(SOC_MCASP_0_CTRL_REGS);
}


void BufferTxDMAActivate(unsigned int txBuf, unsigned short numSamples,
                         unsigned short parId, unsigned short linkPar)
{
    EDMA3CCPaRAMEntry paramSet;

   
    memcpy(&paramSet, &txDefaultPar, SIZE_PARAMSET - 2);
    
   
    paramSet.opt |= TX_DMA_INT_ENABLE;
    paramSet.srcAddr =  txBufPtr[txBuf];
    paramSet.linkAddr = linkPar * SIZE_PARAMSET;  
    paramSet.bCnt = numSamples;

    EDMA3SetPaRAM(SOC_EDMA30CC_0_REGS, parId, &paramSet);
}


int main(void)
{
    unsigned short parToSend;
    unsigned short parToLink;

    
    I2CPinMuxSetup(0);
    McASPPinMuxSetup();

  
    PSCModuleControl(SOC_PSC_1_REGS, HW_PSC_MCASP0, PSC_POWERDOMAIN_ALWAYS_ON,
		     PSC_MDCTL_NEXT_ENABLE);

  
    PSCModuleControl(SOC_PSC_0_REGS, HW_PSC_CC0, PSC_POWERDOMAIN_ALWAYS_ON,
		     PSC_MDCTL_NEXT_ENABLE);
    PSCModuleControl(SOC_PSC_0_REGS, HW_PSC_TC0, PSC_POWERDOMAIN_ALWAYS_ON,
		     PSC_MDCTL_NEXT_ENABLE);

#ifdef _TMS320C6X
   
    IntDSPINTCInit();
#else
   
    IntAINTCInit();
#endif

  
    I2CCodecIfInit(SOC_I2C_0_REGS, INT_CHANNEL_I2C, I2C_SLAVE_CODEC_AIC31);

    EDMA3Init(SOC_EDMA30CC_0_REGS, 0);
    EDMA3IntSetup(); 

    McASPErrorIntSetup();
  
#ifdef _TMS320C6X
    IntGlobalEnable();
#else
   
    IntMasterIRQEnable();
    IntGlobalEnable();
    IntIRQEnable();
#endif

   
    EDMA3RequestChannel(SOC_EDMA30CC_0_REGS, EDMA3_CHANNEL_TYPE_DMA,
                        EDMA3_CHA_MCASP0_TX, EDMA3_CHA_MCASP0_TX, 0);
    EDMA3RequestChannel(SOC_EDMA30CC_0_REGS, EDMA3_CHANNEL_TYPE_DMA,
                        EDMA3_CHA_MCASP0_RX, EDMA3_CHA_MCASP0_RX, 0);

   
    I2SDMAParamInit();

   
    AIC31I2SConfigure();

    
    McASPI2SConfigure();
  
   
    I2SDataTxRxActivate();
   
    
    while(1)
    {
        if(lastFullRxBuf != lastSentTxBuf)
        {  
           
            parToSend =  PAR_TX_START + (parOffTxToSend % NUM_PAR);
            parOffTxToSend = (parOffTxToSend + 1) % NUM_PAR;
            parToLink  = PAR_TX_START + parOffTxToSend; 
 
            lastSentTxBuf = (lastSentTxBuf + 1) % NUM_BUF;

            
            memcpy((void *)txBufPtr[lastSentTxBuf],
                   (void *)rxBufPtr[lastFullRxBuf],
                   AUDIO_BUF_SIZE);

          
            BufferTxDMAActivate(lastSentTxBuf, NUM_SAMPLES_PER_AUDIO_BUF,
                                (unsigned short)parToSend,
                                (unsigned short)parToLink);
        }
    }
}  


static void BufferRxDMAActivate(unsigned int rxBuf, unsigned short parId,
                                unsigned short parLink)
{
    EDMA3CCPaRAMEntry paramSet;

   
    memcpy(&paramSet, &rxDefaultPar, SIZE_PARAMSET - 2);

    
    paramSet.opt |= RX_DMA_INT_ENABLE;
    paramSet.destAddr =  rxBufPtr[rxBuf];
    paramSet.bCnt =  NUM_SAMPLES_PER_AUDIO_BUF;
    paramSet.linkAddr = parLink * SIZE_PARAMSET ;

    EDMA3SetPaRAM(SOC_EDMA30CC_0_REGS, parId, &paramSet);
}


static void McASPRxDMAComplHandler(void)
{
    unsigned short nxtParToUpdate;

    
    lastFullRxBuf = (lastFullRxBuf + 1) % NUM_BUF;
    nxtParToUpdate =  PAR_RX_START + parOffRcvd;  
    parOffRcvd = (parOffRcvd + 1) % NUM_PAR;
 
   
    BufferRxDMAActivate(nxtBufToRcv, nxtParToUpdate,
                        PAR_RX_START + parOffRcvd);
    
   
    nxtBufToRcv = (nxtBufToRcv + 1) % NUM_BUF;
}


static void McASPTxDMAComplHandler(void)
{
    ParamTxLoopJobSet((unsigned short)(PAR_TX_START + parOffSent));

    parOffSent = (parOffSent + 1) % NUM_PAR;
}


static void EDMA3CCComplIsr(void) 
{ 
#ifdef _TMS320C6X
	IntEventClear(SYS_INT_EDMA3_0_CC0_INT1);
#else
    IntSystemStatusClear(SYS_INT_CCINT0);
#endif

   
    if(EDMA3GetIntrStatus(SOC_EDMA30CC_0_REGS) & (1 << EDMA3_CHA_MCASP0_RX)) 
    { 
       
        EDMA3ClrIntr(SOC_EDMA30CC_0_REGS, EDMA3_CHA_MCASP0_RX); 
        McASPRxDMAComplHandler();
    }
    
   
    if(EDMA3GetIntrStatus(SOC_EDMA30CC_0_REGS) & (1 << EDMA3_CHA_MCASP0_TX)) 
    { 
       
        EDMA3ClrIntr(SOC_EDMA30CC_0_REGS, EDMA3_CHA_MCASP0_TX); 
        McASPTxDMAComplHandler();
    }
}


static void McASPErrorIsr(void)
{
#ifdef _TMS320C6X
	IntEventClear(SYS_INT_MCASP0_INT);
#else
    IntSystemStatusClear(SYS_INT_MCASPINT);
#endif

    ; 
}

