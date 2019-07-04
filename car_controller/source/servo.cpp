/*
 * servo.cpp
 *
 *  Created on: Jun 8, 2019
 *      Author: Devin
 */

#include <math.h>

#include "servo.h"
#include "io_abstraction.h"
#include "fsl_ftm.h"
#include "clock_config.h"
#include "assert.h"
#include "fsl_common.h"

typedef enum {
   DC_OFF,
   DC_ON
} PWM_DC_State_T;

typedef struct {
   PWM_DC_State_T dc_state;
   uint16_t on_time;
   uint16_t off_time;
   uint16_t period;
} Software_PWM_T;

#define FTM_SOURCE_CLOCK (CLOCK_GetFreq(kCLOCK_BusClk)/32)
#define SERVO_PWM_FREQ (50.0f) /* Hz */
#define SEC_2_US (1000000)
#define SERVO_PERIOD (uint16_t)((1/SERVO_PWM_FREQ)*SEC_2_US)

#define MIN_PULSE_TIME    (400.0f) /* us */
#define MAX_PULSE_WIDTH  (2000.0f) /* us */

#define ANGLE_2_PULSE_WIDTH_TIME(angle) roundf((((angle)*MAX_PULSE_WIDTH/MAX_ANGLE_DEG)+MIN_PULSE_TIME))

static volatile Software_PWM_T PWM;

void Servo::Init(float offset, FTM_Type *ftm_base_ptr)
{
   /* Configure the PWM output */
   ftm_config_t ftmInfo;

   assert(ftm_base_ptr);

   ftm_ptr = ftm_base_ptr;

   position_offset = offset;
   min_angle += position_offset;
   max_angle -= position_offset;

   /* Initialize the software based PWM parameters */
   PWM.period   = SERVO_PERIOD;
   PWM.on_time  = ANGLE_2_PULSE_WIDTH_TIME(cur_angle + position_offset);
   PWM.off_time = (PWM.period - PWM.on_time);
   PWM.dc_state = DC_ON;

   FTM_GetDefaultConfig(&ftmInfo);

   /* Divide FTM clock by 32 */
   ftmInfo.prescale = kFTM_Prescale_Divide_32;

   FTM_Init(ftm_base_ptr, &ftmInfo);

   FTM_SetTimerPeriod(ftm_ptr, USEC_TO_COUNT(PWM.on_time, FTM_SOURCE_CLOCK));

   FTM_EnableInterrupts(ftm_ptr, kFTM_TimeOverflowInterruptEnable);

   switch((uint32_t)ftm_ptr)
   {
      case FTM0_BASE:
         EnableIRQ(FTM0_IRQn);
         break;
      case FTM1_BASE:
         EnableIRQ(FTM1_IRQn);
         break;
      case FTM2_BASE:
         EnableIRQ(FTM2_IRQn);
         break;
      case FTM3_BASE:
         EnableIRQ(FTM3_IRQn);
         break;
      default:
         assert(false);
   }

   Set_GPIO(SERVO, HIGH);

   FTM_StartTimer(ftm_ptr, kFTM_SystemClock);

   init_complete = true;
}

float Servo::Get_Angle(void)
{
   return(cur_angle);
}

float Servo::Get_Max_Angle(void)
{
   return(max_angle);
}

float Servo::Get_Min_Angle(void)
{
   return(min_angle);
}

void Servo::Set_Angle(float angle)
{
   if (!init_complete)
   {
      /* Init method must be called first */
      assert(false);
   }

   /* Saturate the angle to be within [min_angle, max_angle] */
   cur_angle = angle > max_angle ? max_angle : angle < min_angle ? min_angle : angle;

   FTM_DisableInterrupts(ftm_ptr, kFTM_TimeOverflowInterruptEnable);

   PWM.on_time = ANGLE_2_PULSE_WIDTH_TIME(cur_angle + position_offset);
   PWM.off_time = (PWM.period - PWM.on_time);

   FTM_EnableInterrupts(ftm_ptr, kFTM_TimeOverflowInterruptEnable);
}

void Servo::Set_Max_Angle(float angle)
{
   max_angle = angle < max_angle ? angle : max_angle;
}

void Servo::Set_Min_Angle(float angle)
{
   min_angle = angle > min_angle ? angle : min_angle;
}

extern "C"
{
void FTM3_IRQHandler(void)
{
   FTM_StopTimer(FTM3);

   if (DC_ON == PWM.dc_state)
   {
      Set_GPIO(SERVO, LOW);
      FTM_SetTimerPeriod(FTM3, USEC_TO_COUNT(PWM.off_time, FTM_SOURCE_CLOCK));
      PWM.dc_state = DC_OFF;
   }
   else
   {
      Set_GPIO(SERVO, HIGH);
      FTM_SetTimerPeriod(FTM3, USEC_TO_COUNT(PWM.on_time, FTM_SOURCE_CLOCK));
      PWM.dc_state = DC_ON;
   }

   FTM_StartTimer(FTM3, kFTM_SystemClock);

   /* Clear interrupt flag.*/
    FTM_ClearStatusFlags(FTM3, kFTM_TimeOverflowFlag);
    __DSB();
}
}

