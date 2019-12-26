/*
 * behaviors.cpp
 *
 *  Created on: Jul 28, 2019
 *      Author: Devin
 */


#include "FreeRTOS.h"
#include "task.h"

#include "behaviors.h"
#include "inertial_states.h"
#include "object_detection.h"
#include "go_to_point.h"
#include "motor_controls.h"
#include "udp_client.h"

#define S_2_MS 1000
#define NUM_WAYPOINTS (1)

/* Tuning parameters */
#define Kp  (4.0f)
#define TOLERANCE (0.01) /* (m) */
#define GTP_SPEED (0.5)  /* (m/s) */

static UdpClient ROS_UDP;

static GoToPointController GTP_Controller;
bool auto_mode_active = false;

static Destination_T Waypoints[NUM_WAYPOINTS] = {4,0};
static uint8_t Current_Waypoint = 0;

void Behaviors_Task(void *pvParameters)
{
   while(1)
   {
      Update_Robot_States();

      Run_Object_Detection();

      if (auto_mode_active)
      {
         if (GTP_Controller.In_Route())
         {
            GTP_Controller.Execute();
         }
         else
         {
            if (Current_Waypoint < NUM_WAYPOINTS)
            {
               GTP_Controller.Update_Destination(&(Waypoints[Current_Waypoint]));
               Current_Waypoint++;
            }
            else
            {
               Toggle_Autonomous_Mode();
            }
         }
      }

      Run_Motor_Controls();

      vTaskDelay(pdMS_TO_TICKS(CYCLE_TIME*S_2_MS));
   }
}

void Init_Behaviors(void)
{
   ip4_addr_t remote_ip;
   remote_ip.addr = ipaddr_addr("192.168.1.4");

   if (UDP_CLIENT_SUCCESS == ROS_UDP.Init())
   {
      ROS_UDP.Set_Remote_Ip(&remote_ip);
      ROS_UDP.Set_Remote_Port((uint16_t)5000);

      if (UDP_CLIENT_SUCCESS == ROS_UDP.Connect())
      {
         ROS_UDP.Send_Datagram("Hello World!", strlen("Hello World!"));
      }
   }

   GTP_Controller.Init(TOLERANCE, Kp, GTP_SPEED);
}

void Toggle_Autonomous_Mode(void)
{
   if (auto_mode_active)
   {
      auto_mode_active = false;
   }
   else
   {
      auto_mode_active = true;
   }

   Stop();
}
