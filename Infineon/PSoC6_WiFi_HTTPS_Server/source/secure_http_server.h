/******************************************************************************
* File Name: secure_http_server.h
*
* Description: This file contains configuration parameters for the secure HTTP
* server along with the HTML page that the server will host.
*
* Related Document: See README.md
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


/*******************************************************************************
* Include guard
*******************************************************************************/
#ifndef SECURE_HTTP_SERVER_H_
#define SECURE_HTTP_SERVER_H_

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>
#include "cy_wcm.h"
#include "cybsp.h"
#include "cy_network_mw_core.h"
#include "cyhal_gpio.h"

/* Wi-Fi Credentials: Modify WIFI_SSID and WIFI_PASSWORD to match your Wi-Fi network
 * Credentials.
 */
#define WIFI_SSID                               "WIFI_SSID"
#define WIFI_PASSWORD                           "WIFI_PASSWORD"

/* Security type of the Wi-Fi access point. See 'cy_wcm_security_t' structure
 * in "cy_wcm.h" for more details.
 */
#define WIFI_SECURITY_TYPE                       CY_WCM_SECURITY_WPA2_AES_PSK

#define MAX_WIFI_RETRY_COUNT                     (3)

#define CHECK_RESULT(x)                          do { if (CY_RSLT_SUCCESS != x) { CY_ASSERT(0); } } while(0);
#define APP_INFO(x)                              do { printf("Info: "); printf x; } while(0);
#define ERR_INFO(x)                              do { printf("Error: "); printf x; } while(0);

#define ERR_INFO_MDNS(eval, msg, ret)            if ((eval))           \
                                                 {                     \
                                                      ERR_INFO((msg)); \
                                                      return (ret);    \
                                                 }

#define PRINT_AND_ASSERT(result, msg, args...)   do                                 \
                                                 {                                  \
                                                     if (CY_RSLT_SUCCESS != result) \
                                                     {                              \
                                                         ERR_INFO((msg, ## args));  \
                                                         CY_ASSERT(0);              \
                                                     }                              \
                                                 } while(0);

#define HTTPS_SERVER_NAME                        "mysecurehttpserver"
#define MDNS_TTL_SECONDS                         (255)
#define LED_STATUS_ON                            "ON"
#define LED_STATUS_OFF                           "OFF"
#define MAX_LED_STATUS_LENGTH                    (3)
#define HTTPS_PORT                               (50007)
#define MAX_SOCKETS                              (2)
#define HTTPS_GET_RESPONSE                       "Hello - Message from HTTPS server!"
#define REGISTER_RESOURCE_TASK_STACK_SIZE        (1024)
#define REGISTER_RESOURCE_TASK_PRIORITY          (tskIDLE_PRIORITY)
#define REGISTER_RESOURCE_QUEUE_LENGTH           (1)
#define NEW_RESOURCE_NAME_LENGTH                 (30)
#define HTTPS_RESOURCE_PUT_ERROR                 "Maximum resource name length is %d characters."
#define HTTPS_REQUEST_HANDLE_SUCCESS             (0)
#define HTTPS_REQUEST_HANDLE_ERROR               (-1)

#define HTTPS_STARTUP_WEBPAGE \
              "<!DOCTYPE html>" \
              "<html>" \
              "<head>" \
              "<title>HTTPS Server Demo</title>" \
              "</head>" \
              "<body>" \
                "<h1>HTTPS Server Demo</h1>" \
                "<form method=\"get\">" \
                    "<fieldset>" \
                        "<legend>HTTPS GET</legend>" \
                        "<input type=\"submit\" value=\"Get LED status\"/>" \
                        "<input type=\"text\" name=\"led_status\" value=\"%s\" size=\"3\" readonly/></br></br>" \
                    "</fieldset>" \
                    "</br>" \
                "</form>" \
                "<form method=\"post\">" \
                    "<fieldset>" \
                        "<legend>HTTPS POST</legend>" \
                        "<input type=\"submit\" name=\"toggle_led\" value=\"Toggle LED\"/></br></br>" \
                    "</fieldset>" \
                    "</br>" \
                "</form>" \
              "</body>" \
              "</html>"

#define MAX_HTTP_RESPONSE_LENGTH                 (sizeof(HTTPS_STARTUP_WEBPAGE) + sizeof(MAX_LED_STATUS_LENGTH) + 1)

/*******************************************************************************
* Function Prototypes
********************************************************************************/
void https_server_task(void *arg);
cy_rslt_t wifi_connect(void);
cy_rslt_t mdns_responder_start(void);

#endif /* SECURE_HTTP_SERVER_H_ */


/* [] END OF FILE */

