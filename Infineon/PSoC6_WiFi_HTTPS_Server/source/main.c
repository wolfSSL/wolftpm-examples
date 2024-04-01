/******************************************************************************
* File Name: main.c
*
* Description:
*   This is the main application file which initializes the BSP and starts
*   the RTOS scheduler. It starts a task that connects to the Wi-Fi Access
*   Point, starts the mDNS (multicast DNS) and then starts the HTTP server
*   in secure mode. All the HTTP security keys are configured in the file
*   secure_http_server.h using openSSL utility. See README.md to understand
*   how the security keys are generated.
*
* Related Document: See README.md
*
*******************************************************************************
* Copyright 2020-2023, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/* Header file includes */

#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "secure_http_server.h"
#include <FreeRTOS.h>
#include <task.h>

/* Include serial flash library and QSPI memory configurations only for the
 * kits that require the Wi-Fi firmware to be loaded in external QSPI NOR flash.
 */
#if defined(CY_ENABLE_XIP_PROGRAM)
#include "cy_serial_flash_qspi.h"
#include "cycfg_qspi_memslot.h"
#endif

#include <wolftpm/tpm2_wrap.h>
#include <hal/tpm_io.h>

/*****************************************************************************
* Macros
******************************************************************************/
/* RTOS related macros. */
#define HTTPS_SERVER_TASK_STACK_SIZE        (5 * 1024)
#define HTTPS_SERVER_TASK_PRIORITY          (1)

#define TPM2_I2C_HZ   400000UL /* 400kHz */

/*******************************************************************************
* Global Variables
********************************************************************************/
/* HTTPS server task handle. */
TaskHandle_t https_server_task_handle;

cyhal_i2c_t mI2C;
WOLFTPM2_DEV mDev;

static const char* TPM2_IFX_GetOpModeStr(int opMode)
{
    const char* opModeStr = "Unknown";
    switch (opMode) {
        case 0x00:
            opModeStr = "Normal TPM operational mode";
            break;
        case 0x01:
            opModeStr = "TPM firmware update mode (abandon possible)";
            break;
        case 0x02:
            opModeStr = "TPM firmware update mode (abandon not possible)";
            break;
        case 0x03:
            opModeStr = "After successful update, but before finalize";
            break;
        case 0x04:
            opModeStr = "After finalize or abandon, reboot required";
            break;
        default:
            break;
    }
    return opModeStr;
}

static int TPM2_IFX_PrintInfo(WOLFTPM2_DEV* dev)
{
    int rc;
    WOLFTPM2_CAPS caps;
    rc = wolfTPM2_GetCapabilities(dev, &caps);
    if (rc == TPM_RC_SUCCESS) {
        printf("Mfg %s (%d), Vendor %s, Fw %u.%u (0x%x)\n",
            caps.mfgStr, caps.mfg, caps.vendorStr, caps.fwVerMajor,
            caps.fwVerMinor, caps.fwVerVendor);
        printf("Operational mode: %s (0x%x)\n",
            TPM2_IFX_GetOpModeStr(caps.opMode), caps.opMode);
        printf("KeyGroupId 0x%x, FwCounter %d (%d same)\n",
            caps.keyGroupId, caps.fwCounter, caps.fwCounterSame);
        if (caps.keyGroupId == 0) {
            printf("Error getting key group id from TPM!\n");
            rc = -1;
        }
    }
    return rc;
}


/*******************************************************************************
 * Function Name: main
 *******************************************************************************
 * Summary:
 *  Entry function for the application.
 *  This function initializes the BSP, UART port for debugging, initializes the
 *  user LED on the kit, and starts the RTOS scheduler.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  int: Should never return.
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the Board Support Package (BSP) */
    result = cybsp_init();

    CHECK_RESULT(result);

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize retarget-io to use the debug UART port */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);

    /* Initialize the User LED. */
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_BIDIRECTIONAL, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    /* Init QSPI and enable XIP to get the Wi-Fi firmware from the QSPI NOR flash */
    #if defined(CY_ENABLE_XIP_PROGRAM)
        const uint32_t bus_frequency = 50000000lu;

        cy_serial_flash_qspi_init(smifMemConfigs[0], CYBSP_QSPI_D0, CYBSP_QSPI_D1,
                                      CYBSP_QSPI_D2, CYBSP_QSPI_D3, NC, NC, NC, NC,
                                      CYBSP_QSPI_SCK, CYBSP_QSPI_SS, bus_frequency);

        cy_serial_flash_qspi_enable_xip(true);
    #endif

    /* \x1b[2J\x1b[;H - ANSI ESC sequence to clear screen */
    APP_INFO(("\x1b[2J\x1b[;H"));

    /* Get TPM information */
    {
        cyhal_i2c_cfg_t i2c_cfg;
        memset(&i2c_cfg, 0, sizeof(i2c_cfg));
        i2c_cfg.frequencyhal_hz = TPM2_I2C_HZ;
        result = cyhal_i2c_init(&mI2C, CYBSP_I2C_SDA, CYBSP_I2C_SCL, NULL);
        if (result == CY_RSLT_SUCCESS) {
            result = cyhal_i2c_configure(&mI2C, &i2c_cfg);
        }


        APP_INFO(("===================================\n"));
        APP_INFO(("Infineon TPM Info\n"));
        APP_INFO(("===================================\n\n"));

        int rc = wolfTPM2_Init(&mDev, TPM2_IoCb, &mI2C);
        if (rc == TPM_RC_SUCCESS) {
            rc = TPM2_IFX_PrintInfo(&mDev);
        }
        else {
            printf("Infineon get information failed 0x%x: %s\n",
                rc, TPM2_GetRCString(rc));
        }
    }


    APP_INFO(("===================================\n"));
    APP_INFO(("HTTPS Server\n"));
    APP_INFO(("===================================\n\n"));

    /* Starts the HTTPS server in secure mode. */
    xTaskCreate(https_server_task, "HTTPS Server", HTTPS_SERVER_TASK_STACK_SIZE, NULL,
                HTTPS_SERVER_TASK_PRIORITY, &https_server_task_handle);

    /* Start the FreeRTOS scheduler */
    vTaskStartScheduler();

    /* Should never get here */
    CY_ASSERT(0);
}
/* [] END OF FILE */