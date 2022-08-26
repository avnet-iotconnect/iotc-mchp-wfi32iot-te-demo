/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/
#include <stdio.h>

#include "nx_api.h"
#include "nx_azure_iot_hub_client.h"
#include "nx_azure_iot_hub_client_properties.h"
#include "nx_azure_iot_provisioning_client.h"

/* These are sample files, user can build their own certificate and ciphersuites.  */
#include "nx_azure_iot_cert.h"
#include "nx_azure_iot_ciphersuites.h"
#include "sample_config.h"

/* Definitions and function prototypes required by the application */
#include "app.h"

/* Maximum number of characters in a telemetry message */
#define TELEMETRY_MSGLEN_MAX 90

/* Define Azure RTOS TLS info.  */
static NX_SECURE_X509_CERT root_ca_cert;
static NX_SECURE_X509_CERT root_ca_cert_2;
static NX_SECURE_X509_CERT root_ca_cert_3;
static UCHAR nx_azure_iot_tls_metadata_buffer[NX_AZURE_IOT_TLS_METADATA_BUFFER_SIZE];
static ULONG nx_azure_iot_thread_stack[NX_AZURE_IOT_STACK_SIZE / sizeof(ULONG)];

/* Define the prototypes for AZ IoT.  */
static NX_AZURE_IOT                                 nx_azure_iot;

volatile uint32_t AZ_telemetryInterval = AZ_TELEMETRYINTERVAL_DEFAULT;
volatile uint32_t AZ_systemRebootTimer = 0;

/* External variables used by the application  */
extern APP_CONNECT_STATUS appConnectStatus;
extern APP_SENSORS_DATA APP_SENSORS_data;
extern APP_LED_CTRL appLedCtrl[APP_LED_TOTAL];

// define the modelID associated with device template and the dps payload
#define SAMPLE_PNP_MODEL_ID         "dtmi:com:Microchip:WFI32_IoT_WM;2"
#define SAMPLE_PNP_DPS_PAYLOAD      "{\"modelId\":\"" SAMPLE_PNP_MODEL_ID "\"}"

/* Generally, IoTHub Client and DPS Client do not run at the same time, user can use union as below to
   share the memory between IoTHub Client and DPS Client.

   NOTE: If user can not make sure sharing memory is safe, IoTHub Client and DPS Client must be defined seperately.  */
typedef union SAMPLE_CLIENT_UNION
{
    NX_AZURE_IOT_HUB_CLIENT                         iothub_client;

#ifdef ENABLE_DPS_SAMPLE
    NX_AZURE_IOT_PROVISIONING_CLIENT                prov_client;
#endif /* ENABLE_DPS_SAMPLE */

} SAMPLE_CLIENT;

static SAMPLE_CLIENT                                client;

#define iothub_client client.iothub_client
#ifdef ENABLE_DPS_SAMPLE
#define prov_client client.prov_client
#endif /* ENABLE_DPS_SAMPLE */
 
/* Using X509 certificate authenticate to connect to IoT Hub,
   set the device certificate as your device.  */
#if (USE_DEVICE_CERTIFICATE == 1)
extern const UCHAR sample_device_cert_ptr[];
extern const UINT sample_device_cert_len;
extern const UCHAR sample_device_private_key_ptr[];
extern const UINT sample_device_private_key_len;
NX_SECURE_X509_CERT device_certificate;
#endif /* USE_DEVICE_CERTIFICATE */

/* Define buffer for IoTHub info.  */
#ifdef ENABLE_DPS_SAMPLE
static UCHAR sample_iothub_hostname[SAMPLE_MAX_BUFFER];
static UCHAR sample_iothub_device_id[SAMPLE_MAX_BUFFER];
#endif /* ENABLE_DPS_SAMPLE */

/* Define sample threads.  */
#ifndef DISABLE_TELEMETRY_SAMPLE
#ifdef CLICK_ULTRALOWPRESS
extern ultralowpress_return_value_t ULTRALOWPRESS_status;
#endif /* CLICK_ULTRALOWPRESS */
#ifdef CLICK_VAVPRESS
extern vavpress_return_value_t VAVPRESS_status;
extern vavpress_sensor_param_data_t VAVPRESS_param_data;
extern vavpress_el_signature_data_t VAVPRESS_el_signature_data;
//static float VAVPRESS_pressure;
//static float VAVPRESS_temperature;
#endif /* CLICK_VAVPRESS */
static TX_THREAD sample_telemetry_thread;
static ULONG sample_telemetry_thread_stack[SAMPLE_STACK_SIZE / sizeof(ULONG)];
#endif /* DISABLE_TELEMETRY_SAMPLE */

#ifndef DISABLE_C2D_SAMPLE
static TX_THREAD sample_c2d_thread;
static ULONG sample_c2d_thread_stack[SAMPLE_STACK_SIZE / sizeof(ULONG)];
#endif /* DISABLE_C2D_SAMPLE */

#if !defined(DISABLE_TELEMETRY_SAMPLE) || !defined(DISABLE_C2D_SAMPLE)
/* Define sample properties.  */
static const CHAR *sample_properties[MAX_PROPERTY_COUNT][2] = {{"propertyA", "valueA"},
                                                               {"propertyB", "valueB"}};
#endif /* !defined(DISABLE_TELEMETRY_SAMPLE) && !defined(DISABLE_C2D_SAMPLE) */

// property names for LEDs
static const CHAR sample_prop_name_LED_blue[] = "led_b";
static const CHAR sample_prop_name_LED_green[] = "led_g";
static const CHAR sample_prop_name_LED_yellow[] = "led_y";
static const CHAR sample_prop_name_LED_red[] = "led_r";

#ifndef DISABLE_DIRECT_METHOD_SAMPLE
static CHAR method_response_payload[] = "{\"status\": \"OK\"}";
static TX_THREAD sample_direct_method_thread;
static ULONG sample_direct_method_thread_stack[SAMPLE_STACK_SIZE / sizeof(ULONG)];
az_result process_reboot_command(
    az_span   payload_span,
    az_span   response_span,
    az_span*  out_response_span,
    uint16_t* out_response_status);
#endif /* DISABLE_DIRECT_METHOD_SAMPLE */

#ifndef DISABLE_DEVICE_TWIN_SAMPLE
extern twin_properties_t twin_properties;
//static CHAR fixed_reported_properties[] = "{\"sample_report\": \"OK\"}";
static TX_THREAD sample_device_twin_thread;
static ULONG sample_device_twin_thread_stack[SAMPLE_STACK_SIZE / sizeof(ULONG)];
#endif /* DISABLE_DEVICE_TWIN_SAMPLE */

#ifndef DISABLE_APP_CTRL_SAMPLE
extern button_press_data_t button_press_data;
static TX_THREAD sample_app_ctrl_thread;
static ULONG sample_app_ctrl_thread_stack[SAMPLE_STACK_SIZE / sizeof(ULONG)];
#endif /* DISABLE_APP_CTRL_SAMPLE */

#ifndef DISABLE_PERIOD_TIMER_SAMPLE
static TX_THREAD sample_period_timer_thread;
static ULONG sample_period_timer_thread_stack[SAMPLE_STACK_SIZE / sizeof(ULONG)];
#endif /* DISABLE_PERIOD_TIMER_SAMPLE */

void sample_entry(NX_IP *ip_ptr, NX_PACKET_POOL *pool_ptr, NX_DNS *dns_ptr, UINT (*unix_time_callback)(ULONG *unix_time));
#ifdef ENABLE_DPS_SAMPLE
static UINT sample_dps_entry(UCHAR **iothub_hostname, UINT *iothub_hostname_length,
                             UCHAR **iothub_device_id, UINT *iothub_device_id_length);
#endif /* ENABLE_DPS_SAMPLE */

#ifndef DISABLE_TELEMETRY_SAMPLE
static void sample_telemetry_thread_entry(ULONG parameter);
#endif /* DISABLE_TELEMETRY_SAMPLE */

#ifndef DISABLE_C2D_SAMPLE
static void sample_c2d_thread_entry(ULONG parameter);
#endif /* DISABLE_C2D_SAMPLE */

#ifndef DISABLE_DIRECT_METHOD_SAMPLE
static void sample_direct_method_thread_entry(ULONG parameter);
#endif /* DISABLE_DIRECT_METHOD_SAMPLE */

#ifndef DISABLE_DEVICE_TWIN_SAMPLE
static void sample_device_twin_thread_entry(ULONG parameter);
#endif /* DISABLE_DEVICE_TWIN_SAMPLE */

#ifndef DISABLE_APP_CTRL_SAMPLE
static void sample_app_ctrl_thread_entry(ULONG parameter);
#endif /* DISABLE_APP_CTRL_SAMPLE */

#ifndef DISABLE_PERIOD_TIMER_SAMPLE
static void sample_period_timer_thread_entry(ULONG parameter);
#endif /* DISABLE_PERIOD_TIMER_SAMPLE */

static VOID printf_packet(NX_PACKET *packet_ptr)
{
    while (packet_ptr != NX_NULL)
    {
        printf("%.*s", (INT)(packet_ptr -> nx_packet_append_ptr - packet_ptr -> nx_packet_prepend_ptr),
               (CHAR *)packet_ptr -> nx_packet_prepend_ptr);
        packet_ptr = packet_ptr -> nx_packet_next;
    }
}
static VOID sprintf_packet(char* buf, NX_PACKET *packet_ptr)
{
    while (packet_ptr != NX_NULL)
    {
        sprintf(buf, "%.*s", (INT)(packet_ptr -> nx_packet_append_ptr - packet_ptr -> nx_packet_prepend_ptr),
               (CHAR *)packet_ptr -> nx_packet_prepend_ptr);
        packet_ptr = packet_ptr -> nx_packet_next;
    }
}
static bool find_property_value(char*buf, const char* param, char*retStr )
{
    /* ret true parameter found, false parameter not found
     *   if found, retStr will have resulting parameter string
     *   else retStr will not be modified
     */
    
    int rxCount=0;
    char * ptrInterval = strstr(buf, param);
    if (ptrInterval == 0)
    {
        return false;
    }
    else
    {
        ptrInterval += strlen(param)+1;
        while(*ptrInterval!=',' && *ptrInterval!='}')
        {
            retStr[rxCount++] = *ptrInterval++;
        }
        retStr[rxCount] = 0;
        return true;
    }
}

#define PROPERTY_TELEMETRY_INTERVAL "\"telemetryInterval\""
#define PROPERTY_LEDY "\"led_y\""
#define PROPERTY_VERSION_FIELD "\"$version\""

/* static int parse_packet_data(char* packetData, char* responseProperty, int responseSize)
 
   packetData is a JSON (input) character array from proper write
   responseProperty is the (output) char array to return the response data
   responseSize (input) is the size of responseProperty array
   property_version (output) is the version from the packet.
  
   returns responseLength (int), with the size of response.  Will return 0 if no data was found
 */
static int parse_packet_data(char* packetData, char* responseProperty, int responseSize)
{    
    bool bPropertyFound = false;
    char propertyValue[30];
    char tempStr[60];
    int responseLength = 0;
    
    memset(responseProperty, 0, responseSize);
    responseProperty[0] = '{';
    bPropertyFound = find_property_value(packetData, PROPERTY_TELEMETRY_INTERVAL, propertyValue);
    if(bPropertyFound == true)
    {
        //printf("%s = %s\r\n", PROPERTY_TELEMETRY_INTERVAL, propertyValue);
        AZ_telemetryInterval = atoi(propertyValue);
        sprintf(tempStr, "%s: %s", PROPERTY_TELEMETRY_INTERVAL, propertyValue);
        responseLength += strlen(tempStr);
        strcat(responseProperty, tempStr);            
    } 
    
    bPropertyFound = find_property_value(packetData, PROPERTY_LEDY, propertyValue);
    if(bPropertyFound == true)
    {
        if(responseLength>0)
        {
            strcat(responseProperty,", ");
        }
        //printf("%s = %s\r\n", PROPERTY_LEDY, propertyValue);
        appLedCtrl[APP_LED_YELLOW].mode = atoi(propertyValue);
        sprintf(tempStr, "%s: %s", PROPERTY_LEDY, propertyValue);
        responseLength += strlen(tempStr);
        strcat(responseProperty, tempStr);            
    } 
    

    strcat(responseProperty, "}");
    return strlen(responseProperty);
}
static void sample_reported_properties_send_action(NX_AZURE_IOT_HUB_CLIENT *hub_client_ptr)
{
UINT status = 0;
UINT response_status;
UINT request_id;
NX_AZURE_IOT_JSON_WRITER json_writer;
NX_PACKET *packet_ptr;
ULONG reported_property_version;

    if ((appConnectStatus.wifi == false) || (appConnectStatus.cloud == false)) 
    {
        return;
    }

    if ((status = nx_azure_iot_hub_client_reported_properties_create(hub_client_ptr,
                                                                     &packet_ptr, NX_WAIT_FOREVER)))
    {
        printf("Failed create reported properties: error code = 0x%08x\r\n", status);
        return;
    }
    
    if ((status = nx_azure_iot_json_writer_init(&json_writer, packet_ptr, NX_WAIT_FOREVER)))
    {
        printf("Failed init json writer: error code = 0x%08x\r\n", status);
        nx_packet_release(packet_ptr);
        return;
    }

    if ((status = nx_azure_iot_json_writer_append_begin_object(&json_writer)) ||
        (status = nx_azure_iot_json_writer_append_property_with_int32_value(&json_writer,
                                                                             (const UCHAR *)sample_prop_name_LED_blue,
                                                                             sizeof(sample_prop_name_LED_blue) - 1,
                                                                             appLedCtrl[APP_LED_BLUE].mode)) ||
        (status = nx_azure_iot_json_writer_append_property_with_int32_value(&json_writer,
                                                                             (const UCHAR *)sample_prop_name_LED_green,
                                                                             sizeof(sample_prop_name_LED_green) - 1,
                                                                             appLedCtrl[APP_LED_GREEN].mode)) ||
        (status = nx_azure_iot_json_writer_append_property_with_int32_value(&json_writer,
                                                                             (const UCHAR *)sample_prop_name_LED_yellow,
                                                                             sizeof(sample_prop_name_LED_yellow) - 1,
                                                                             appLedCtrl[APP_LED_YELLOW].mode)) ||
        (status = nx_azure_iot_json_writer_append_property_with_int32_value(&json_writer,
                                                                             (const UCHAR *)sample_prop_name_LED_red,
                                                                             sizeof(sample_prop_name_LED_red) - 1,
                                                                             appLedCtrl[APP_LED_RED].mode)) ||
        (status = nx_azure_iot_json_writer_append_end_object(&json_writer)))
    {
        printf("Build reported property failed: error code = 0x%08x\r\n", status);
        nx_packet_release(packet_ptr);
        return;
    }

    if ((status = nx_azure_iot_hub_client_reported_properties_send(hub_client_ptr,
                                                                   packet_ptr,
                                                                   &request_id, &response_status,
                                                                   &reported_property_version,
                                                                   (5 * NX_IP_PERIODIC_RATE))))
    {
        printf("Reported properties failed!: error code = 0x%08x\r\n", status);
        nx_packet_release(packet_ptr);
        return;
    }

    if ((response_status < 200) || (response_status >= 300))
    {
        printf("Reported properties failed with code : %d\r\n", response_status);
        return;
    }
}
static VOID connection_status_callback(NX_AZURE_IOT_HUB_CLIENT *hub_client_ptr, UINT status)
{
    NX_PARAMETER_NOT_USED(hub_client_ptr);
    if (status)
    {
        printf("Disconnected from Azure IoT Hub!!!: error code = 0x%08x\r\n", status);
        appConnectStatus.cloud = false;
    }
    else
    {
        printf("Connected to Azure IoT Hub!!!\r\n");
        appConnectStatus.cloud = true;
        LED_GREEN_On();
    }
}

static UINT sample_initialize_iothub(NX_AZURE_IOT_HUB_CLIENT *iothub_client_ptr)
{
UINT status;
#ifdef ENABLE_DPS_SAMPLE
UCHAR *iothub_hostname = NX_NULL;
UCHAR *iothub_device_id = NX_NULL;
UINT iothub_hostname_length = 0;
UINT iothub_device_id_length = 0;
#else
UCHAR *iothub_hostname = (UCHAR *)HOST_NAME;
UCHAR *iothub_device_id = (UCHAR *)DEVICE_ID;
UINT iothub_hostname_length = sizeof(HOST_NAME) - 1;
UINT iothub_device_id_length = sizeof(DEVICE_ID) - 1;
#endif /* ENABLE_DPS_SAMPLE */

#ifdef ENABLE_DPS_SAMPLE

    /* Run DPS.  */
    if ((status = sample_dps_entry(&iothub_hostname, &iothub_hostname_length,
                                   &iothub_device_id, &iothub_device_id_length)))
    {
        printf("Failed on sample_dps_entry!: error code = 0x%08x\r\n", status);
        return(status);
    }
#endif /* ENABLE_DPS_SAMPLE */

    printf("IoTHub Host Name: %.*s; Device ID: %.*s.\r\n",
           iothub_hostname_length, iothub_hostname, iothub_device_id_length, iothub_device_id);

    /* Initialize IoTHub client.  */
    if ((status = nx_azure_iot_hub_client_initialize(iothub_client_ptr, &nx_azure_iot,
                                                     iothub_hostname, iothub_hostname_length,
                                                     iothub_device_id, iothub_device_id_length,
                                                     (UCHAR *)MODULE_ID, sizeof(MODULE_ID) - 1,
                                                     _nx_azure_iot_tls_supported_crypto,
                                                     _nx_azure_iot_tls_supported_crypto_size,
                                                     _nx_azure_iot_tls_ciphersuite_map,
                                                     _nx_azure_iot_tls_ciphersuite_map_size,
                                                     nx_azure_iot_tls_metadata_buffer,
                                                     sizeof(nx_azure_iot_tls_metadata_buffer),
                                                     &root_ca_cert)))
    {
        printf("Failed on nx_azure_iot_hub_client_initialize!: error code = 0x%08x\r\n", status);
        return(status);
    }
    
/* Set the model id.  */
#ifdef SAMPLE_PNP_MODEL_ID
    if ((status = nx_azure_iot_hub_client_model_id_set(iothub_client_ptr,
                                                       (const UCHAR *)SAMPLE_PNP_MODEL_ID,
                                                       sizeof(SAMPLE_PNP_MODEL_ID) - 1)))
    {
        printf("Failed on nx_azure_iot_hub_client_model_id_set!: error code = 0x%08x\r\n", status);
    }
#endif
    
    /* Add more CA certificates.  */
    if ((status = nx_azure_iot_hub_client_trusted_cert_add(iothub_client_ptr, &root_ca_cert_2)))
    {
        printf("Failed on nx_azure_iot_hub_client_trusted_cert_add!: error code = 0x%08x\r\n", status);
    }
    else if ((status = nx_azure_iot_hub_client_trusted_cert_add(iothub_client_ptr, &root_ca_cert_3)))
    {
        printf("Failed on nx_azure_iot_hub_client_trusted_cert_add!: error code = 0x%08x\r\n", status);
    }

#if (USE_DEVICE_CERTIFICATE == 1)

    /* Initialize the device certificate.  */
    else if ((status = nx_secure_x509_certificate_initialize(&device_certificate,
                                                             (UCHAR *)sample_device_cert_ptr, (USHORT)sample_device_cert_len,
                                                             NX_NULL, 0,
                                                             (UCHAR *)sample_device_private_key_ptr, (USHORT)sample_device_private_key_len,
                                                             DEVICE_KEY_TYPE)))
    {
        printf("Failed on nx_secure_x509_certificate_initialize!: error code = 0x%08x\r\n", status);
    }

    /* Set device certificate.  */
    else if ((status = nx_azure_iot_hub_client_device_cert_set(iothub_client_ptr, &device_certificate)))
    {
        printf("Failed on nx_azure_iot_hub_client_device_cert_set!: error code = 0x%08x\r\n", status);
    }
#else

    /* Set symmetric key.  */
    else if ((status = nx_azure_iot_hub_client_symmetric_key_set(iothub_client_ptr,
                                                                 (UCHAR *)DEVICE_SYMMETRIC_KEY,
                                                                 strlen(DEVICE_SYMMETRIC_KEY))))
    {
        printf("Failed on nx_azure_iot_hub_client_symmetric_key_set!\r\n");
    }
#endif /* USE_DEVICE_CERTIFICATE */

    /* Set connection status callback.  */
    else if ((status = nx_azure_iot_hub_client_connection_status_callback_set(iothub_client_ptr,
                                                                              connection_status_callback)))
    {
        printf("Failed on connection_status_callback!\r\n");
    }    
#ifndef DISABLE_C2D_SAMPLE
    else if ((status = nx_azure_iot_hub_client_cloud_message_enable(iothub_client_ptr)))
    {
        printf("C2D receive enable failed!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_C2D_SAMPLE */
#ifndef DISABLE_DIRECT_METHOD_SAMPLE
    else if ((status = nx_azure_iot_hub_client_direct_method_enable(iothub_client_ptr)))
    {
        printf("Direct method receive enable failed!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_DIRECT_METHOD_SAMPLE */
#ifndef DISABLE_DEVICE_TWIN_SAMPLE
    else if ((status = nx_azure_iot_hub_client_device_twin_enable(iothub_client_ptr)))
    {
        printf("Device twin enabled failed!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_DEVICE_TWIN_SAMPLE */

    if (status)
    {
        nx_azure_iot_hub_client_deinitialize(iothub_client_ptr);
    }
    
    return(status);
}

static void log_callback(az_log_classification classification, UCHAR *msg, UINT msg_len)
{
    if (classification == AZ_LOG_IOT_AZURERTOS)
    {
        printf("%.*s", msg_len, (CHAR *)msg);
       printf("%.*s", msg_len, (CHAR *)msg);
    }
}

void sample_entry(NX_IP *ip_ptr, NX_PACKET_POOL *pool_ptr, NX_DNS *dns_ptr, UINT (*unix_time_callback)(ULONG *unix_time))
{
UINT status = 0;
UINT loop = NX_TRUE;

    nx_azure_iot_log_init(log_callback);

    /* Create Azure IoT handler.  */
    if ((status = nx_azure_iot_create(&nx_azure_iot, (UCHAR *)"Azure IoT", ip_ptr, pool_ptr, dns_ptr,
                                      nx_azure_iot_thread_stack, sizeof(nx_azure_iot_thread_stack),
                                      NX_AZURE_IOT_THREAD_PRIORITY, unix_time_callback)))
    {
        printf("Failed on nx_azure_iot_create!: error code = 0x%08x\r\n", status);
        return;
    }

    /* Initialize CA certificates.  */
    if ((status = nx_secure_x509_certificate_initialize(&root_ca_cert, (UCHAR *)_nx_azure_iot_root_cert,
                                                        (USHORT)_nx_azure_iot_root_cert_size,
                                                        NX_NULL, 0, NULL, 0, NX_SECURE_X509_KEY_TYPE_NONE)))
    {
        printf("Failed to initialize ROOT CA certificate!: error code = 0x%08x\r\n", status);
        nx_azure_iot_delete(&nx_azure_iot);
        return;
    }

    if ((status = nx_secure_x509_certificate_initialize(&root_ca_cert_2, (UCHAR *)_nx_azure_iot_root_cert_2,
                                                        (USHORT)_nx_azure_iot_root_cert_size_2,
                                                        NX_NULL, 0, NULL, 0, NX_SECURE_X509_KEY_TYPE_NONE)))
    {
        printf("Failed to initialize ROOT CA certificate!: error code = 0x%08x\r\n", status);
        nx_azure_iot_delete(&nx_azure_iot);
        return;
    }

    if ((status = nx_secure_x509_certificate_initialize(&root_ca_cert_3, (UCHAR *)_nx_azure_iot_root_cert_3,
                                                        (USHORT)_nx_azure_iot_root_cert_size_3,
                                                        NX_NULL, 0, NULL, 0, NX_SECURE_X509_KEY_TYPE_NONE)))
    {
        printf("Failed to initialize ROOT CA certificate!: error code = 0x%08x\r\n", status);
        nx_azure_iot_delete(&nx_azure_iot);
        return;
    }
    
    if ((status = sample_initialize_iothub(&iothub_client)))
    {
        printf("Failed to initialize iothub client: error code = 0x%08x\r\n", status);
        nx_azure_iot_delete(&nx_azure_iot);
        return;
    }

    if (nx_azure_iot_hub_client_connect(&iothub_client, NX_TRUE, NX_WAIT_FOREVER))
    {
        printf("Failed on nx_azure_iot_hub_client_connect!\r\n");
        nx_azure_iot_hub_client_deinitialize(&iothub_client);
        nx_azure_iot_delete(&nx_azure_iot);
        return;
    }

#ifndef DISABLE_TELEMETRY_SAMPLE

    /* Create Telemetry sample thread.  */
    if ((status = tx_thread_create(&sample_telemetry_thread, "Sample Telemetry Thread",
                                   sample_telemetry_thread_entry, 0,
                                   (UCHAR *)sample_telemetry_thread_stack, SAMPLE_STACK_SIZE,
                                   SAMPLE_THREAD_PRIORITY, SAMPLE_THREAD_PRIORITY,
                                   1, TX_AUTO_START)))
    {
        printf("Failed to create telemetry sample thread!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_TELEMETRY_SAMPLE */

#ifndef DISABLE_C2D_SAMPLE

    /* Create C2D sample thread.  */
    if ((status = tx_thread_create(&sample_c2d_thread, "Sample C2D Thread",
                                   sample_c2d_thread_entry, 0,
                                   (UCHAR *)sample_c2d_thread_stack, SAMPLE_STACK_SIZE,
                                   SAMPLE_THREAD_PRIORITY, SAMPLE_THREAD_PRIORITY,
                                   1, TX_AUTO_START)))
    {
        printf("Failed to create c2d sample thread!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_C2D_SAMPLE */

#ifndef DISABLE_DIRECT_METHOD_SAMPLE

    /* Create Direct Method sample thread.  */
    if ((status = tx_thread_create(&sample_direct_method_thread, "Sample Direct Method Thread",
                                   sample_direct_method_thread_entry, 0,
                                   (UCHAR *)sample_direct_method_thread_stack, SAMPLE_STACK_SIZE,
                                   SAMPLE_THREAD_PRIORITY, SAMPLE_THREAD_PRIORITY,
                                   1, TX_AUTO_START)))
    {
        printf("Failed to create direct method sample thread!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_DIRECT_METHOD_SAMPLE */

#ifndef DISABLE_DEVICE_TWIN_SAMPLE

    /* Create Device twin sample thread.  */
    if ((status = tx_thread_create(&sample_device_twin_thread, "Sample Device Twin Thread",
                                   sample_device_twin_thread_entry, 0,
                                   (UCHAR *)sample_device_twin_thread_stack, SAMPLE_STACK_SIZE,
                                   SAMPLE_THREAD_PRIORITY, SAMPLE_THREAD_PRIORITY,
                                   1, TX_AUTO_START)))
    {
        printf("Failed to create device twin sample thread!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_DEVICE_TWIN_SAMPLE */

#ifndef DISABLE_APP_CTRL_SAMPLE

    /* Create Device twin sample thread.  */
    if ((status = tx_thread_create(&sample_app_ctrl_thread, "Sample Application Control Thread",
                                   sample_app_ctrl_thread_entry, 0,
                                   (UCHAR *)sample_app_ctrl_thread_stack, SAMPLE_STACK_SIZE,
                                   SAMPLE_THREAD_PRIORITY, SAMPLE_THREAD_PRIORITY,
                                   1, TX_AUTO_START)))
    {
        printf("Failed to create application LED sample thread!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_APP_CTRL_SAMPLE */

#ifndef DISABLE_PERIOD_TIMER_SAMPLE

    /* Create Device twin sample thread.  */
    if ((status = tx_thread_create(&sample_period_timer_thread, "Sample Period Timer Thread",
                                   sample_period_timer_thread_entry, 0,
                                   (UCHAR *)sample_period_timer_thread_stack, SAMPLE_STACK_SIZE,
                                   SAMPLE_THREAD_PRIORITY, SAMPLE_THREAD_PRIORITY,
                                   1, TX_AUTO_START)))
    {
        printf("Failed to create period timer sample thread!: error code = 0x%08x\r\n", status);
    }
#endif /* DISABLE_PERIOD_TIMER_SAMPLE */
    
    /* Simply loop in sample.  */
    while (loop)
    {
        tx_thread_sleep(NX_IP_PERIODIC_RATE);
    }
}

#ifdef ENABLE_DPS_SAMPLE
static UINT sample_dps_entry(UCHAR **iothub_hostname, UINT *iothub_hostname_length,
                             UCHAR **iothub_device_id, UINT *iothub_device_id_length)
{
UINT status;

    printf("Start Provisioning Client...\r\n");
  
    /* Initialize IoT provisioning client.  */
    if ((status = nx_azure_iot_provisioning_client_initialize(&prov_client, &nx_azure_iot,
                                                              (UCHAR *)ENDPOINT, sizeof(ENDPOINT) - 1,
                                                              (UCHAR *)ID_SCOPE, strlen(ID_SCOPE),
                                                              (UCHAR *)REGISTRATION_ID, strlen(REGISTRATION_ID),
                                                              _nx_azure_iot_tls_supported_crypto,
                                                              _nx_azure_iot_tls_supported_crypto_size,
                                                              _nx_azure_iot_tls_ciphersuite_map,
                                                              _nx_azure_iot_tls_ciphersuite_map_size,
                                                              nx_azure_iot_tls_metadata_buffer,
                                                              sizeof(nx_azure_iot_tls_metadata_buffer),
                                                              &root_ca_cert)))
    {
        printf("Failed on nx_azure_iot_provisioning_client_initialize!: error code = 0x%08x\r\n", status);
        return(status);
    }
    
    /* Initialize length of hostname and device ID.  */
    *iothub_hostname_length = sizeof(sample_iothub_hostname);
    *iothub_device_id_length = sizeof(sample_iothub_device_id);

    /* Add more CA certificates.  */
    if ((status = nx_azure_iot_provisioning_client_trusted_cert_add(&prov_client, &root_ca_cert_2)))
    {
        printf("Failed on nx_azure_iot_provisioning_client_trusted_cert_add!: error code = 0x%08x\r\n", status);
    }
    else if ((status = nx_azure_iot_provisioning_client_trusted_cert_add(&prov_client, &root_ca_cert_3)))
    {
        printf("Failed on nx_azure_iot_provisioning_client_trusted_cert_add!: error code = 0x%08x\r\n", status);
    }

#if (USE_DEVICE_CERTIFICATE == 1)

    /* Initialize the device certificate.  */
    else if ((status = nx_secure_x509_certificate_initialize(&device_certificate, (UCHAR *)sample_device_cert_ptr, (USHORT)sample_device_cert_len, NX_NULL, 0,
                                                             (UCHAR *)sample_device_private_key_ptr, (USHORT)sample_device_private_key_len, DEVICE_KEY_TYPE)))
    {
        printf("Failed on nx_secure_x509_certificate_initialize!: error code = 0x%08x\r\n", status);
    }

    /* Set device certificate.  */
    else if ((status = nx_azure_iot_provisioning_client_device_cert_set(&prov_client, &device_certificate)))
    {
        printf("Failed on nx_azure_iot_provisioning_client_device_cert_set!: error code = 0x%08x\r\n", status);
    }
#else

    /* Set symmetric key.  */
    else if ((status = nx_azure_iot_provisioning_client_symmetric_key_set(&prov_client, (UCHAR *)DEVICE_SYMMETRIC_KEY,
                                                                          strlen(DEVICE_SYMMETRIC_KEY))))
    {
        printf("Failed on nx_azure_iot_hub_client_symmetric_key_set!: error code = 0x%08x\r\n", status);
    }
#endif /* USE_DEVICE_CERTIFICATE */
    
    /* send model ID to DPS if set*/
#ifdef SAMPLE_PNP_DPS_PAYLOAD
    else if ((status = nx_azure_iot_provisioning_client_registration_payload_set(&prov_client, (UCHAR *)SAMPLE_PNP_DPS_PAYLOAD,
                                                                                 sizeof(SAMPLE_PNP_DPS_PAYLOAD) - 1)))
    {
        printf("Failed on nx_azure_iot_provisioning_client_registration_payload_set!: error code = 0x%08x\r\n", status);
    }
#endif
    /* Register device */
    else if ((status = nx_azure_iot_provisioning_client_register(&prov_client, NX_WAIT_FOREVER)))
    {
        printf("Failed on nx_azure_iot_provisioning_client_register!: error code = 0x%08x\r\n", status);
    }

    /* Get Device info */
    else if ((status = nx_azure_iot_provisioning_client_iothub_device_info_get(&prov_client,
                                                                               sample_iothub_hostname, iothub_hostname_length,
                                                                               sample_iothub_device_id, iothub_device_id_length)))
    {
        printf("Failed on nx_azure_iot_provisioning_client_iothub_device_info_get!: error code = 0x%08x\r\n", status);
    }
    else
    {
        *iothub_hostname = sample_iothub_hostname;
        *iothub_device_id = sample_iothub_device_id;
        printf("Registered Device Successfully.\r\n");
    }

    /* Destroy Provisioning Client.  */
    nx_azure_iot_provisioning_client_deinitialize(&prov_client);

    return(status);
}
#endif /* ENABLE_DPS_SAMPLE */

#ifndef DISABLE_TELEMETRY_SAMPLE

void send_telemetry_message(ULONG parameter, UCHAR *message, UINT mesg_length)
{
    UINT status = 0;
    NX_PACKET *packet_ptr;

    NX_PARAMETER_NOT_USED(parameter);
    
    /* Create a telemetry message packet.  */
    if ((status = nx_azure_iot_hub_client_telemetry_message_create(&iothub_client, &packet_ptr, NX_WAIT_FOREVER)))
    {
        printf("Telemetry message create failed!: error code = 0x%08x\r\n", status);
        return;
    }

    /* Add properties to telemetry message.  */
    for (int index = 0; index < MAX_PROPERTY_COUNT; index++)
    {
        if ((status =
                nx_azure_iot_hub_client_telemetry_property_add(packet_ptr,
                                                               (UCHAR *)sample_properties[index][0],
                                                               (USHORT)strlen(sample_properties[index][0]),
                                                               (UCHAR *)sample_properties[index][1],
                                                               (USHORT)strlen(sample_properties[index][1]),
                                                               NX_WAIT_FOREVER)))
        {
            printf("Telemetry property add failed!: error code = 0x%08x\r\n", status);
            return;
        }
    }

    if (status)
    {
        nx_azure_iot_hub_client_telemetry_message_delete(packet_ptr);
        return;
    }
 
    if (nx_azure_iot_hub_client_telemetry_send(&iothub_client, packet_ptr,
                                               (UCHAR *)message, mesg_length, NX_WAIT_FOREVER))
    {
        printf("Telemetry message send failed!: error code = 0x%08x\r\n", status);
        nx_azure_iot_hub_client_telemetry_message_delete(packet_ptr);
        return;
    }
    printf("%s\r\n", message);   
}

void sample_telemetry_thread_entry(ULONG parameter)
{
    CHAR buffer[TELEMETRY_MSGLEN_MAX];
    UINT buffer_length;
    UCHAR loop = NX_TRUE;
    uint32_t SM8436_serialNumber;
    float temperature, pressure;    
    NX_PARAMETER_NOT_USED(parameter);

    APP_SENSORS_init();
    
    tx_thread_sleep(AZ_telemetryInterval * NX_IP_PERIODIC_RATE);

#ifdef CLICK_ULTRALOWPRESS
    printf("<ULP Click> Initializing SM8436...\r\n");
    SM8436_serialNumber = ULTRALOWPRESS_init();
    if (ULTRALOWPRESS_status == ULTRALOWPRESS_OK)
    {
        printf("<ULP Click> * Serial Number = %u\r\n", SM8436_serialNumber);    
    }
    else
    {
        printf("<ULP Click> SM8436 not detected\r\n");          
    }
    tx_thread_sleep(100);
#endif /* CLICK_ULTRALOWPRESS */

#ifdef CLICK_VAVPRESS
    printf("<VAV Click> Initializing LMIS025B...\r\n");
    VAVPRESS_init();
    if (VAVPRESS_status == VAVPRESS_OK)
    {
        printf("<VAV Click> Retrieving Electronic Signature Data...\r\n");
        tx_thread_sleep(100);
        printf("<VAV Click> * Part Number: %.11s\r\n", VAVPRESS_el_signature_data.part_number);
        tx_thread_sleep(100);
        printf("<VAV Click> * Firmware Version: %.3f\r\n", VAVPRESS_el_signature_data.firmware_version);
        tx_thread_sleep(100);
        printf("<VAV Click> * Pressure Range: %d Pa\r\n", VAVPRESS_el_signature_data.pressure_range);
        tx_thread_sleep(100);
        printf("<VAV Click> * Lot Number: %.7s\r\n", VAVPRESS_el_signature_data.lot_number);
        tx_thread_sleep(100);
        printf("<VAV Click> * Output Type: %c\r\n", VAVPRESS_el_signature_data.output_type);
        tx_thread_sleep(100);
        printf("<VAV Click> * Scale Factor: %d\r\n", VAVPRESS_el_signature_data.scale_factor);
        tx_thread_sleep(100);
        printf("<VAV Click> * Calibration ID: %.2s\r\n", VAVPRESS_el_signature_data.calibration_id);
        tx_thread_sleep(100);
        printf("<VAV Click> * Week Number: %d\r\n", VAVPRESS_el_signature_data.week_number);
        tx_thread_sleep(100);
        printf("<VAV Click> * Year Number: %d\r\n", VAVPRESS_el_signature_data.year_number);
        tx_thread_sleep(100);
        printf("<VAV Click> * Sequence Number: %d\r\n", VAVPRESS_el_signature_data.sequence_number);
    }
    else
    {
        printf("<VAV Click> LMIS025B not detected\r\n");          
    }
    tx_thread_sleep(100);
#endif /* CLICK_VAVPRESS */
        
    /* Loop to send telemetry messages */
    while (loop)
    {
#ifdef WFI32IOT_SENSORS
        //printf("\r\n<WFI32-IoT> Reading temperature & light sensors...\r\n");
        buffer_length = (UINT)snprintf(buffer, sizeof(buffer),
                "{\"WFI32IoT_temperature\": %u, \"WFI32IoT_light\": %u}",
                APP_SENSORS_readTemperature(), APP_SENSORS_readLight() );
        send_telemetry_message(parameter, (UCHAR *)buffer, buffer_length);
#endif /* WFI32IOT_SENSORS */
#ifdef CLICK_ULTRALOWPRESS
        if (ULTRALOWPRESS_status == ULTRALOWPRESS_OK)
        {
            if (ULTRALOWPRESS_isReady())
            {
                //printf("\r\n<ULP Click> STATUS [ %x ] ", APP_SENSORS_data.i2c.rxBuffer[0]);
                ULTRALOWPRESS_clearStatus();
                temperature = ULTRALOWPRESS_getTemperature();
                //printf("DSP_T [ %x ] ", APP_SENSORS_data.i2c.rxBuffer[0]);
                pressure = ULTRALOWPRESS_getPressure();     
                //printf("DSP_S [ %x ]\r\n", APP_SENSORS_data.i2c.rxBuffer[0]);
                buffer_length = (UINT)snprintf(buffer, sizeof(buffer),
                        "{\"ULP_temperature\": %.2f, \"ULP_pressure\": %.2f}",
                        temperature, pressure );                
                send_telemetry_message(parameter, (UCHAR *)buffer, buffer_length);
            }
            else
            {
                //printf("\r\n<ULP Click> SM8436 is not ready...\r\n");
            }
            //tx_thread_sleep(500);
        }
#endif /* CLICK_ULTRALOWPRESS */
#ifdef CLICK_VAVPRESS
        if (VAVPRESS_status == VAVPRESS_OK)
        {
            if (VAVPRESS_getSensorReadings(&VAVPRESS_param_data, &pressure, &temperature) == VAVPRESS_OK)
            {
                //printf("\r\n<VAV Click> Extended data readout ( pressure | temperature ) = [ %x (%i) | %x (%i) ]\r\n",
                        //APP_SENSORS_data.i2c.rxBuffer[0], VAVPRESS_2sCompToDecimal(APP_SENSORS_data.i2c.rxBuffer[0]),
                        //APP_SENSORS_data.i2c.rxBuffer[1], VAVPRESS_2sCompToDecimal(APP_SENSORS_data.i2c.rxBuffer[1])
                      //);
                //tx_thread_sleep(500);
                buffer_length = (UINT)snprintf(buffer, sizeof(buffer),
                        "{\"VAV_temperature\": %.2f, \"VAV_pressure\": %.2f}",
                        temperature, pressure);              
                send_telemetry_message(parameter, (UCHAR *)buffer, buffer_length);      
            }
        }
#endif /* CLICK_VAVPRESS */
#ifdef SEND_LED_PROPERTIES_WITH_TELEMETRY
        sample_reported_properties_send_action(&iothub_client);
#endif

        tx_thread_sleep(AZ_telemetryInterval * NX_IP_PERIODIC_RATE);
    }
}
#endif /* DISABLE_TELEMETRY_SAMPLE */

#ifndef DISABLE_C2D_SAMPLE
void sample_c2d_thread_entry(ULONG parameter)
{
    UCHAR loop = NX_TRUE;
    NX_PACKET *packet_ptr;
    UINT status = 0;
    USHORT property_buf_size;
    const UCHAR *property_buf;

    NX_PARAMETER_NOT_USED(parameter);

    /* Loop to receive c2d message.  */
    while (loop)
    {
        if ((status = nx_azure_iot_hub_client_cloud_message_receive(&iothub_client, &packet_ptr, NX_WAIT_FOREVER)))
        {
            printf("C2D receive failed!: error code = 0x%08x\r\n", status);
            break;
        }

        if ((status = nx_azure_iot_hub_client_cloud_message_property_get(&iothub_client, packet_ptr,
                                                                         (UCHAR *)sample_properties[0][0],
                                                                         (USHORT)strlen(sample_properties[0][0]),
                                                                         &property_buf, &property_buf_size)) == NX_AZURE_IOT_SUCCESS)
        {
            printf("Receive property: %s = %.*s\r\n", sample_properties[0][0],
                   (INT)property_buf_size, property_buf);
        }

        printf("Receive message: ");
        printf_packet(packet_ptr);
        printf("\r\n");

        nx_packet_release(packet_ptr);
    }
}
#endif /* DISABLE_C2D_SAMPLE */

#ifndef DISABLE_DIRECT_METHOD_SAMPLE

int reboot_command(char* payload)
{
    int payload_size = strlen(payload);
    int delay = 0;
    // example payload: "PT5S", quotes are transmitted.  
    // So 'P' is character 1 in string instead of 0 
    if(payload[1] == 'P' && payload[2] == 'T' && payload[payload_size-2] == 'S')
    {  // payload is expected format, convert delay to integer
        payload[payload_size-2] = 0;
        delay = (uint32_t) atoi(&payload[3]);
        payload[payload_size-2] = 'S';
        printf("Reboot in %d seconds...\r\n", delay);
    }
    else
    {
        printf("Invalid reboot payload, expected format \"PT5S\", for 5 second delay\r\n");
    }
    return delay;
}
void sendMsg_command(char* payload)
{
    #define PROPERTY_MSG_TEXT  "\"sendMsgString\""
    bool bPropertyFound;
    char propertyValue[64];
  
    bPropertyFound = find_property_value(payload, PROPERTY_MSG_TEXT, propertyValue);
    if(bPropertyFound == true)
    {
        printf("Message received from cloud: %s\r\n", PROPERTY_MSG_TEXT, propertyValue);
                    
    }
    else
    {
        printf("unexpected object data for sendMsg method\r\n");
    }
}
void sample_direct_method_thread_entry(ULONG parameter)
{
    UCHAR loop = NX_TRUE;
    NX_PACKET *packet_ptr;
    UINT status = 0;
    USHORT method_name_length;
    const UCHAR *method_name_ptr;
    USHORT context_length;
    VOID *context_ptr;
    char command[32];
    char payload[64];

    NX_PARAMETER_NOT_USED(parameter);

    /* Loop to receive direct method message.  */
    while (loop)
    {
        if ((status = nx_azure_iot_hub_client_direct_method_message_receive(&iothub_client,
                                                                            &method_name_ptr, &method_name_length,
                                                                            &context_ptr, &context_length,
                                                                            &packet_ptr, NX_WAIT_FOREVER)))
        {
            printf("Direct method receive failed!: error code = 0x%08x\r\n", status);
            break;
        }
        printf("Receive method call: %.*s, with payload:", (INT)method_name_length, (CHAR *)method_name_ptr);
        sprintf_packet(payload, packet_ptr);
        printf("%s\r\n", payload);

        memset(command, 0, sizeof(command));
        memcpy(command, method_name_ptr, method_name_length);

        if (strcmp(command, "reboot") == 0)
        {  // if command is reboot, process payload to gather delay
            AZ_systemRebootTimer = reboot_command(payload);
            if (AZ_systemRebootTimer == 0)
            {
                SYS_RESET_SoftwareReset();
            }
        }
        if (strcmp(command, "sendMsg") == 0)
        {  // if command is sendMsg, pull text from object and print to terminal
            sendMsg_command(payload);
        }
        if ((status = nx_azure_iot_hub_client_direct_method_message_response(&iothub_client, 200 /* method status */,
                                                                             context_ptr, context_length,
                                                                             (UCHAR *)method_response_payload, sizeof(method_response_payload) - 1,
                                                                             NX_WAIT_FOREVER)))
        {
            printf("Direct method response failed!: error code = 0x%08x\r\n", status);
            nx_packet_release(packet_ptr);
            break;
        }

        nx_packet_release(packet_ptr);
    }
}
#endif /* DISABLE_DIRECT_METHOD_SAMPLE */

#ifndef DISABLE_DEVICE_TWIN_SAMPLE
void sample_device_twin_thread_entry(ULONG parameter)
{
    UCHAR loop = NX_TRUE;
    NX_PACKET *packet_ptr;
    UINT status = 0;
    UINT response_status;
    UINT request_id;
    ULONG reported_property_version;
    char responseProperty[120];
    int responsePropertyLen;
    char receivedProperties[180];
    char propertyValue[30];
    
    bool bPropertyFound;

    NX_PARAMETER_NOT_USED(parameter);

    init_twin_data(&twin_properties);
    
    if ((status = nx_azure_iot_hub_client_device_twin_properties_request(&iothub_client, NX_WAIT_FOREVER)))
    {
        printf("device twin document request failed!: error code = 0x%08x\r\n", status);
        return;
    }

    if ((status = nx_azure_iot_hub_client_device_twin_properties_receive(&iothub_client, &packet_ptr, NX_WAIT_FOREVER)))
    {
        printf("device twin document receive failed!: error code = 0x%08x\r\n", status);
        return;
    }
    
    printf("Receive twin properties :");
    sprintf_packet(receivedProperties, packet_ptr);
    int rxProp_size = strlen(receivedProperties);
    int txIndex = 0;
    while(rxProp_size > 100)
    {       
        printf("%.*s", 100, &receivedProperties[txIndex]);
        rxProp_size -= 100;
        txIndex += 100;
        tx_thread_sleep(100);        
    }
    printf("%.*s\r\n", rxProp_size, &receivedProperties[txIndex]);
    tx_thread_sleep(100);
    
    responsePropertyLen = parse_packet_data(receivedProperties, responseProperty, sizeof(responseProperty));
//    bPropertyFound = find_property_value(receivedProperties, "\"telemetryInterval\"", propertyValue);
//    if(bPropertyFound == true)
//    {
//        //printf("%s = %s\r\n", "\"telemetryInterval\"", propertyValue);
//        AZ_telemetryInterval = atoi(propertyValue);        
//    }
//    else
//    {
//        printf("\"telemetryInterval\" not detected\r\n");
//    }

    nx_packet_release(packet_ptr);

    sample_reported_properties_send_action(&iothub_client);
    
    /* Loop to receive device twin message.  */
    while (loop)
    {
        if ((status = nx_azure_iot_hub_client_device_twin_desired_properties_receive(&iothub_client, &packet_ptr,
                                                                                     NX_WAIT_FOREVER)))
        {
            printf("Receive desired property receive failed!: error code = 0x%08x\r\n", status);
            break;
        }

        printf("Receive desired property call: ");
        // todo decode packet.....

        sprintf_packet(receivedProperties, packet_ptr);
        printf("%s\r\n", receivedProperties);
        
        responsePropertyLen = parse_packet_data(receivedProperties, responseProperty, sizeof(responseProperty));
        bPropertyFound = find_property_value(receivedProperties, PROPERTY_VERSION_FIELD, propertyValue);
        if(bPropertyFound == true)
        {
            reported_property_version = atoi(propertyValue);
        }
        if (responsePropertyLen > 2)
        {
            response_status = 200;
        }
        printf("%s\r\n", responseProperty);
        nx_packet_release(packet_ptr);

        if ((status = nx_azure_iot_hub_client_device_twin_reported_properties_send(&iothub_client,
                                                                                   (UCHAR *)responseProperty, responsePropertyLen,
                                                                                   &request_id, &response_status,
                                                                                   &reported_property_version,
                                                                                   NX_WAIT_FOREVER)))
        {
            printf("Device twin reported properties failed!: error code = 0x%08x\r\n", status);
            break;
        }

        if ((response_status < 200) || (response_status >= 300))
        {
            printf("device twin report properties failed with code : %d\r\n", response_status);
            break;
        }
    }
}
#endif /* DISABLE_DEVICE_TWIN_SAMPLE */

void send_button_event(ULONG parameter, UINT number, UINT count)
{
    CHAR buffer[TELEMETRY_MSGLEN_MAX];
    UINT buffer_length;

    NX_PARAMETER_NOT_USED(parameter);
       
    buffer_length = (UINT)snprintf(buffer, sizeof(buffer),
            "{\"button_event\":\"SW%u\", \"press_count\": %u}", number, count);
    send_telemetry_message(parameter, (UCHAR *)buffer, buffer_length);
}

#ifndef DISABLE_APP_CTRL_SAMPLE
void sample_app_ctrl_thread_entry(ULONG parameter)
{
    UCHAR loop = NX_TRUE;

    NX_PARAMETER_NOT_USED(parameter);

    APP_LED_init();
    APP_SWITCH_init();

    while (loop)
    {
        APP_LED_refresh();

        if (button_press_data.flag.sw1 == true)
        {
            send_button_event(parameter, 1, button_press_data.sw1_press_count);
            button_press_data.flag.sw1 = false;  
        }
        if (button_press_data.flag.sw2 == true)
        {
            send_button_event(parameter, 2, button_press_data.sw2_press_count);
            button_press_data.flag.sw2 = false;  
        }
        
        tx_thread_sleep(NX_IP_PERIODIC_RATE/2);
    }
}
#endif /* DISABLE_APP_CTRL_SAMPLE */

#ifndef DISABLE_PERIOD_TIMER_SAMPLE
void sample_period_timer_thread_entry(ULONG parameter)
{
    UCHAR loop = NX_TRUE;

    NX_PARAMETER_NOT_USED(parameter);

    while (loop)
    {
        if (AZ_systemRebootTimer)
        {
            AZ_systemRebootTimer--;
            if (AZ_systemRebootTimer == 0)
            {
                printf("AZURE: Rebooting...");
                SYS_RESET_SoftwareReset();
            }
        }
        tx_thread_sleep(NX_IP_PERIODIC_RATE);
    }
}
#endif /* DISABLE_PERIOD_TIMER_SAMPLE */

UINT azureGlue_crypto_hmac_256_calculate(UCHAR *key, UINT key_length, const UCHAR *input, UINT input_length, UCHAR *output)
{
    UINT status;
    VOID *handler;
    
    /* Reset the output buffer.  */
    NX_CRYPTO_MEMSET(output, 0xFF, sizeof(output));
    //extern NX_CRYPTO_METHOD crypto_method_hmac_sha256;
    //NX_CRYPTO_METHOD *crypto_method = &crypto_method_hmac_sha256;
    extern NX_CRYPTO_METHOD crypto_method_hmac_sha1;
    NX_CRYPTO_METHOD *crypto_method = &crypto_method_hmac_sha1;
    
    /* Initialize.  */
    status = crypto_method->nx_crypto_init((NX_CRYPTO_METHOD *)crypto_method,
                                                          key, (key_length << 3),
                                                          &handler,
                                                          nx_azure_iot_tls_metadata_buffer,
                                                          NX_AZURE_IOT_TLS_METADATA_BUFFER_SIZE);
    if (status!=NX_CRYPTO_SUCCESS)
    {
        printf("[Err]%s:status = %d\r\n", __FUNCTION__, status);
        return(status);
    }

    /* Authenticate.  */
    status = crypto_method->nx_crypto_operation(NX_CRYPTO_AUTHENTICATE,
                                                               handler,
                                                               (NX_CRYPTO_METHOD *)crypto_method,
                                                               key,
                                                               (key_length << 3),
                                                               (VOID *)input,
                                                               input_length,
                                                               NX_CRYPTO_NULL,
                                                               output,
                                                               32,
                                                               nx_azure_iot_tls_metadata_buffer,
                                                               NX_AZURE_IOT_TLS_METADATA_BUFFER_SIZE,
                                                               NX_CRYPTO_NULL,
                                                               NX_CRYPTO_NULL);
    if (status!=NX_CRYPTO_SUCCESS)
    {
        printf("[Err]%s:status = %d\r\n", __FUNCTION__, status);
        return(status);
    }

    /* Cleanup.  */
    status = crypto_method->nx_crypto_cleanup(nx_azure_iot_tls_metadata_buffer);
    
    //printf("[Ret]%s:status = %d\r\n", __FUNCTION__, status);
    return(status);
    
}
