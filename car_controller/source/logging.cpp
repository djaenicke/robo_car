/*
 * logging.cpp
 *
 *  Created on: Jun 16, 2019
 *      Author: Devin
 */

#include <stdio.h>
#include <string.h>
#include "fsl_sd.h"
#include "fsl_debug_console.h"
#include "ff.h"
#include "diskio.h"
#include "fsl_sd_disk.h"
#include "board.h"
#include "battery_monitor.h"
#include "wheel_speeds.h"
#include "low_pass_filter.h"

#include "pin_mux.h"
#include "clock_config.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define BUFFER_SIZE (100U)
#define CLOSED (0U)
#define OPEN   (1U)

/*******************************************************************************
* Variables
******************************************************************************/
SDK_ALIGN(uint8_t Write_Buffer[SDK_SIZEALIGN(BUFFER_SIZE, SDMMC_DATA_BUFFER_ALIGN_CACHE)],
          MAX(SDMMC_DATA_BUFFER_ALIGN_CACHE, SDMMCHOST_DMA_BUFFER_ADDR_ALIGN));

static void SD_Card_Detect_Call_Back(bool is_inserted, void *userData);
static const sdmmchost_detect_card_t s_sdCardDetect = {
    .cdType = kSDMMCHOST_DetectCardByGpioCD,
    .cdTimeOut_ms = (~0U),
    .cardInserted = SD_Card_Detect_Call_Back,
    .cardRemoved  = SD_Card_Detect_Call_Back,
};

static FATFS File_System;
static FIL   File_Object;
static uint8_t File_State = CLOSED;
static bool File_System_Init_Complete = false;

static volatile bool Card_Inserted      = false;
static volatile bool Card_Insert_Status = false;

static SemaphoreHandle_t Card_Detect_Semaphore = NULL;
static SemaphoreHandle_t File_Access_Semaphore = NULL;

static const uint32_t END_PATTERN = 0xFFFFFFFF;

static int Init_File_System(void);

/*******************************************************************************
 * Function Definitions
 ******************************************************************************/
void Data_Logging_Task(void *pvParameters)
{
   static uint32_t cnt;
   float vbatt = 0.0;
   Wheel_Speeds_T wheel_speeds;
   Wheel_Speeds_T filt_wheel_speeds;
   uint32_t bw = 0;

   while(1)
   {
      vbatt = Read_Battery_Voltage();

      Get_Wheel_Speeds(&wheel_speeds);

      filt_wheel_speeds.rr = LP_Filter(wheel_speeds.rr, filt_wheel_speeds.rr, 0.4);
      filt_wheel_speeds.rr = LP_Filter(wheel_speeds.rl, filt_wheel_speeds.rl, 0.4);
      filt_wheel_speeds.rr = LP_Filter(wheel_speeds.fr, filt_wheel_speeds.fr, 0.4);
      filt_wheel_speeds.rr = LP_Filter(wheel_speeds.fl, filt_wheel_speeds.fl, 0.4);

      if (pdTRUE == xSemaphoreTake(File_Access_Semaphore, portMAX_DELAY))
      {
         if (SD_IsCardPresent(&g_sd) && OPEN == File_State)
         {
            f_write(&File_Object, &cnt,                  sizeof(uint32_t), (UINT *)&bw);
            f_write(&File_Object, &vbatt,                sizeof(float),    (UINT *)&bw);
            f_write(&File_Object, &wheel_speeds.rr,      sizeof(float),    (UINT *)&bw);
            f_write(&File_Object, &wheel_speeds.rl,      sizeof(float),    (UINT *)&bw);
            f_write(&File_Object, &wheel_speeds.fr,      sizeof(float),    (UINT *)&bw);
            f_write(&File_Object, &wheel_speeds.fl,      sizeof(float),    (UINT *)&bw);
            f_write(&File_Object, &filt_wheel_speeds.rr, sizeof(float),    (UINT *)&bw);
            f_write(&File_Object, &filt_wheel_speeds.rl, sizeof(float),    (UINT *)&bw);
            f_write(&File_Object, &filt_wheel_speeds.fr, sizeof(float),    (UINT *)&bw);
            f_write(&File_Object, &filt_wheel_speeds.fl, sizeof(float),    (UINT *)&bw);

            f_write(&File_Object, &END_PATTERN,     sizeof(uint32_t), (UINT *)&bw);
            cnt++;
         }

         xSemaphoreGive(File_Access_Semaphore);
      }
      else
      {
         PRINTF("Failed to take semaphore.\r\n");
      }
      vTaskDelay(pdMS_TO_TICKS(50));
   }
}

void SD_Card_Init_Task(void *pvParameters)
{
   Card_Detect_Semaphore = xSemaphoreCreateBinary();
   File_Access_Semaphore = xSemaphoreCreateBinary();

   g_sd.host.base           = SD_HOST_BASEADDR;
   g_sd.host.sourceClock_Hz = SD_HOST_CLK_FREQ;
   g_sd.usrParam.cd         = &s_sdCardDetect;

   if (SD_HostInit(&g_sd) == kStatus_Success)
   {
      while (true)
      {
         if (xSemaphoreTake(Card_Detect_Semaphore, pdMS_TO_TICKS(1000)) == pdTRUE)
         {
            if (Card_Inserted != Card_Insert_Status)
            {
               Card_Inserted = Card_Insert_Status;

               SD_PowerOffCard(g_sd.host.base, g_sd.usrParam.pwr);

               if (Card_Inserted)
               {
                  PRINTF("\r\nCard inserted.\r\n");

                  SD_PowerOnCard(g_sd.host.base, g_sd.usrParam.pwr);

                  if (-1 != Init_File_System())
                  {
                     xSemaphoreGive(File_Access_Semaphore);
                  }
               }
            }

            if (!Card_Inserted)
            {
               PRINTF("\r\nPlease insert a card into board.\r\n");
            }
         }
      }
   }
   else
   {
      PRINTF("\r\nSD host init fail\r\n");
   }

   vTaskSuspend(NULL);
}

static int Init_File_System(void)
{
   FRESULT error;
   const TCHAR drive_number[3U] = {SDDISK + '0', ':', '/'};

   error = f_mount(&File_System, drive_number, 0U);
   if (error)
   {
       PRINTF("Mount volume failed.\r\n");
       return -1;
   }

   error = f_chdrive((char const *)&drive_number[0U]);
   if (error)
   {
       PRINTF("Change drive failed.\r\n");
       return -1;
   }

   PRINTF("\r\nCreate directory......\r\n");
   error = f_mkdir(_T("/dir_1"));
   if (error)
   {
       if (error == FR_EXIST)
       {
          PRINTF("Directory exists.\r\n");
       }
       else
       {
         PRINTF("Make directory failed.\r\n");
         return -1;
       }
   }

   File_System_Init_Complete = true;
   return 0;
}

void Open_Log_File(void)
{
   FRESULT error;

   if (File_System_Init_Complete)
   {
      PRINTF("\r\nCreate a file......\r\n");
      error = f_open(&File_Object, _T("/dir_1/data_log.bin"), (FA_WRITE | FA_CREATE_ALWAYS));
      if (error)
      {
           PRINTF("Open file failed.\r\n");
      }
      else
      {
         PRINTF("File created.\r\n");
         File_State = OPEN;
      }
   }
}

void Close_Log_File(void)
{
   if (CLOSED != File_State)
   {
      if (pdTRUE == xSemaphoreTake(File_Access_Semaphore, portMAX_DELAY))
      {
         if (f_close(&File_Object))
         {
            PRINTF("\r\nFailed to close file.\r\n");
         }
         else
         {
            PRINTF("\r\nFile closed.\r\n");
            File_State = CLOSED;
         }
      }
   }
}

static void SD_Card_Detect_Call_Back(bool is_inserted, void *userData)
{
   Card_Insert_Status = is_inserted;
   xSemaphoreGiveFromISR(Card_Detect_Semaphore, NULL);
}
