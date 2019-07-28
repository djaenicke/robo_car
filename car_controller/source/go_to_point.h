/*
 * go_to_point_controller.h
 *
 *  Created on: Jul 28, 2019
 *      Author: Devin
 */

#ifndef GO_TO_POINT_H_
#define GO_TO_POINT_H_

#include <stdint.h>

extern void Run_Go_To_Point_Controller(void);
extern void Update_Destination(float x, float y, float robot_v);

#endif /* GO_TO_POINT_H_ */
