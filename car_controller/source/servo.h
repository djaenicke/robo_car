/*
 * servo.h
 *
 *  Created on: Jun 8, 2019
 *      Author: Devin
 */

#ifndef SERVO_H_
#define SERVO_H_

#include "fsl_ftm.h"

#define MIN_ANGLE_DEG (0.0f)
#define DEF_ANGLE_DEG (90.0f)
#define MAX_ANGLE_DEG (180.0f)

class Servo
{
private:
   bool  init_complete = false;
   float position_offset = 0;
   float cur_angle = DEF_ANGLE_DEG;
   float min_angle = MIN_ANGLE_DEG;
   float max_angle = MAX_ANGLE_DEG;
public:
   void  Init(float offset);

   float Get_Angle(void);
   float Get_Max_Angle(void);
   float Get_Min_Angle(void);

   void Set_Angle(float angle);
   void Set_Max_Angle(float angle);
   void Set_Min_Angle(float angle);
};

#endif /* SERVO_H_ */
