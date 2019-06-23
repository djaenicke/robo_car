/*
 * wheel_speeds.cpp
 *
 *  Created on: Jun 14, 2019
 *      Author: Devin
 */

#include <string.h>
#include "wheel_speeds.h"
#include "fsl_port.h"
#include "io_abstraction.h"
#include "assert.h"
#include "fsl_ftm.h"
#include "low_pass_filter.h"

#define ISR_Flag_Is_Set(pos) ((Pin_Cfgs[pos].pbase->PCR[Pin_Cfgs[pos].pin] >> PORT_PCR_ISF_SHIFT) && (uint32_t) 0x01)
#define Clear_ISR_Flag(pos)  Pin_Cfgs[pos].pbase->PCR[Pin_Cfgs[pos].pin] |= PORT_PCR_ISF(1);

#define PULSES_PER_REV (20.0f)
#define RAD_PER_REV    (6.2831853f)
#define CLK_PERIOD     (0.00002048f)
#define START          ((uint8_t)0)
#define END            ((uint8_t)1)
#define FILTER_ALPHA   (0.5f) /* TODO - update this to a better value */

typedef struct {
   uint8_t  meas_type;
   uint16_t start_cnt;
   uint16_t period_cnt;
} Period_T;

static volatile Period_T Encoder_Period[NUM_WHEELS] = {0};

static inline void  Measure_Period(Wheel_T pos);
static inline float Period_2_Speed(Wheel_T pos);

void Init_Wheel_Speed_Sensors(void)
{
   port_interrupt_t p_int_cfg;
   ftm_config_t ftmInfo;

   FTM_GetDefaultConfig(&ftmInfo);

   ftmInfo.prescale = kFTM_Prescale_Divide_32;

   /* Initialize FTM module */
   FTM_Init(FTM1, &ftmInfo);

   FTM_SetTimerPeriod(FTM1, UINT16_MAX);
   FTM_StartTimer(FTM1, kFTM_FixedClock);

   /* Enable interrupts to capture the optical encoder pulses */
   p_int_cfg = kPORT_InterruptRisingEdge;
   PORT_SetPinInterruptConfig(Pin_Cfgs[RR_SPEED_SENSOR].pbase, Pin_Cfgs[RR_SPEED_SENSOR].pin, p_int_cfg);
   PORT_SetPinInterruptConfig(Pin_Cfgs[RL_SPEED_SENSOR].pbase, Pin_Cfgs[RL_SPEED_SENSOR].pin, p_int_cfg);
   PORT_SetPinInterruptConfig(Pin_Cfgs[FR_SPEED_SENSOR].pbase, Pin_Cfgs[FR_SPEED_SENSOR].pin, p_int_cfg);
   PORT_SetPinInterruptConfig(Pin_Cfgs[FL_SPEED_SENSOR].pbase, Pin_Cfgs[FL_SPEED_SENSOR].pin, p_int_cfg);

   NVIC_SetPriority(PORTB_IRQn, 2);
   NVIC_SetPriority(PORTC_IRQn, 2);

   PORT_ClearPinsInterruptFlags(PORTB, 0xFFFFFFFF);
   EnableIRQ(PORTB_IRQn);

   PORT_ClearPinsInterruptFlags(PORTC, 0xFFFFFFFF);
   EnableIRQ(PORTC_IRQn);
}

void Get_Wheel_Speeds(Wheel_Speeds_T * speeds)
{
   if (NULL != speeds)
   {
      speeds->rr = Period_2_Speed(RR);
      speeds->rl = Period_2_Speed(RL);
      speeds->fr = Period_2_Speed(FR);
      speeds->fl = Period_2_Speed(FL);
   }
   else
   {
      assert(false);
   }
}

void Zero_Wheel_Speeds(void)
{
   for (uint8_t i=0; i<(uint8_t)NUM_WHEELS; i++)
   {
      Encoder_Period[i].period_cnt = (uint16_t) 0;
   }
}

extern "C"
{
void PORTC_IRQHandler(void)
{
   /* Determine which wheel speed sensor(s) caused the interrupt */
   if (ISR_Flag_Is_Set(RL_SPEED_SENSOR))
   {
      Measure_Period(RL);
      Clear_ISR_Flag(RL_SPEED_SENSOR);
   }
   if (ISR_Flag_Is_Set(FR_SPEED_SENSOR))
   {
      Measure_Period(FR);
      Clear_ISR_Flag(FR_SPEED_SENSOR);
   }
   else
   {
      assert(false);
   }
}

void PORTB_IRQHandler(void)
{
   /* Determine which wheel speed sensor(s) caused the interrupt */
   if (ISR_Flag_Is_Set(RR_SPEED_SENSOR))
   {
      Measure_Period(RR);
      Clear_ISR_Flag(RR_SPEED_SENSOR);
   }
   if (ISR_Flag_Is_Set(FL_SPEED_SENSOR))
   {
      Measure_Period(FL);
      Clear_ISR_Flag(FL_SPEED_SENSOR);
   }
   else
   {
      assert(false);
   }
}
}

static inline void Measure_Period(Wheel_T pos)
{
   if (START == Encoder_Period[pos].meas_type)
   {
      Encoder_Period[pos].start_cnt = (uint16_t) FTM1->CNT;
      Encoder_Period[pos].meas_type = END;
   }
   else
   {
      Encoder_Period[pos].period_cnt = (uint16_t) LP_Filter(Encoder_Period[pos].period_cnt, (uint16_t) FTM1->CNT, FILTER_ALPHA);
      Encoder_Period[pos].meas_type  = START;
   }
}

static inline float Period_2_Speed(Wheel_T pos)
{
   float temp = 0.0f;

   temp = RAD_PER_REV/(PULSES_PER_REV*Encoder_Period[pos].period_cnt*CLK_PERIOD);

   return temp;
}

