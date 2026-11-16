/**
 * @file
 * @brief command line tool that simulates a BACnet server device on the
 * network using the BACnet Stack and all the example object types.
 * @author Steve Karg <skarg@users.sourceforge.net>
 * @date 2006
 * @copyright SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
/* BACnet Stack defines - first */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/apdu.h"
#include "bacnet/bacdcode.h"
#include "bacnet/bactext.h"
#include "bacnet/dcc.h"
#include "bacnet/getevent.h"
#include "bacnet/iam.h"
#include "bacnet/npdu.h"
#include "bacnet/version.h"
#include "bacnet/wp.h"
#include "bacnet/bacapp.h"
#include "bacnet/basic/object/schedule.h"
#include "bacnet/basic/sys/cJSON.h"
/* some demo stuff needed */
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/sys/mstimer.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
#include "bacnet/datetime.h"
/* include the device object */
#include "bacnet/basic/object/device.h"
/* objects that have tasks inside them */
#if (BACNET_PROTOCOL_REVISION >= 14)
#include "bacnet/basic/object/lo.h"
#include "bacnet/basic/object/channel.h"
#endif
#if (BACNET_PROTOCOL_REVISION >= 24)
#include "bacnet/basic/object/color_object.h"
#include "bacnet/basic/object/color_temperature.h"
#endif
#include "bacnet/basic/object/lc.h"
#include "bacnet/basic/object/trendlog.h"
#include "bacnet/basic/object/structured_view.h"
#if defined(INTRINSIC_REPORTING)
#include "bacnet/basic/object/nc.h"
#endif /* defined(INTRINSIC_REPORTING) */
#include "bacnet/basic/object/bacfile.h"
#if defined BACNET_BACKUP_RESTORE
#include "bacfile-posix.h"
#endif
#if defined(BAC_UCI)
#include "bacnet/basic/ucix/ucix.h"
#endif /* defined(BAC_UCI) */

/* (Doxygen note: The next two lines pull all the following Javadoc
 *  into the ServerDemo module.) */
/** @addtogroup ServerDemo */
/*@{*/

/* current version of the BACnet stack */
static const char *BACnet_Version = BACNET_VERSION_TEXT;
/* task timer for various BACnet timeouts */
static struct mstimer BACnet_Task_Timer;
/* task timer for TSM timeouts */
static struct mstimer BACnet_TSM_Timer;
/* task timer for address binding timeouts */
static struct mstimer BACnet_Address_Timer;
#if defined(INTRINSIC_REPORTING)
/* task timer for notification recipient timeouts */
static struct mstimer BACnet_Notification_Timer;
#endif
/* task timer for objects */
static struct mstimer BACnet_Object_Timer;
/** Buffer used for receiving */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };
/* SmartCampus: parsed JSON configuration (environment BACNET_OBJECTS_CONFIG) */
static cJSON *Config_Root;
#if defined BACNET_BACKUP_RESTORE
/* SmartCampus: automatic backup of the objects after each change */
static uint32_t Backup_File_Instance = BACNET_MAX_INSTANCE;
static bool Backup_Pending;
static struct mstimer Backup_Timer;
#endif
static void Backup_Mark(void);

/* configure an example structured view object subordinate list */
#if (BACNET_PROTOCOL_REVISION >= 4)
#define LIGHTING_OBJECT_WATTS OBJECT_ACCUMULATOR
#else
#define LIGHTING_OBJECT_WATTS OBJECT_ANALOG_INPUT
#endif
#if (BACNET_PROTOCOL_REVISION >= 6)
#define LIGHTING_OBJECT_ADR OBJECT_LOAD_CONTROL
#else
#define LIGHTING_OBJECT_ADR OBJECT_MULTISTATE_OUTPUT
#endif
#if (BACNET_PROTOCOL_REVISION >= 6)
#define LIGHTING_OBJECT_ADR OBJECT_LOAD_CONTROL
#else
#define LIGHTING_OBJECT_ADR OBJECT_MULTISTATE_OUTPUT
#endif
#if (BACNET_PROTOCOL_REVISION >= 14)
#define LIGHTING_OBJECT_SCENE OBJECT_CHANNEL
#define LIGHTING_OBJECT_LIGHT OBJECT_LIGHTING_OUTPUT
#else
#define LIGHTING_OBJECT_SCENE OBJECT_ANALOG_VALUE
#define LIGHTING_OBJECT_LIGHT OBJECT_ANALOG_OUTPUT
#endif
#if (BACNET_PROTOCOL_REVISION >= 16)
#define LIGHTING_OBJECT_RELAY OBJECT_BINARY_LIGHTING_OUTPUT
#else
#define LIGHTING_OBJECT_RELAY OBJECT_BINARY_OUTPUT
#endif

static BACNET_SUBORDINATE_DATA Lighting_Subordinate[] = {
    { 0, LIGHTING_OBJECT_WATTS, 1, "watt-hours", 0, 0, NULL },
    { 0, LIGHTING_OBJECT_ADR, 1, "demand-response", 0, 0, NULL },
    { 0, LIGHTING_OBJECT_SCENE, 1, "scene", 0, 0, NULL },
    { 0, LIGHTING_OBJECT_LIGHT, 1, "light", 0, 0, NULL },
    { 0, LIGHTING_OBJECT_RELAY, 1, "relay", 0, 0, NULL },
#if (BACNET_PROTOCOL_REVISION >= 24)
    { 0, OBJECT_COLOR, 1, "color", 0, 0, NULL },
    { 0, OBJECT_COLOR_TEMPERATURE, 1, "color-temperature", 0, 0, NULL },
#endif
};
static BACNET_WRITE_GROUP_NOTIFICATION Write_Group_Notification = { 0 };

/**
 * @brief Update the strcutured view static data with device ID and linked lists
 * @param device_id Device Instance to assign to every subordinate
 */
static void Structured_View_Update(void)
{
    uint32_t device_id, instance;
    BACNET_DEVICE_OBJECT_REFERENCE represents = { 0 };
    size_t i;

    device_id = Device_Object_Instance_Number();
    for (i = 0; i < ARRAY_SIZE(Lighting_Subordinate); i++) {
        /* link the lists */
        if (i < (ARRAY_SIZE(Lighting_Subordinate) - 1)) {
            Lighting_Subordinate[i].next = &Lighting_Subordinate[i + 1];
        }
        /* update the device instance to internal */
        Lighting_Subordinate[i].Device_Instance = device_id;
        /* update the common node data */
        Lighting_Subordinate[i].Node_Type = BACNET_NODE_POINT;
        Lighting_Subordinate[i].Relationship = BACNET_RELATIONSHIP_CONTAINS;
    }
    instance = Structured_View_Index_To_Instance(0);
    Structured_View_Subordinate_List_Set(instance, Lighting_Subordinate);
    /* In some cases, the Structure View object will abstractly represent
       this entity by itself, and this property will either be absent,
       unconfigured, or point to itself. */
    represents.deviceIdentifier.type = OBJECT_NONE;
    represents.deviceIdentifier.instance = BACNET_MAX_INSTANCE;
    represents.objectIdentifier.type = OBJECT_DEVICE;
    represents.objectIdentifier.instance = device_id;
    Structured_View_Represents_Set(instance, &represents);
    Structured_View_Node_Type_Set(instance, BACNET_NODE_ROOM);
    Structured_View_Default_Subordinate_Relationship_Set(
        instance, BACNET_RELATIONSHIP_CONTAINS);
}

/* verbose request logging; set environment variable BACNET_VERBOSE=0 to mute */
static bool Verbose_Enabled(void)
{
    static int enabled = -1;
    const char *env;

    if (enabled < 0) {
        env = getenv("BACNET_VERBOSE");
        enabled = !((env != NULL) && (env[0] == '0'));
    }

    return enabled != 0;
}

static void Log_Request(
    const char *name,
    uint16_t service_len,
    const BACNET_ADDRESS *src,
    const BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    unsigned i;

    if (!Verbose_Enabled()) {
        return;
    }
    printf(
        "[REQ] %s invoke-id=%u len=%u from net=%u mac=", name,
        (unsigned)service_data->invoke_id, (unsigned)service_len,
        (unsigned)src->net);
    for (i = 0; i < src->mac_len; i++) {
        printf("%02X", (unsigned)src->mac[i]);
    }
    printf("\n");
    fflush(stdout);
}

static void Verbose_Read_Property(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    Log_Request("ReadProperty", service_len, src, service_data);
    handler_read_property(service_request, service_len, src, service_data);
}

static void Verbose_Read_Property_Multiple(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    Log_Request("ReadPropertyMultiple", service_len, src, service_data);
    handler_read_property_multiple(
        service_request, service_len, src, service_data);
}

static void Verbose_Write_Property(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    BACNET_WRITE_PROPERTY_DATA wp_data = { 0 };
    int len;

    Log_Request("WriteProperty", service_len, src, service_data);
    if (Verbose_Enabled()) {
        len = wp_decode_service_request(service_request, service_len, &wp_data);
        if (len > 0) {
            printf(
                "[REQ]   %s-%lu %s array-index=%ld priority=%u "
                "data-len=%d\n",
                bactext_object_type_name(wp_data.object_type),
                (unsigned long)wp_data.object_instance,
                bactext_property_name(wp_data.object_property),
                (wp_data.array_index == BACNET_ARRAY_ALL)
                    ? -1L
                    : (long)wp_data.array_index,
                (unsigned)wp_data.priority, wp_data.application_data_len);
        } else {
            printf("[REQ]   WriteProperty decode failed (%d)\n", len);
        }
    }
    handler_write_property(service_request, service_len, src, service_data);
    Backup_Mark();
#if BACNET_EXCEPTION_SCHEDULE_SIZE
    if (Verbose_Enabled() && (len > 0) &&
        (wp_data.object_type == OBJECT_SCHEDULE) &&
        (wp_data.object_property == PROP_EXCEPTION_SCHEDULE)) {
        printf(
            "[REQ]   Exception_Schedule now holds %u entries\n",
            Schedule_Exception_Schedule_Count(wp_data.object_instance));
    }
#endif
    fflush(stdout);
}

static void Verbose_Write_Property_Multiple(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    Log_Request("WritePropertyMultiple", service_len, src, service_data);
    handler_write_property_multiple(
        service_request, service_len, src, service_data);
    Backup_Mark();
}

static void Verbose_Create_Object(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    Log_Request("CreateObject", service_len, src, service_data);
    handler_create_object(service_request, service_len, src, service_data);
    Backup_Mark();
}

static void Verbose_Delete_Object(
    uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_DATA *service_data)
{
    Log_Request("DeleteObject", service_len, src, service_data);
    handler_delete_object(service_request, service_len, src, service_data);
    Backup_Mark();
}

/* ---------------------------------------------------------------------
 * SmartCampus: objects created at startup from a JSON configuration file
 * --------------------------------------------------------------------- */

/**
 * @brief Read and parse the JSON configuration file
 * @param path [in] path of the JSON file
 * @return parsed JSON tree, or NULL on error
 */
static cJSON *Config_Parse(const char *path)
{
    FILE *file;
    long size;
    char *text;
    cJSON *root = NULL;

    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "[CFG] cannot open %s\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    text = calloc((size_t)size + 1, 1);
    if (text && (fread(text, 1, (size_t)size, file) == (size_t)size)) {
        root = cJSON_Parse(text);
        if (!root) {
            fprintf(
                stderr, "[CFG] invalid JSON in %s near: %.40s\n", path,
                cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "?");
        }
    }
    free(text);
    fclose(file);

    return root;
}

/**
 * @brief Find the BACnet datatype of a property by reading its current
 *  value from the object, so any object type works without specific code.
 * @param object_type [in] BACnet object type
 * @param object_instance [in] BACnet object instance
 * @param property [in] BACnet property
 * @return the application tag of the property, or -1 if unknown
 */
static int Config_Property_Tag(
    BACNET_OBJECT_TYPE object_type,
    uint32_t object_instance,
    BACNET_PROPERTY_ID property)
{
    BACNET_READ_PROPERTY_DATA rp_data = { 0 };
    BACNET_APPLICATION_DATA_VALUE value = { 0 };
    uint8_t apdu[MAX_APDU] = { 0 };
    int len;

    rp_data.object_type = object_type;
    rp_data.object_instance = object_instance;
    rp_data.object_property = property;
    rp_data.array_index = BACNET_ARRAY_ALL;
    rp_data.application_data = apdu;
    rp_data.application_data_len = sizeof(apdu);
    len = Device_Read_Property(&rp_data);
    if ((len > 0) &&
        (bacapp_decode_application_data(apdu, (uint32_t)len, &value) > 0)) {
        return value.tag;
    }

    return bacapp_known_property_tag(object_type, property);
}

/**
 * @brief Write one property of an object from its JSON value. The BACnet
 *  datatype is deduced from the current value of the property.
 * @param object_type [in] BACnet object type
 * @param object_instance [in] BACnet object instance
 * @param item [in] JSON item: name is the property, value its value
 * @return true if the property was written
 */
static bool Config_Property_Write(
    BACNET_OBJECT_TYPE object_type, uint32_t object_instance, cJSON *item)
{
    BACNET_APPLICATION_DATA_VALUE value = { 0 };
    BACNET_WRITE_PROPERTY_DATA wp_data = { 0 };
    uint32_t property = 0;
    uint32_t units = 0;
    char text[32] = "";
    const char *argument = text;
    int tag;
    bool status = false;

    if (!bactext_property_strtol(item->string, &property)) {
        printf("[CFG]   unknown property %s\n", item->string);
        return false;
    }
    tag = Config_Property_Tag(
        object_type, object_instance, (BACNET_PROPERTY_ID)property);
    if (tag < 0) {
        printf("[CFG]   unsupported property %s\n", item->string);
        return false;
    }
    if (cJSON_IsString(item)) {
        argument = item->valuestring;
        if ((property == PROP_UNITS) &&
            bactext_engineering_unit_index(argument, &units)) {
            snprintf(text, sizeof(text), "%lu", (unsigned long)units);
            argument = text;
        }
    } else if (cJSON_IsBool(item)) {
        argument = cJSON_IsTrue(item) ? "1" : "0";
    } else if (cJSON_IsNumber(item)) {
        snprintf(text, sizeof(text), "%g", item->valuedouble);
    } else {
        printf("[CFG]   %s: value must be a string, number or boolean\n",
            item->string);
        return false;
    }
    if (bacapp_parse_application_data(
            (BACNET_APPLICATION_TAG)tag, argument, &value)) {
        wp_data.object_type = object_type;
        wp_data.object_instance = object_instance;
        wp_data.object_property = (BACNET_PROPERTY_ID)property;
        wp_data.array_index = BACNET_ARRAY_ALL;
        wp_data.priority = BACNET_MAX_PRIORITY;
        wp_data.application_data_len =
            bacapp_encode_application_data(wp_data.application_data, &value);
        status = Device_Write_Property(&wp_data);
    }
    if (status) {
        printf("[CFG]   %s=%s OK\n", item->string, argument);
    } else {
        printf(
            "[CFG]   %s=%s refused: %s\n", item->string, argument,
            bactext_error_code_name(wp_data.error_code));
    }

    return status;
}

/**
 * @brief Create the objects listed in the "objects" array of the
 *  configuration, then write their properties.
 * @param root [in] parsed JSON configuration
 */
static void Config_Objects_Create(cJSON *root)
{
    BACNET_CREATE_OBJECT_DATA object_data = { 0 };
    cJSON *objects, *object, *type, *instance, *properties, *property;
    uint32_t object_type = 0;

    objects = cJSON_GetObjectItem(root, "objects");
    cJSON_ArrayForEach(object, objects)
    {
        type = cJSON_GetObjectItem(object, "type");
        instance = cJSON_GetObjectItem(object, "instance");
        if (!cJSON_IsString(type) || !cJSON_IsNumber(instance) ||
            !bactext_object_type_strtol(type->valuestring, &object_type)) {
            printf("[CFG] object ignored: needs a valid \"type\" and "
                   "\"instance\"\n");
            continue;
        }
        object_data.object_type = (BACNET_OBJECT_TYPE)object_type;
        object_data.object_instance = (uint32_t)instance->valuedouble;
        object_data.error_class = ERROR_CLASS_OBJECT;
        object_data.error_code = ERROR_CODE_SUCCESS;
        if (!Device_Create_Object(&object_data)) {
            printf(
                "[CFG] %s-%lu not created: %s\n", type->valuestring,
                (unsigned long)object_data.object_instance,
                bactext_error_code_name(object_data.error_code));
            continue;
        }
        printf(
            "[CFG] %s-%lu created\n", type->valuestring,
            (unsigned long)object_data.object_instance);
        properties = cJSON_GetObjectItem(object, "properties");
        cJSON_ArrayForEach(property, properties)
        {
            Config_Property_Write(
                object_data.object_type, object_data.object_instance,
                property);
        }
    }
    fflush(stdout);
}

/* ---------------------------------------------------------------------
 * SmartCampus: automatic backup and restore of the objects. The stack
 * already knows how to save every object as a CreateObject request in a
 * file (Device_Start_Backup) and to recreate them (Device_End_Restore).
 * --------------------------------------------------------------------- */

/**
 * @brief Ask for a backup a short time after a change, so that a burst
 *  of writes produces a single backup.
 */
static void Backup_Mark(void)
{
#if defined BACNET_BACKUP_RESTORE
    Backup_Pending = true;
    mstimer_set(&Backup_Timer, 2000UL);
#endif
}

#if defined BACNET_BACKUP_RESTORE
/**
 * @brief Create the File object used for the backup, then restore the
 *  objects from it if the file is not empty.
 */
static void Backup_Init(void)
{
    BACNET_CREATE_OBJECT_DATA object_data = { 0 };
    const char *pathname;

    pathname = getenv("BACNET_BACKUP_FILE");
    if (!pathname || !pathname[0]) {
        pathname = "backup_1.bin";
    }
    object_data.object_type = OBJECT_FILE;
    object_data.object_instance = BACNET_MAX_INSTANCE;
    if (!Device_Create_Object(&object_data)) {
        printf("[BACKUP] cannot create the backup File object\n");
        return;
    }
    Backup_File_Instance = object_data.object_instance;
    bacfile_pathname_set(Backup_File_Instance, pathname);
    Device_Configuration_File_Set(0, Backup_File_Instance);
    if (bacfile_file_size(Backup_File_Instance) > 0) {
        Device_Start_Restore();
        Device_End_Restore();
        printf(
            "[BACKUP] objects restored from %s (%s)\n", pathname,
            (Device_Backup_And_Restore_State() == BACKUP_STATE_IDLE)
                ? "OK"
                : "failure");
    } else {
        printf("[BACKUP] no backup in %s yet\n", pathname);
    }
    fflush(stdout);
}

/**
 * @brief Write all the objects to the backup file when a change is pending
 */
static void Backup_Task(void)
{
    if (!Backup_Pending || !mstimer_expired(&Backup_Timer) ||
        (Backup_File_Instance == BACNET_MAX_INSTANCE)) {
        return;
    }
    Backup_Pending = false;
    bacfile_file_size_set(Backup_File_Instance, 0);
    Device_Start_Backup();
    Device_Backup_And_Restore_State_Set(BACKUP_STATE_IDLE);
    printf(
        "[BACKUP] %lu objects saved in %s (%lu bytes)\n",
        (unsigned long)Device_Object_List_Count(),
        bacfile_pathname(Backup_File_Instance),
        (unsigned long)bacfile_file_size(Backup_File_Instance));
    fflush(stdout);
}
#endif

/** Initialize the handlers we will utilize.
 * @see Device_Init, apdu_set_unconfirmed_handler, apdu_set_confirmed_handler
 */
static void Init_Service_Handlers(void)
{
    BACNET_CREATE_OBJECT_DATA object_data = { 0 };
#ifdef SERVER_STATIC_OBJECTS
    unsigned int i = 0;
#endif

    /* Initialize the device object and its children */
    Device_Init(NULL);
#ifdef SERVER_STATIC_OBJECTS
    /* Build with -DSERVER_STATIC_OBJECTS to create one instance of every
       object type at startup. By default none are created; clients can
       add them with CreateObject. */
    object_data.object_instance = BACNET_MAX_INSTANCE;
    for (i = 0; i < BACNET_OBJECT_TYPE_RESERVED_MIN; i++) {
        object_data.object_type = i;
        object_data.error_class = ERROR_CLASS_OBJECT;
        object_data.error_code = ERROR_CODE_SUCCESS;
        if (Device_Create_Object(&object_data)) {
            printf(
                "CreateObject: %s-%u %s\n", bactext_object_type_name(i),
                (unsigned)object_data.object_instance,
                bactext_error_code_name(object_data.error_code));
        }
    }
#endif
#if defined BACNET_BACKUP_RESTORE
    /* initialize the POSIX file object backend */
    bacfile_posix_init();
#ifdef SERVER_STATIC_OBJECTS
    /* file for backup and restore example */
    object_data.object_instance = bacfile_index_to_instance(0);
    if (object_data.object_instance < BACNET_MAX_INSTANCE) {
        bacfile_pathname_set(object_data.object_instance, "backup_1.bin");
        Device_Configuration_File_Set(0, object_data.object_instance);
        printf(
            "Created %s-%u path=%s for backup and restore (%u files).\n",
            bactext_object_type_name(OBJECT_FILE),
            (unsigned)object_data.object_instance,
            bacfile_pathname(object_data.object_instance), bacfile_count());
    }
#endif
#endif
    /* SmartCampus: objects from the configuration, then the last backup */
    if (Config_Root) {
        Config_Objects_Create(Config_Root);
    }
#if defined BACNET_BACKUP_RESTORE && !defined SERVER_STATIC_OBJECTS
    Backup_Init();
#endif
    /* set up our confirmed service unrecognized service handler - required! */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* we need to handle who-is to support dynamic device binding */
    apdu_set_unconfirmed_handler(
        SERVICE_UNCONFIRMED_WHO_IS, handler_who_is_who_am_i_unicast);
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_HAS, handler_who_has);
    /* Set the handlers for any confirmed services that we support. */
    /* We must implement read property - it's required! */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, Verbose_Read_Property);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROP_MULTIPLE, Verbose_Read_Property_Multiple);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_WRITE_PROPERTY, Verbose_Write_Property);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_WRITE_PROP_MULTIPLE, Verbose_Write_Property_Multiple);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_RANGE, handler_read_range);
#if defined BACNET_BACKUP_RESTORE
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_ATOMIC_READ_FILE, handler_atomic_read_file);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_ATOMIC_WRITE_FILE, handler_atomic_write_file);
#endif
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_REINITIALIZE_DEVICE, handler_reinitialize_device);
    apdu_set_unconfirmed_handler(
        SERVICE_UNCONFIRMED_UTC_TIME_SYNCHRONIZATION, handler_timesync_utc);
    apdu_set_unconfirmed_handler(
        SERVICE_UNCONFIRMED_TIME_SYNCHRONIZATION, handler_timesync);
    apdu_set_unconfirmed_handler(
        SERVICE_UNCONFIRMED_YOU_ARE, handler_you_are_json_print);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_SUBSCRIBE_COV, handler_cov_subscribe);
    apdu_set_unconfirmed_handler(
        SERVICE_UNCONFIRMED_COV_NOTIFICATION, handler_ucov_notification);
    /* handle communication so we can shutup when asked */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL,
        handler_device_communication_control);
    /* handle the data coming back from private requests */
    apdu_set_unconfirmed_handler(
        SERVICE_UNCONFIRMED_PRIVATE_TRANSFER,
        handler_unconfirmed_private_transfer);
    handler_unconfirmed_private_transfer_callback_set(
        handler_private_transfer_print);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_PRIVATE_TRANSFER, handler_confirmed_private_transfer);
    handler_confirmed_private_transfer_callback_set(
        handler_private_transfer_print);
    /* WriteGroup for Channels */
    apdu_set_unconfirmed_handler(
        SERVICE_UNCONFIRMED_WRITE_GROUP, handler_write_group);
    /* add WriteGroup iterator to the Channel objects */
    Write_Group_Notification.callback = Channel_Write_Group;
    handler_write_group_notification_add(&Write_Group_Notification);
#if defined(INTRINSIC_REPORTING)
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_ACKNOWLEDGE_ALARM, handler_alarm_ack);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_GET_EVENT_INFORMATION, handler_get_event_information);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_GET_ALARM_SUMMARY, handler_get_alarm_summary);
#endif /* defined(INTRINSIC_REPORTING) */
#if defined(BACNET_TIME_MASTER)
    handler_timesync_init();
#endif
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_CREATE_OBJECT, Verbose_Create_Object);
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_DELETE_OBJECT, Verbose_Delete_Object);
    /* configure the cyclic timers */
    mstimer_set(&BACnet_Task_Timer, 1000UL);
    mstimer_set(&BACnet_TSM_Timer, 50UL);
    mstimer_set(&BACnet_Address_Timer, 60UL * 1000UL);
    mstimer_set(&BACnet_Object_Timer, 100UL);
#if defined(INTRINSIC_REPORTING)
    mstimer_set(&BACnet_Notification_Timer, NC_RESCAN_RECIPIENTS_SECS * 1000UL);
#endif
}

static void print_usage(const char *filename)
{
    printf("Usage: %s [device-instance [device-name]]\n", filename);
    printf("       [--version][--help]\n");
}

static void print_help(const char *filename)
{
    printf("Simulate a BACnet server device\n"
           "device-instance:\n"
           "BACnet Device Object Instance number that you are\n"
           "trying simulate.\n"
           "device-name:\n"
           "The Device object-name is the text name for the device.\n"
           "\nExample:\n");
    printf(
        "To simulate Device 123, use the following command:\n"
        "%s 123\n",
        filename);
    printf(
        "To simulate Device 123 named Fred, use following command:\n"
        "%s 123 Fred\n",
        filename);
}

/** Main function of server demo.
 *
 * @see Device_Set_Object_Instance_Number, dlenv_init, Send_I_Am,
 *      datalink_receive, npdu_handler,
 *      dcc_timer_seconds, datalink_maintenance_timer,
 *      handler_cov_task, tsm_timer_milliseconds
 *
 * @param argc [in] Arg count.
 * @param argv [in] Takes one argument: the Device Instance #.
 * @return 0 on success.
 */
int main(int argc, char *argv[])
{
    BACNET_ADDRESS src = { 0 }; /* address where message came from */
    uint16_t pdu_len = 0;
    unsigned timeout = 1; /* milliseconds */
    uint32_t elapsed_milliseconds = 0;
    uint32_t elapsed_seconds = 0;
    BACNET_CHARACTER_STRING DeviceName;
    uint32_t device_id = 0xFFFFFFFF;
#if defined(BACNET_TIME_MASTER)
    BACNET_DATE_TIME bdatetime;
#endif
#if defined(BAC_UCI)
    int uciId = 0;
    struct uci_context *ctx;
#endif
    int argi = 0;
    const char *filename = NULL;
    const char *config_path = NULL;
    cJSON *config_device = NULL;
    cJSON *config_item = NULL;

    filename = filename_remove_path(argv[0]);
    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0) {
            print_usage(filename);
            print_help(filename);
            return 0;
        }
        if (strcmp(argv[argi], "--version") == 0) {
            printf("%s %s\n", filename, BACNET_VERSION_TEXT);
            printf("Copyright (C) 2014 by Steve Karg and others.\n"
                   "This is free software; see the source for copying "
                   "conditions.\n"
                   "There is NO warranty; not even for MERCHANTABILITY or\n"
                   "FITNESS FOR A PARTICULAR PURPOSE.\n");
            return 0;
        }
    }
    /* SmartCampus: optional JSON configuration of the device and objects */
    config_path = getenv("BACNET_OBJECTS_CONFIG");
    if (config_path && config_path[0]) {
        Config_Root = Config_Parse(config_path);
        if (Config_Root) {
            printf("[CFG] configuration loaded from %s\n", config_path);
        }
    }
    config_device = cJSON_GetObjectItem(Config_Root, "device");
    config_item = cJSON_GetObjectItem(config_device, "instance");
    if ((argc <= 1) && cJSON_IsNumber(config_item)) {
        Device_Set_Object_Instance_Number((uint32_t)config_item->valuedouble);
    }
#if defined(BAC_UCI)
    ctx = ucix_init("bacnet_dev");
    if (!ctx) {
        fprintf(stderr, "Failed to load config file bacnet_dev\n");
    }
    uciId = ucix_get_option_int(ctx, "bacnet_dev", "0", "Id", 0);
    if (uciId != 0) {
        Device_Set_Object_Instance_Number(uciId);
    } else {
#endif /* defined(BAC_UCI) */
        /* allow the device ID to be set */
        if (argc > 1) {
            Device_Set_Object_Instance_Number(strtol(argv[1], NULL, 0));
        }

#if defined(BAC_UCI)
    }
    ucix_cleanup(ctx);
#endif /* defined(BAC_UCI) */

    printf(
        "BACnet Server Demo\n"
        "BACnet Stack Version %s\n"
        "BACnet Device ID: %u\n"
        "Max APDU: %d\n",
        BACnet_Version, Device_Object_Instance_Number(), MAX_APDU);
    /* load any static address bindings to show up
       in our device bindings list */
    address_init();
    Init_Service_Handlers();
    /* initialize timesync callback function. */
    handler_timesync_set_callback_set(&datetime_timesync);

#if defined(BAC_UCI)
    const char *uciname;
    ctx = ucix_init("bacnet_dev");
    if (!ctx) {
        fprintf(stderr, "Failed to load config file bacnet_dev\n");
    }
    uciname = ucix_get_option(ctx, "bacnet_dev", "0", "Name");
    if (uciname != 0) {
        Device_Object_Name_ANSI_Init(uciname);
    } else {
#endif /* defined(BAC_UCI) */
        config_item = cJSON_GetObjectItem(config_device, "name");
        if (argc > 2) {
            Device_Object_Name_ANSI_Init(argv[2]);
        } else if (cJSON_IsString(config_item)) {
            Device_Object_Name_ANSI_Init(config_item->valuestring);
        }
#if defined(BAC_UCI)
    }
    ucix_cleanup(ctx);
#endif /* defined(BAC_UCI) */
    if (Device_Object_Name(Device_Object_Instance_Number(), &DeviceName)) {
        printf("BACnet Device Name: %s\n", DeviceName.value);
    }
    dlenv_init();
    atexit(datalink_cleanup);
#if BACNET_PROTOCOL_REVISION >= 22
    if (Device_Object_Instance_Number() == BACNET_MAX_INSTANCE) {
        apdu_set_unconfirmed_handler(
            SERVICE_UNCONFIRMED_YOU_ARE, handler_you_are_device_id_set);
        /* The Who-Am-I service is used by a sending BACnet-user
           to indicate that it requires identity configuration
           via the You-Are service. */
        Send_Who_Am_I_Broadcast(
            Device_Vendor_Identifier(), Device_Model_Name(),
            Device_Serial_Number());
    }
#endif
    /* loop forever */
    for (;;) {
        if (device_id != Device_Object_Instance_Number()) {
            device_id = Device_Object_Instance_Number();
            /* update structured view with this device instance */
            Structured_View_Update();
            if (Device_Object_Instance_Number() != BACNET_MAX_INSTANCE) {
                /* broadcast an I-Am on startup */
                Send_I_Am(&Handler_Transmit_Buffer[0]);
            }
        }
        /* input */
        pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, timeout);

        /* process */
        if (pdu_len) {
            npdu_handler(&src, &Rx_Buf[0], pdu_len);
        }
        if (mstimer_expired(&BACnet_Task_Timer)) {
            mstimer_reset(&BACnet_Task_Timer);
            elapsed_milliseconds = mstimer_interval(&BACnet_Task_Timer);
            elapsed_seconds = elapsed_milliseconds / 1000;
            /* 1 second tasks */
            dcc_timer_seconds(elapsed_seconds);
            datalink_maintenance_timer(elapsed_seconds);
            dlenv_maintenance_timer(elapsed_seconds);
            handler_cov_timer_seconds(elapsed_seconds);
            trend_log_timer(elapsed_seconds);
#if defined BACNET_BACKUP_RESTORE && !defined SERVER_STATIC_OBJECTS
            Backup_Task();
#endif
#if defined(INTRINSIC_REPORTING)
            Device_local_reporting();
#endif
#if defined(BACNET_TIME_MASTER)
            Device_getCurrentDateTime(&bdatetime);
            handler_timesync_task(&bdatetime);
#endif
        }
        if (mstimer_expired(&BACnet_TSM_Timer)) {
            mstimer_reset(&BACnet_TSM_Timer);
            elapsed_milliseconds = mstimer_interval(&BACnet_TSM_Timer);
            tsm_timer_milliseconds(elapsed_milliseconds);
        }
        if (mstimer_expired(&BACnet_Address_Timer)) {
            mstimer_reset(&BACnet_Address_Timer);
            elapsed_milliseconds = mstimer_interval(&BACnet_Address_Timer);
            elapsed_seconds = elapsed_milliseconds / 1000;
            address_cache_timer(elapsed_seconds);
        }
        handler_cov_task();
#if defined(INTRINSIC_REPORTING)
        if (mstimer_expired(&BACnet_Notification_Timer)) {
            mstimer_reset(&BACnet_Notification_Timer);
            Notification_Class_find_recipient();
        }
#endif
        /* output */
        if (mstimer_expired(&BACnet_Object_Timer)) {
            mstimer_reset(&BACnet_Object_Timer);
            elapsed_milliseconds = mstimer_interval(&BACnet_Object_Timer);
            Device_Timer(elapsed_milliseconds);
        }
    }

    return 0;
}

/* @} */

/* End group ServerDemo */
