/******************************************************************************
* File Name: secure_http_server.c
*
* Description: This file contains the necessary functions to start the HTTPS
* server and processes GET, POST, and PUT request from the HTTPS client.
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

/* Header file includes */
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

/* Cypress Secure Sockets header file */
#include "cy_secure_sockets.h"
#include "cy_tls.h"

/* Wi-Fi connection manager header files */
#include "cy_wcm.h"
#include "cy_wcm_error.h"

/* Standard C header file */
 #define _GNU_SOURCE
#include <string.h>
extern void *memmem(const void* haystack, size_t haystacklen,
                    const void* needle, size_t needlelen);

/* HTTPS server task header file. */
#include "secure_http_server.h"
#include "cy_http_server.h"
#include "secure_keys.h"

/* MDNS responder header file */
#include "mdns.h"

/* TPM */
#include <wolftpm/tpm2_wrap.h>
extern WOLFTPM2_DEV mDev;

/*******************************************************************************
* Global Variables
********************************************************************************/
/* Holds the IP address obtained using Wi-Fi Connection Manager (WCM). */
static cy_wcm_ip_address_t ip_addr;

/* Secure HTTP server instance. */
static cy_http_server_t https_server;

/* Wi-Fi network interface. */
static cy_network_interface_t nw_interface;

/* Holds the IP address and port number details of the socket for the HTTPS server. */
static cy_socket_sockaddr_t https_ip_address;

#ifdef HTTPS_PORT
/* Holds the security configuration such as server certificate,
 * server key, and rootCA.
 */
static cy_https_server_security_info_t security_config;
#endif

/* Holds the response handler for HTTPS GET and POST request from the client. */
static cy_resource_dynamic_data_t https_get_post_resource;

/* Holds the user data which adds/updates the URL data resources. */
static cy_resource_dynamic_data_t https_put_resource;

/* Global variable to track number of resources registered. */
static uint32_t number_of_resources_registered = 0;

/* Holds all the URL resources along with its data. */
typedef struct
{
    char *resource_name;
    char *value;
    uint32_t length;
} https_url_database_t;

/* Initializes the URL database with first entry reserved for HTTPS root URL.
 * The default value of MAX_NUMBER_OF_HTTP_SERVER_RESOURCES is 10 as defined
 * by the HTTPS server middleware. This can be overriden by setting its value
 * in the application Makefile. Refer to README.md for details.
 */
static https_url_database_t url_resources_db[MAX_NUMBER_OF_HTTP_SERVER_RESOURCES] =
{
    /* First entry in the URL database is reserved for HTTPS Root URL. */
    { (char *)"/", NULL, 0 }
};


#define MAX_FIRMWARE_MANIFEST_SZ 4096

#ifndef HTTP_SERVER_MTU_SIZE
#define HTTP_SERVER_MTU_SIZE             (1460)
#endif

#define FW_UPDATE_TASK_STACK_SIZE        (5 * 1024)
#define FW_UPDATE_TASK_PRIORITY          (1)

#define IFX_FW_MAX_CHUNK_SZ 1024

static TaskHandle_t fw_update_task_handle = NULL;

typedef struct FirmwareChunk {
    uint32_t sz;
    uint8_t  buf[IFX_FW_MAX_CHUNK_SZ];
} FirmwareChunk_t;

typedef enum {
    FW_STATE_INIT,
    FW_STATE_MANIFEST_START,
    FW_STATE_MANIFEST_DONE,
    FW_STATE_FIRMWARE_DATA_START,
    FW_STATE_FIRMWARE_DATA_CHUNK,
    FW_STATE_FIRMWARE_DONE,
    FW_STATE_FIRMWARE_REST
} FwState;

typedef enum {
    FW_STATE_THREAD_INIT,
    FW_STATE_THREAD_STARTED,
    FW_STATE_THREAD_READY,
    FW_STATE_THREAD_DONE,
    FW_STATE_THREAD_FAILED
} FwThreadState;

typedef struct {
    FwState state;
    FwThreadState threadState;
    int           threadRc;
    TaskHandle_t  notifyHandle;
    uint8_t manifest[MAX_FIRMWARE_MANIFEST_SZ];
    size_t  manifestSz;
    size_t  firmwareSz;
    FirmwareChunk_t chunk;

    char boundary[64];
    char fieldName[64];
    char fileName[64];
} fw_info_t;
static fw_info_t mFwInfo;


/******************************************************************************
* Function Prototypes
*******************************************************************************/
static cy_rslt_t configure_https_server(void);
void print_heap_usage(char *msg);
extern const char* TPM2_IFX_GetInfo(int firstCall);



/* Company Logo */
#define INFINEON_LOGO \
    "<img style=\"float: right;\" alt=\"logo.png\" "\
    "src=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAATkAAABcBAMAAADngd+fAA" \
           "AAD1BMVEX///8VWJbiOlVtkLHIxNm177myAAAEAklEQVR4AezBgQAAAACAoP2pF6kCA" \
           "ABmxgxw29VhMO4WHwArHABlOQBpOIAJvv+ZnmPolnbbwn9Dev2EPNJK3m+fHZPRlmRf" \
           "JMKvh7aRwSvKXAsMryg0NnhFdcmKyvCSIupf1DgVErnzjJMkp8KJ0p2WLJJqhtPkR6L" \
           "hPfvfPOxIdSZe9iHS9JGdV/5Lj+xiOEVX7z2851qIkHr4rRbadFqrKNz4sYpOkAY4qD" \
           "XNX1unmk6zDtM7nZLR9Fu6hehU89CGcEc1HcJhurdHOqrFp1gXoKL7sqp80LuObPNHR" \
           "0V9NQHQgsZ6MohmbnYdG10XOxr0xiVIvV6JGGAx2LKjMUp0LbqF9nXcS4tEk+aO/aI3" \
           "EPdA5dcwLORwbhYWNjpSleiAesvRW5OzfQP4ZaOvt4f0kRKY5iIquEpAQJPeKKSuNjr" \
           "LrmGeWoUdKzrWON/pBriQUF8cUS6rVcM7EqlWUrp4VZSZeHErcVkxDSJIcxw6DY3RlS" \
           "u6qSPWfGB0rlyLK2vSq99iw7tHYdn9SJqIYOn1Pk5ArAGQ4OI6/XRoth3c6RgrumFHW" \
           "4Z9qfa1vENhqNT1G91C7hMdd4Xucozu8kHXP9E5pOkrOkxpnW+3Oa1p5R2HXA27FiIQ" \
           "TT5A3Ohwo7PJgJbuD96RaqNzn+lu6e2W0k1/vFUDZVLsaaMnoysf9+pdAbMudPr9Qlz" \
           "2EP8zXX+ULhU85dPr3TvTYMs4CG90qLHes7rEYl6n4U/eORH5wbtk3qW3is7U0YQidz" \
           "ogLnRDAes1p5tA1xYODONvvTNU+IYOtO20qKkInuimZLbvfWd0pSPjFDWzAVsu1NCiG" \
           "3/cs6qDe7aic3C50yFtdD0UusXy2EQZOge6bM278L13eiHf593UnHdIpr2IVDx/ors4" \
           "8647Nu/s/PS9dxcb55FWmo54B7RrMhct21LT9epX9ayQOLQPUOO3dLg/z4jgEF2kTfv" \
           "N9pyt6dC+stCDudtsvHCn657oLDvrmmZoVbY+fA5I1RmlriwSG123obsjR2OGbyWsAe" \
           "Xw+c7ES32++1JoyaEpr4J/kDTPxhOg6az/K0Y4rEczuvRIt9JjtbIwewx5CygIeQyIE" \
           "vLoOaCwHDHvMB6OT5V9Si8fAlXAEHzmMAbPOWD2cMV8zXqFEHL2oxwxz/NBuAA/a42b" \
           "Ukyg8tkrXQjjqDBidNd89Uo3+qx0DE3lw3jY7FBMd7GlDlLolEV8HtU29c5flVKCmoq" \
           "Z4Tw8+1sP4vHZ72SbxhlcG68UVgBOxWvZlw8PHhGBc3X1JvneNx/+75faX79zx/wC75" \
           "SNwRSC4MYlcv/I1i/C96QALyH8ClDgdYTy4Jr81x4cEwAAACAMsn9qM+wHlgEAAADAA" \
           "To83zvHyP+JAAAAAElFTkSuQmCC\" />"

#define HTTPS_STARTUP_HEADER \
"<!DOCTYPE html>" \
"<html>" \
"<head>" \
"<title>Infineon TPM Firmware Update Demo</title>" \
"</head>" \
"<body>" \
"<h1 style=\"text-align: left\">Infineon TPM Firmware Update Demo" \
INFINEON_LOGO \
"</h1>" \
"<p><span style=\"font-size: 12pt;\"><strong>Infineon</strong> is the first TPM vendor to <strong>open source their " \
"firmware update procedure and process</strong> in their latest <strong>Infineon SLB9672 (SPI) and SLB9673 (I2C)</strong> versions of the TPM 2.0 module.</span></p>" \
"<p><span style=\"font-size: 12pt;\"><strong>wolfTPM</strong> is the only library to offer integrated support for updating TPM firmware.</span></p>" \
"<p><span style=\"text-decoration: underline;\"><span style=\"font-size: 12pt;\">Demo Platform:</span></span></p>" \
"<ul>" \
"    <li><span style=\"font-size: 12pt;\">Infineon PSoC 62S2 evaluation kit (Wifi)</span></li>" \
"    <li><span style=\"font-size: 12pt;\">Infineon SLB9373 (I2C) TPM 2.0 mikroBUS module</span></li>" \
"    <li><span style=\"font-size: 12pt;\">Modus Toolbox Wi-Fi-HTTPS-Server demo</span>" \
"    <ul>" \
"        <li><span style=\"font-size: 12pt;\">wolfSSL TLS v1.3 server</span></li>" \
"        <li><span style=\"font-size: 12pt;\">wolfTPM</span></li>" \
"    </ul>" \
"    </li>" \
"</ul>" \
"<h2>TPM Module Interface</h2>" \
"<form method=\"get\">" \
"<fieldset>" \
"    <legend>Firmware Status</legend>" \
"    <input type=\"submit\" value=\"Refresh TPM\"/>" \
"    <textarea id=\"tpm_status\" name=\"tpm_status\" rows=\"4\" cols=\"60\">"

#define HTTPS_STARTUP_FOOTER \
"    </textarea>" \
"</fieldset>" \
"</form>" \
"<form method=\"post\" enctype=\"multipart/form-data\">" \
"<fieldset>" \
"    <legend>Firmware Update</legend>" \
"    <p>" \
"        <label for=\"manifest\">Manifest File:</label>" \
"        <input type=\"file\" name=\"manifest\" value=\"Manifest File\"/></br></br>" \
"    </p>" \
"    <p>" \
"        <label for=\"data\">Firmware File:</label>" \
"        <input type=\"file\" name=\"data\" value=\"Firmware File\"/>" \
"    </p>" \
"    <input type=\"submit\" name=\"submit\" value=\"Update Firmware\"/>" \
"</fieldset>" \
"</form>" \
"</body>" \
"</html>"


/* Local Functions */
static int TPM2_IFX_FwData_Cb(uint8_t* data, uint32_t data_req_sz,
    uint32_t offset, void* cb_ctx)
{
    fw_info_t* fwInfo = (fw_info_t*)cb_ctx;
    FirmwareChunk_t* fwChunk = NULL;

    fwInfo->threadState = FW_STATE_THREAD_READY;

#if 0 /* test mode */
    do {
        int i;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        fwChunk = &fwInfo->chunk;

        if (data_req_sz > fwChunk->sz) {
            data_req_sz = fwChunk->sz;
        }
        if (data_req_sz > 0) {
            XMEMCPY(data, fwChunk->buf, data_req_sz);
            fwInfo->firmwareSz += data_req_sz;
        }

        printf("Chunk %d (total %d): ", (int)fwChunk->sz, fwInfo->firmwareSz);
        for (i=0; i<(int)fwChunk->sz; i+=4) {
            printf("%02x %02x %02x %02x ",
                fwChunk->buf[i],   fwChunk->buf[i+1],
                fwChunk->buf[i+2], fwChunk->buf[i+3]);
        }
        printf("\r\n");

        xTaskNotifyGive(fwInfo->notifyHandle);
    } while (fwChunk->sz > 0);
    return 0;
#else

    /* wait for chunk */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    fwChunk = &fwInfo->chunk;

    /* Process chunks */
    if (data_req_sz > fwChunk->sz) {
        data_req_sz = fwChunk->sz;
    }
    if (data_req_sz > 0) {
        XMEMCPY(data, fwChunk->buf, data_req_sz);
        fwInfo->firmwareSz += data_req_sz;
    }
    xTaskNotifyGive(fwInfo->notifyHandle);

    return data_req_sz;
#endif
}

static void fw_update_task(void *arg)
{
    int rc;
    fw_info_t* fwInfo = (fw_info_t*)arg;

    /* task has started */
    fwInfo->threadState = FW_STATE_THREAD_STARTED;
    fwInfo->threadRc = 0;

    /* start the update process */
    rc = wolfTPM2_FirmwareUpgrade(&mDev,
        fwInfo->manifest, fwInfo->manifestSz,
        TPM2_IFX_FwData_Cb, fwInfo);
    if (rc != 0) {
        printf("Infineon firmware update failed 0x%x: %s\n",
            rc, TPM2_GetRCString(rc));
        /* task failed */
        fwInfo->threadState = FW_STATE_THREAD_FAILED;
        fwInfo->threadRc = rc;
    }
    else {
        printf("Infineon firmware update success!\n");
        fwInfo->threadState = FW_STATE_THREAD_DONE;
        fwInfo->threadRc = rc;
    }

    vTaskDelete(NULL); /* done with task */
    fw_update_task_handle = NULL;
}

static char* parse_http_multipart_post(const char* header, char* boundary, char* fieldName, char* fileName)
{
    char* found = NULL, *start, *end = NULL;
    size_t sz;
    const char* boundaryStr = "------WebKitFormBoundary";
    const char* contentDisp = "Content-Disposition: form-data;";
    const char* nameStr = "name=\"";
    const char* filenameStr = "filename=\"";
    const char* streamStr = "Content-Type: application/octet-stream\r\n\r\n";

    /* get boundary */
    start = strstr(header, boundaryStr);
    if (start != NULL) {
        end = strstr(start, "\r\n");
        if (end != NULL) {
            sz = end-start;
            if (sz > 64-1)
                sz = 64-1;
            memcpy(boundary, start, sz);
            boundary[sz] = '\0'; /* null term */
        }
    }

    if (end != NULL) {
        /* find Content-Disposition */
        start = strstr(end, contentDisp);
        if (start != NULL) {
            start += strlen(contentDisp);
            /* find name=" " */
            start = strstr(start, nameStr);
            if (start != NULL) {
                start += strlen(nameStr);
                end = strstr(start, "\"");
                if (end != NULL) {
                    sz = end-start;
                    if (sz > 64-1)
                        sz = 64-1;
                    memcpy(fieldName, start, sz);
                    fieldName[sz] = '\0'; /* null term */
                }
            }
        }
    }

    if (end != NULL) {
        /* find filename */
        start = strstr(end, filenameStr);
        if (start != NULL) {
            start += strlen(filenameStr);
            end = strstr(start, "\"");
            if (end != NULL) {
                sz = end-start;
                if (sz > 64-1)
                    sz = 64-1;
                memcpy(fileName, start, sz);
                fileName[sz] = '\0'; /* null term */
            }
        }
    }

    if (end != NULL) {
        /* locate start of data stream and return pointer to it */
        found = strstr(end, streamStr);
        if (found != NULL) {
            found += strlen(streamStr);
        }
    }
    return found;
}

/*******************************************************************************
 * Function Name: dynamic_resource_handler
 *******************************************************************************
 * Summary:
 *  Handles HTTPS GET, POST, and PUT requests from the client.
 *  HTTPS GET sends the current CYBSP_USER_LED status as a response to the client.
 *  HTTPS POST toggles the CYBSP_USER_LED and then sends its current state as a
 *  response to the client.
 *  HTTPS PUT sends an error message as a response to the client if the resource
 *  registration is unsuccessful.
 *
 * Parameters:
 *  url_path - Pointer to the HTTPS URL path.
 *  url_parameters - Pointer to the HTTPS URL query string.
 *  stream - Pointer to the HTTPS response stream.
 *  arg - Pointer to the argument passed during HTTPS resource registration.
 *  https_message_body - Pointer to the HTTPS data from the client.
 *
 * Return:
 *  int32_t - Returns HTTPS_REQUEST_HANDLE_SUCCESS if the request from the client
 *  was handled successfully. Otherwise, it returns HTTPS_REQUEST_HANDLE_ERROR.
 *
 *******************************************************************************/
int32_t dynamic_resource_handler(const char* url_path,
                                 const char* url_parameters,
                                 cy_http_response_stream_t* stream,
                                 void* arg,
                                 cy_http_message_body_t* https_message_body)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int32_t status = HTTPS_REQUEST_HANDLE_SUCCESS;
    char err_buf[128];
    const char* msg;
    size_t offset = 0;

    switch (https_message_body->request_type)
    {
        case CY_HTTP_REQUEST_GET:
            APP_INFO(("Received HTTPS GET request.\n"));

            /* Toggle the user LED. */
            cyhal_gpio_toggle(CYBSP_USER_LED);

            /* Send the HTTPS response. */
            msg = HTTPS_STARTUP_HEADER;
            result = cy_http_server_response_stream_write_payload(stream,
                msg, strlen(msg));
            if (CY_RSLT_SUCCESS == result) {
                msg = TPM2_IFX_GetInfo(0);
                result = cy_http_server_response_stream_write_payload(stream,
                    msg, strlen(msg));
            }
            if (CY_RSLT_SUCCESS == result) {
                msg = HTTPS_STARTUP_FOOTER;
                result = cy_http_server_response_stream_write_payload(stream,
                    msg, strlen(msg));
            }
            if (CY_RSLT_SUCCESS != result) {
                ERR_INFO(("Failed to send the HTTPS GET response.\n"));
            }
            break;

        case CY_HTTP_REQUEST_POST:
            APP_INFO(("Received HTTPS POST request.\n"));

        #if 1
            printf("https_message_body->data_length %d, remain %lu, chunked %d\n",
                https_message_body->data_length,
                https_message_body->data_remaining,
                https_message_body->is_chunked_transfer
            );
        #endif
        #if 0
            if (https_message_body->data_length > 0) {
                int i;
                printf("HTTP Data:\r\n");
                for (i=0; i<https_message_body->data_length; i++) {
                    printf("%02x ", https_message_body->data[i]);
                }
                printf("\r\n");
            }
        #endif

            /* Toggle the user LED. */
            cyhal_gpio_toggle(CYBSP_USER_LED);

            /* State Machine */
            switch (mFwInfo.state) {
                case FW_STATE_INIT:
                    /* parser manifest file */
                    memset(&mFwInfo, 0, sizeof(mFwInfo));
                    msg = parse_http_multipart_post((char*)https_message_body->data, mFwInfo.boundary, mFwInfo.fieldName, mFwInfo.fileName);
                    if (msg != NULL) {
                        printf("POST: Field: %s, File %s, Boundary %s\n", mFwInfo.fieldName, mFwInfo.fileName, mFwInfo.boundary);
                        if (strcmp(mFwInfo.fieldName, "manifest") != 0) {
                            printf("error: field not \"manifest\"\n");
                            break;
                        }
                        offset = msg - (char*)https_message_body->data;
                        if (offset > https_message_body->data_length)
                            offset = https_message_body->data_length;
                        mFwInfo.manifestSz = https_message_body->data_length - offset;
                        if (mFwInfo.manifestSz > sizeof(mFwInfo.manifest))
                            mFwInfo.manifestSz = sizeof(mFwInfo.manifest);
                        memcpy(mFwInfo.manifest, msg, mFwInfo.manifestSz);
                        mFwInfo.state = FW_STATE_MANIFEST_START;
                        break; /* get more data - TODO: Check for end boundary */
                    }
                    else {
                        printf("error - post not valid / found\n");
                        break;
                    }
                    /* fall-through */
                case FW_STATE_MANIFEST_START:
                    /* find end of boundary */
                    msg = memmem(https_message_body->data, https_message_body->data_length,
                        mFwInfo.boundary, strlen(mFwInfo.boundary));
                    if (msg != NULL) {
                        /* found end of stream */
                        msg -= 2; /* backup \r\n */
                        offset = msg - (char*)https_message_body->data;
                        if (offset > https_message_body->data_length)
                            offset = https_message_body->data_length;
                        if (mFwInfo.manifestSz + offset < sizeof(mFwInfo.manifest)) {
                            memcpy(&mFwInfo.manifest[mFwInfo.manifestSz], https_message_body->data, offset);
                            mFwInfo.manifestSz += offset;

                            /* copy remainder into firmware chunk */
                            mFwInfo.chunk.sz = https_message_body->data_length - offset;
                            memcpy(mFwInfo.chunk.buf, &https_message_body->data[offset], mFwInfo.chunk.sz);

                            mFwInfo.state = FW_STATE_MANIFEST_DONE;
                        }
                        else {
                            printf("error: manifest end overrun\n");
                            break;
                        }
                    }
                    else {
                        /* copy all data */
                        if (mFwInfo.manifestSz + https_message_body->data_length < sizeof(mFwInfo.manifest)) {
                            memcpy(&mFwInfo.manifest[mFwInfo.manifestSz], https_message_body->data, https_message_body->data_length);
                            mFwInfo.manifestSz += https_message_body->data_length;
                        }
                        else {
                            printf("error: manifest middle overrun\n");
                        }
                        break;
                    }
                    /* fall-through */

                case FW_STATE_MANIFEST_DONE:
                    printf("Manifest data received: %d bytes\n", mFwInfo.manifestSz);

                    /* get the current thread for fw data notifications */
                    mFwInfo.notifyHandle = xTaskGetCurrentTaskHandle();

                    /* start thread */
                    xTaskCreate(fw_update_task, "FW Update", FW_UPDATE_TASK_STACK_SIZE,
                        &mFwInfo, FW_UPDATE_TASK_PRIORITY, &fw_update_task_handle);
                    /* wait for task to mark state as "ready" */
                    while (mFwInfo.threadState != FW_STATE_THREAD_READY &&
                           mFwInfo.threadState != FW_STATE_THREAD_FAILED) {
                        vTaskDelay(1);
                    }
                    if (mFwInfo.threadState != FW_STATE_THREAD_READY) {
                        printf("Thread Firmware Update Failed! %d\n", mFwInfo.threadRc);
                        snprintf(err_buf, sizeof(err_buf), "Update failed 0x%x: %s",
                            mFwInfo.threadRc, TPM2_GetRCString(mFwInfo.threadRc));
                        result = cy_http_server_response_stream_write_payload(stream, err_buf, strlen(err_buf));
                        mFwInfo.state = FW_STATE_INIT;
                        return HTTPS_REQUEST_HANDLE_ERROR;
                    }

                    mFwInfo.state = FW_STATE_FIRMWARE_DATA_START;
                    /* fall-through */

                case FW_STATE_FIRMWARE_DATA_START:
                    /* parse for firmware file */
                    memset(mFwInfo.boundary, 0, sizeof(mFwInfo.boundary));
                    memset(mFwInfo.fieldName, 0, sizeof(mFwInfo.fieldName));
                    memset(mFwInfo.fileName, 0, sizeof(mFwInfo.fileName));
                    msg = parse_http_multipart_post((char*)mFwInfo.chunk.buf, mFwInfo.boundary, mFwInfo.fieldName, mFwInfo.fileName);
                    if (msg != NULL) {
                        printf("POST: Field: %s, File %s, Boundary %s\n", mFwInfo.fieldName, mFwInfo.fileName, mFwInfo.boundary);
                        if (strcmp(mFwInfo.fieldName, "data") != 0) {
                            printf("error: field not \"data\"\n");
                            break;
                        }

                        /* copy firmware data info first chunk */
                        offset = (size_t)msg - (size_t)mFwInfo.chunk.buf;
                        memcpy(mFwInfo.chunk.buf, msg, mFwInfo.chunk.sz - offset);
                        mFwInfo.chunk.sz -= offset;

                        mFwInfo.state = FW_STATE_FIRMWARE_DATA_CHUNK;
                    }
                    else {
                        printf("error: firmware post not found\n");
                    }
                    break;

                case FW_STATE_FIRMWARE_DATA_CHUNK:
                    /* firmware data */
                    /* try and fill chunk */
                    offset = https_message_body->data_length;
                    if (offset > sizeof(mFwInfo.chunk.buf) - mFwInfo.chunk.sz)
                        offset = sizeof(mFwInfo.chunk.buf) - mFwInfo.chunk.sz;
                    memcpy(&mFwInfo.chunk.buf[mFwInfo.chunk.sz], https_message_body->data, offset);
                    mFwInfo.chunk.sz += offset;

                    /* find end of boundary */
                    msg = memmem(mFwInfo.chunk.buf, mFwInfo.chunk.sz,
                        mFwInfo.boundary, strlen(mFwInfo.boundary));
                    if (msg != NULL) {
                        /* found end of stream */
                        msg -= 2; /* backup \r\n */
                        mFwInfo.chunk.sz = (size_t)msg - (size_t)mFwInfo.chunk.buf;

                        /* sent last chunk */
                        printf("Sent last chunk: offset %d, data len %d\n", offset, https_message_body->data_length);
                        xTaskNotifyGive(fw_update_task_handle);
                        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                        mFwInfo.state = FW_STATE_FIRMWARE_DONE;
                    }
                    else {
                        if (mFwInfo.chunk.sz == sizeof(mFwInfo.chunk.buf)) {
                            printf("Sent chunk: offset %d, data len %d\n", offset, https_message_body->data_length);
                            xTaskNotifyGive(fw_update_task_handle);
                            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                            mFwInfo.chunk.sz = 0;
                        }
                        else {
                            break; /* keep reading data */
                        }
                    }

                    /* copy remainder */
                    do {
                        mFwInfo.chunk.sz = https_message_body->data_length - offset;
                        if (mFwInfo.chunk.sz > sizeof(mFwInfo.chunk.buf))
                            mFwInfo.chunk.sz = sizeof(mFwInfo.chunk.buf);
                        memcpy(mFwInfo.chunk.buf, &https_message_body->data[offset], mFwInfo.chunk.sz);
                        offset += mFwInfo.chunk.sz;
                        if (mFwInfo.chunk.sz == sizeof(mFwInfo.chunk.buf)) {
                            printf("Sent chunk: offset %d, data len %d\n", offset, https_message_body->data_length);
                            xTaskNotifyGive(fw_update_task_handle);
                            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                            mFwInfo.chunk.sz = 0;
                        }
                        else {
                            /* leave remainder */
                            break;
                        }
                    } while (offset < https_message_body->data_length);

                    if (mFwInfo.state != FW_STATE_FIRMWARE_DONE) {
                        break;
                    }
                    /* fall-through */

                case FW_STATE_FIRMWARE_DONE:
                    printf("Firmware data received: %d bytes\n", mFwInfo.firmwareSz);
                    /* send last 0 byte chunk to finalize */
                    mFwInfo.chunk.sz = 0;
                    xTaskNotifyGive(fw_update_task_handle);
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

                    /* wait for task to complete */
                    while (mFwInfo.threadState != FW_STATE_THREAD_DONE &&
                           mFwInfo.threadState != FW_STATE_THREAD_FAILED) {
                        vTaskDelay(1);
                    }
                    break;
                case FW_STATE_FIRMWARE_REST:
                    printf("Reset device\n");
                    break;
            }

            if (https_message_body->data_remaining == 0) {
                /* Send the HTTPS response. */
                msg = HTTPS_STARTUP_HEADER;
                result = cy_http_server_response_stream_write_payload(stream,
                    msg, strlen(msg));
                if (CY_RSLT_SUCCESS == result) {
                    snprintf(err_buf, sizeof(err_buf), "Update result 0x%x: %s",
                            mFwInfo.threadRc, TPM2_GetRCString(mFwInfo.threadRc));
                    msg = err_buf;
                    result = cy_http_server_response_stream_write_payload(stream,
                        msg, strlen(msg));
                }
                if (CY_RSLT_SUCCESS == result) {
                    msg = HTTPS_STARTUP_FOOTER;
                    result = cy_http_server_response_stream_write_payload(stream,
                        msg, strlen(msg));
                }
                if (CY_RSLT_SUCCESS != result) {
                    ERR_INFO(("Failed to send the HTTPS POST response.\n"));
                }

                mFwInfo.state = FW_STATE_INIT; /* reset state */
            }
            break;

        case CY_HTTP_REQUEST_PUT:

            /* Send the HTTPS error response. */
            const char* putErrStr = "HTTP PUT not supported";
            result = cy_http_server_response_stream_write_payload(stream,
                putErrStr, strlen(putErrStr));
            status = HTTPS_REQUEST_HANDLE_ERROR;
            if (CY_RSLT_SUCCESS != result)
            {
                ERR_INFO(("Failed to send the HTTPS PUT error response.\n"));
            }
            break;

        default:
            ERR_INFO(("Received invalid HTTP request method. Supported HTTP methods are GET, POST, and PUT.\n"));
            break;
    }

    if (CY_RSLT_SUCCESS != result)
    {
        status = HTTPS_REQUEST_HANDLE_ERROR;
    }

    print_heap_usage("At the end of GET/POST/PUT request handler");

    return status;
}

/*******************************************************************************
 * Function Name: https_put_resource_handler
 *******************************************************************************
 * Summary:
 *  Handles HTTPS GET request for the newly created resource by the client
 *  through HTTPS PUT request. It returns the value registered with the requested
 *  resource.
 *
 * Parameters:
 *  url_path - Pointer to the HTTPS URL path.
 *  url_parameters - Pointer to the HTTPS URL query string.
 *  stream - Pointer to the HTTPS response stream.
 *  arg - Pointer to the argument passed during HTTPS resource registration.
 *  https_message_body - Pointer to the HTTPS data from the client.
 *
 * Return:
 *  int32_t - Returns HTTPS_REQUEST_HANDLE_SUCCESS if the request from the client
 *  was handled successfully. Otherwise, it returns HTTPS_REQUEST_HANDLE_ERROR.
 *
 *******************************************************************************/
int32_t https_put_resource_handler(const char *url_path,
                                   const char *url_parameters,
                                   cy_http_response_stream_t *stream,
                                   void *arg,
                                   cy_http_message_body_t *https_message_body)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int32_t status = HTTPS_REQUEST_HANDLE_SUCCESS;
    uint32_t index = 0;

    if (CY_HTTP_REQUEST_GET == https_message_body->request_type)
    {
        APP_INFO(("Received HTTPS GET request.\n"));

        while (index < MAX_NUMBER_OF_HTTP_SERVER_RESOURCES)
        {
            if (strcmp(url_resources_db[index].resource_name, (char *)arg) == 0)
            {
                result = cy_http_server_response_stream_write_payload(stream,
                                               url_resources_db[index].value,
                                             url_resources_db[index].length);
                break;
            }

            /* Get next entry. */
            index++;
        }
    }

    if (CY_RSLT_SUCCESS != result)
    {
        ERR_INFO(("Failed to send the response message.\n"));
        status = HTTPS_REQUEST_HANDLE_ERROR;
    }

    return status;
}

/*******************************************************************************
 * Function Name: register_https_resource
 *******************************************************************************
 * Summary:
 *  Registers/Updates the new resource with the HTTPS server when HTTPS PUT
 *  request is received from the client.
 *
 * Parameters:
 *  register_resource_name: Pointer to the resource name to be registered
 *   with the HTTPS server.
 *
 * Return:
 *  None
 *
 *******************************************************************************/
void register_https_resource(char *register_resource_name)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    char *url_resource_data = NULL;
    uint32_t index = 0;

    APP_INFO(("New resource to create: %s\n", register_resource_name));

    /* Configure dynamic resource handler. */
    https_put_resource.resource_handler = https_put_resource_handler;
    https_put_resource.arg = NULL;

    /* Split the URL resource name and data from the HTTPS PUT request. */
    strtok_r(register_resource_name, "=", &url_resource_data);
    APP_INFO(("New URL: %s, Response text: %s\n", register_resource_name, url_resource_data));

    /* Register the resource if it does not exist in the URL database or
     * update the resource data if the requested resource already
     * exists in the URL database.
     */
    while (index < MAX_NUMBER_OF_HTTP_SERVER_RESOURCES)
    {
        if ((NULL != url_resources_db[index].resource_name) &&
             strcmp(url_resources_db[index].resource_name, register_resource_name) == 0)
        {
            APP_INFO(("Updating the existing resource: %s\n\n", register_resource_name));
            url_resources_db[index].length = strlen(url_resource_data);
            url_resources_db[index].value = (char *)realloc(url_resources_db[index].value, (url_resources_db[index].length + 1));
            memset(url_resources_db[index].value, 0, (url_resources_db[index].length + 1));
            memcpy(url_resources_db[index].value, url_resource_data, url_resources_db[index].length);
            https_put_resource.arg = url_resources_db[index].resource_name;
            break;
        }
        else if (NULL == url_resources_db[index].resource_name)
        {
            APP_INFO(("Registering the new resource: %s\n\n", register_resource_name));
            url_resources_db[index].value = (char *)calloc(strlen(url_resource_data), sizeof(char));
            url_resources_db[index].resource_name = (char *)calloc(strlen(register_resource_name), sizeof(char));

            if ((NULL == url_resources_db[index].resource_name) ||
                (NULL == url_resources_db[index].value))
            {
                 ERR_INFO(("Failed to allocate memory for URL resources.\n"));
                 CY_ASSERT(0);
            }

            /* Add the resource name, data, and data length into URL database. */
            url_resources_db[index].length = strlen(url_resource_data);
            memcpy(url_resources_db[index].resource_name, register_resource_name,
                                                 strlen(register_resource_name));
            memcpy(url_resources_db[index].value, url_resource_data,
                                                 url_resources_db[index].length);
            https_put_resource.arg = url_resources_db[index].resource_name;

            /* Register the new/updated resource with HTTPS server. */
            result = cy_http_server_register_resource(https_server,
                                                      (uint8_t*)url_resources_db[index].resource_name,
                                                      (uint8_t*)"text/html",
                                                      CY_DYNAMIC_URL_CONTENT,
                                                      &https_put_resource);
            PRINT_AND_ASSERT(result, "Failed to register a new resource.\n");

            /* Update the resource count. */
            number_of_resources_registered++;
            break;
        }
        else
        {
            /* Get next resource. */
            index++;
        }
    }

    if (index >= MAX_NUMBER_OF_HTTP_SERVER_RESOURCES)
    {
        ERR_INFO(("Requested resource not registered/updated. Reached Maximum "
                  "allowed number of resource registration: %d\n", MAX_NUMBER_OF_HTTP_SERVER_RESOURCES));
    }

    /* Clear the request buffer. */
    //memset(resource_name, 0, sizeof(resource_name));
}

/*******************************************************************************
 * Function Name: configure_https_server
 *******************************************************************************
 * Summary:
 *  Configures the security parameters such as server certificate, private key,
 *  and the root CA certificate to start the HTTP server in secure mode. By default,
 *  it registers a dynamic URL handler to handle the HTTPS GET, POST, and PUT
 *  requests.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Returns CY_RSLT_SUCCESS if the secure HTTP server is configured
 *  successfully, otherwise, it returns CY_RSLT_TYPE_ERROR.
 *
 *******************************************************************************/
static cy_rslt_t configure_https_server(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

#ifdef HTTPS_PORT
    /*
     * Setup the necessary security configuration required for establishing
     * secure HTTP connection with the secure HTTP client. This example uses
     * a self-signed certificate and is not trusted by the browser. Browsers
     * trust only the certificates signed by a CA (certificate authority).
     * To make the browser trust the connection with this HTTP server, user
     * needs to load the server certificate into the browser.
     */
    security_config.certificate                = (uint8_t *)keySERVER_CERTIFICATE_PEM;
    security_config.certificate_length         = strlen(keySERVER_CERTIFICATE_PEM);
    security_config.private_key                = (uint8_t *)keySERVER_PRIVATE_KEY_PEM;
    security_config.key_length                 = strlen(keySERVER_PRIVATE_KEY_PEM);
    security_config.root_ca_certificate        = (uint8_t *)keyCLIENT_ROOTCA_PEM;
    security_config.root_ca_certificate_length = strlen(keyCLIENT_ROOTCA_PEM);
#endif

    /* IP address of server. */
    https_ip_address.ip_address.ip.v4 = ip_addr.ip.v4;
    https_ip_address.ip_address.version = CY_SOCKET_IP_VER_V4;

    /* Add IP address information to network interface object. */
    nw_interface.object  = (void *) &https_ip_address;
    nw_interface.type = CY_NW_INF_TYPE_WIFI;

    /* Initialize secure socket library. */
    result = cy_http_server_network_init();

    /* Allocate memory needed for secure HTTP server. */
#ifdef HTTPS_PORT
    result = cy_http_server_create(&nw_interface, HTTPS_PORT, MAX_SOCKETS, &security_config, &https_server);
#else
    result = cy_http_server_create(&nw_interface, HTTP_PORT, MAX_SOCKETS, NULL, &https_server);
#endif
    PRINT_AND_ASSERT(result, "Failed to allocate memory for the HTTPS server.\n");

    /* Configure dynamic resource handler. */
    https_get_post_resource.resource_handler = dynamic_resource_handler;
    https_get_post_resource.arg = NULL;

    /* Register all the resources with the secure HTTP server. */
    result = cy_http_server_register_resource(https_server,
                                              (uint8_t*)"/",
                                              (uint8_t*)"text/html",
                                              CY_DYNAMIC_URL_CONTENT,
                                              &https_get_post_resource);
    /* Update the resource count. */
    number_of_resources_registered++;

    return result;
}

/*******************************************************************************
 * Function Name: https_server_task
 *******************************************************************************
 * Summary:
 *  Starts the HTTP server in secure mode. This example application is using a
 *  self-signed certificate which means there is no third-party certificate issuing
 *  authority involved in the authentication of the server. It is the user's
 *  responsibility to supply the necessary security configurations such as server's
 *  certificate, private key of the server, and RootCA of the server to start the
 *  HTTP server in secure mode.
 *
 * Parameters:
 *  arg - Unused.
 *
 * Return:
 *  None.
 *
 *******************************************************************************/
void https_server_task(void *arg)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    (void)arg;

    /* Connects to the Wi-Fi Access Point. */
    result = wifi_connect();
    PRINT_AND_ASSERT(result, "Wi-Fi connection failed.\n");

#if LWIP_MDNS_RESPONDER
    /* Resolves the HTTPS server name to an IP address. */
    result = mdns_responder_start();
    PRINT_AND_ASSERT(result, "Failed to start MDNS responder.\n");
#endif

    /* Configure the HTTPS server with all the security parameters and
     * register a default dynamic URL handler.
     */
    result = configure_https_server();
    PRINT_AND_ASSERT(result, "Failed to configure the HTTPS server.\n");

    /* Start the HTTPS server. */
    result = cy_http_server_start(https_server);
    PRINT_AND_ASSERT(result, "Failed to start the HTTPS server.\n");

#ifdef HTTPS_PORT
    APP_INFO(("HTTPS server has successfully started. The server is running at "
              "URL https://%s.local:%d\n\n", HTTPS_SERVER_NAME, HTTPS_PORT));
#else
    APP_INFO(("HTTPS server has successfully started. The server is running at "
            "URL http://%s.local:%d\n\n", HTTPS_SERVER_NAME, HTTP_PORT));
#endif

    /* continue running HTTP server (handled through callbacks) */
    while(true)
    {
        vTaskDelay(10000/portTICK_PERIOD_MS);
    }
}

#if LWIP_MDNS_RESPONDER
/********************************************************************************
 * Function Name: mdns_responder_start
 ********************************************************************************
 * Summary:
 * Starts the mDNS responder using lwIP network stack APIs. It resolves the IP address
 * for a given hostname and sends the DNS response to the DNS client.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Returns CY_RSLT_SUCCESS if the MDNS has started successfully.
 *
 *******************************************************************************/
cy_rslt_t mdns_responder_start(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Start lwIP's MDNS responder.
     * MDNS is required to resolve the local domain name of the secure HTTP server.
     */
    struct netif *net = cy_network_get_nw_interface(CY_NETWORK_WIFI_STA_INTERFACE, 0);

    mdns_resp_init();

    if (ERR_OK != mdns_resp_add_netif(net, HTTPS_SERVER_NAME, MDNS_TTL_SECONDS))
    {
        ERR_INFO(("Failed to start the MDNS responder.\n"));
        result = CY_RSLT_TYPE_ERROR;
    }

    return result;
}
#endif /* #if LWIP_MDNS_RESPONDER */

/********************************************************************************
 * Function Name: wifi_connect
 ********************************************************************************
 * Summary:
 *  The device associates to the Access Point with given SSID, PASSWORD, and SECURITY
 *  type. It retries for MAX_WIFI_RETRY_COUNT times if the Wi-Fi connection fails.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Returns CY_RSLT_SUCCESS if the Wi-Fi connection is successfully
 *  established, a WCM error code otherwise.
 *
 *******************************************************************************/
cy_rslt_t wifi_connect(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t retry_count = 0;
    cy_wcm_connect_params_t connect_param = {0};
    cy_wcm_config_t wcm_config = {.interface = CY_WCM_INTERFACE_TYPE_STA};

    result = cy_wcm_init(&wcm_config);

    if (CY_RSLT_SUCCESS == result)
    {
        APP_INFO(("Wi-Fi initialization is successful\n"));
        memcpy(&connect_param.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
        memcpy(&connect_param.ap_credentials.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
        connect_param.ap_credentials.security = WIFI_SECURITY_TYPE;
        APP_INFO(("Join to AP: %s\n", connect_param.ap_credentials.SSID));

        /*
         * Connect to Access Point. It validates the connection parameters
         * and then establishes connection to AP.
         */
        for (retry_count = 0; retry_count < MAX_WIFI_RETRY_COUNT; retry_count++)
        {
             result = cy_wcm_connect_ap(&connect_param, &ip_addr);

             if (CY_RSLT_SUCCESS == result)
             {
                 APP_INFO(("Successfully joined Wi-Fi network %s\n", connect_param.ap_credentials.SSID));

                 if (CY_WCM_IP_VER_V4 == ip_addr.version)
                 {
                     APP_INFO(("Assigned IP address: %s\n", ip4addr_ntoa((const ip4_addr_t *)&ip_addr.ip.v4)));
                 }
                 else if (CY_WCM_IP_VER_V6 == ip_addr.version)
                 {
                     APP_INFO(("Assigned IP address: %s\n", ip6addr_ntoa((const ip6_addr_t *)&ip_addr.ip.v6)));
                 }

                 break;
             }

             ERR_INFO(("Failed to join Wi-Fi network. Retrying...\n"));
        }
    }

    return result;
}


/* [] END OF FILE */

