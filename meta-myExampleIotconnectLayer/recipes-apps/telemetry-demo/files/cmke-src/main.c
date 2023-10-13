//
// Copyright: Avnet 2020
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include "iotconnect_common.h"
#include "iotconnect.h"
#include "cJSON.h"

#include "json_parser.h"

// windows compatibility
#if defined(_WIN32) || defined(_WIN64)
#define F_OK 0
#include <Windows.h>
#include <io.h>
int usleep(unsigned long usec) {
    Sleep(usec / 1000);
    return 0;
}
#define access    _access_s
#else
#include <unistd.h>
#endif

#define APP_VERSION "00.01.00"
#define STRINGS_ARE_EQUAL 0
#define FREE(x) if ((x)) { free(x); (x) = NULL; }

#undef COMMAND_FUNCTION_PTR_PARSING

typedef struct cert_struct {

    char* x509_id_cert;
    char* x509_id_key;

} cert_struct_t;



typedef enum command_type
{
    ECHO = 1,
    LED = 2,
    COMMANDS_END
} command_type_t;

const char *command_strings[] = {
    [ECHO] = "echo ",
    [LED] = "led ",
};



typedef struct local_data {

    char* board_name;
    char* scripts_list;
    char* scripts_path;
    sensors_data_t sensors;
    commands_data_t commands;

} local_data_t;

static local_data_t local_data = {0};

#define DOES_COMMAND_MATCH(input_str, command_enum) (strncmp((input_str), command_strings[(command_enum)], strlen(command_strings[(command_enum)])) == STRINGS_ARE_EQUAL)

static void free_sensor_data(sensors_data_t *sensors);
bool string_ends_with(const char * needle, const char* haystack);

// MOVE THIS LATER -afk
static void publish_message(const char* key_str,const char* value_str);

static void on_connection_status(IotConnectConnectionStatus status) {
    // Add your own status handling
    switch (status) {
        case IOTC_CS_MQTT_CONNECTED:
            printf("IoTConnect Client Connected\n");
            break;
        case IOTC_CS_MQTT_DISCONNECTED:
            printf("IoTConnect Client Disconnected\n");
            break;
        default:
            printf("IoTConnect Client ERROR\n");
            break;
    }
}


static void command_status(IotclEventData data, const char *command_name, bool success) {

    if (strcmp(command_name, "Internal error") == STRINGS_ARE_EQUAL){
        printf("Internal error (null ptr command)\r\n");
    }

    const char* message = success ? "OK" : "Failed_or_not_implemented";

    char *ack = iotcl_create_ack_string_and_destroy_event(data, success, message);
    printf("command: %s status=%s: %s\n", command_name, success ? "OK" : "Failed", message);
    printf("Sent CMD ack: %s\n", ack);
    iotconnect_sdk_send_packet(ack);
    free((void *)ack);
}

static int find_scripts(){

    if (!local_data.scripts_list){
        printf("no scripts dir");
        return 1;
    }

    FILE *fd = NULL;

    fd = fopen(local_data.scripts_list, "r");

    if (!fd){
        printf("Failed to open scripts list file %s\r\n", local_data.scripts_list);
        return 1;
    }

    char line[MAX_SCRIPT_NAME_LENGTH];

    int counter = 0;

    while(!feof(fd)){
        fgets(line, MAX_SCRIPT_NAME_LENGTH, fd);
        local_data.commands.scripts_counter++;

    }

    rewind(fd);
    
    printf("Found %d accepted scripts\r\n", local_data.commands.scripts_counter);

    if (local_data.commands.scripts_counter == 0){
        printf("no accepted script commands\r\n");
        fclose(fd);
        return 0;
    }

    local_data.commands.scripts = (scripts_data_t*)calloc(local_data.commands.scripts_counter, sizeof(scripts_data_t));

    if (!local_data.commands.scripts){
        printf("Failed to calloc \r\n");
        fclose(fd);
        return 1;
    }

    for (int i = 0; i < local_data.commands.scripts_counter; i++){
        fgets(line, MAX_SCRIPT_NAME_LENGTH, fd);
        strncpy(local_data.commands.scripts[i].script_name, line, MAX_SCRIPT_NAME_LENGTH);
        
        // replacing \n with \0
        if (local_data.commands.scripts[i].script_name[strlen(local_data.commands.scripts[i].script_name)-1] == '\n'){
            local_data.commands.scripts[i].script_name[strlen(local_data.commands.scripts[i].script_name)-1] = '\0';
        }
    }
    fclose(fd);

}

static int find_script_in_file(char* req_script){

    if (!local_data.scripts_list){
        printf("no scripts dir");
        return 1;
    }

    for (int i = 0; i < local_data.commands.scripts_counter; i++){
        //printf("comparing {%s} with {%s}\r\n", req_script, local_data.commands.scripts[i].script_name);
        if (strcmp(req_script, local_data.commands.scripts[i].script_name) == STRINGS_ARE_EQUAL){
            printf("script %s found\r\n", req_script);
            return 0;
        }
    }
    
    return 1;
}

static int command_parser(char *command_str){

    int ret = 1;

    char* command_str_cp = strdup(command_str);
    char* token = NULL;
    char* copy_ptr = &command_str[0];


    if (!command_str_cp){
        printf("failed to copy string\r\n");
        return 1;
    }

    int pos = 0;

    // find position of first ' ' char in received command
    for (int i = 0; i < strlen(command_str_cp); i++){
        if (command_str_cp[i] == ' '){
            pos = i;
            break;
        }
    } 

    token = (char*)calloc(pos+1, sizeof(char));

    if (!token){
        printf("failed to calloc\r\n");
        goto END;
    }

    // fill token buffer with data from command up to 1st ' '
    for (int i = 0; i < pos; i++){
        token[i] = command_str_cp[i];
    }
    token[pos] = '\0';
    

    char* rest_of_str = NULL;

    int tok_len = 0;

    int req_str_len = strlen(command_str);


    tok_len = strlen(token);
    req_str_len = req_str_len - tok_len;


    rest_of_str = (char*)calloc(req_str_len+1, sizeof(char));

    if (!rest_of_str){
        printf("failed to calloc!\r\n");
        goto END;
    }

    strncpy(rest_of_str, command_str+(tok_len+1), req_str_len-1);
    
    char* concat_str = NULL;

    if (strcmp(token, "exec") == STRINGS_ARE_EQUAL){

        int space_pos_new = 0;

        // find position of 2nd ' ' 
        for (int i = pos+1; i < strlen(command_str); i++){

            if (command_str_cp[i] == ' ' || command_str_cp[i] == '\0'){
                space_pos_new = i;
                break;
            }
        }

        free(token);
        token = NULL;

        // assuming .sh scripts will require at least 1 parameter (thus more than 1 space in original command). if not - this needs to be reworked 
        if (space_pos_new != 0){

            token = (char*)calloc((space_pos_new - pos) +1, sizeof(char));
        
            if (!token){
                printf("Faield to calloc\r\n");
                goto END;
            }

            // calculating length of 2nd token
            int new_len = space_pos_new - pos - 1;

            for (int i = 0; i < new_len; i++){
                token[i] = command_str_cp[(pos+1)+i]; // pos+1 to account for offset (1st token) 
            }        
            token[space_pos_new] = '\0';
            
        } else {

            token = (char*)calloc(req_str_len+1, sizeof(char));

            if (!token){
                
                printf("Faield to calloc\r\n");
                goto END;
            }

            strncpy(token, req_str_len, req_str_len);

        }
        //printf("NEWEST token {%s}\r\n", token);
        

        if (string_ends_with(".sh", token)){

            // check command file list and run found script 
            if (find_script_in_file(token) != 0){
                // if we'd need to allow runnin ANY scripts - this if section needs to be filled, so - PLACEHOLDER
                printf("Failed to find script {%s}", token);
                
                
            } else {

                // allocate enough memory to include script folder path (derived from initial json parsing) + command (except 1st token) 
                concat_str = (char*)calloc((strlen(rest_of_str)+strlen(local_data.scripts_path))+1, sizeof(char));
                
                if (!concat_str){
                    printf("failed to calloc\r\n");
                    ret = 1;
                    goto END;
                }

                memcpy(concat_str, local_data.scripts_path, strlen(local_data.scripts_path));
                memcpy(concat_str+(strlen(local_data.scripts_path)*sizeof(char)), rest_of_str, strlen(rest_of_str));
                
                // this runs bash
                ret = system(concat_str); 
            }
        } else { // run passed command as bash without string alterations

            ret = system(rest_of_str);    
        }

        
    } else if (strcmp(token, "echo") == STRINGS_ARE_EQUAL) {
        printf("%s\r\n", rest_of_str);
    } else { // assuming that all other cases would be an attempt to run one of the predefines scripts

        
        concat_str = (char*)calloc((strlen(command_str)+strlen(local_data.scripts_path))+1, sizeof(char));
        
        if (!concat_str){
            printf("failed to calloc\r\n");
            ret = 1;
            goto END;
        }

        memcpy(concat_str, local_data.scripts_path, strlen(local_data.scripts_path));
        memcpy(concat_str+(strlen(local_data.scripts_path)*sizeof(char)), command_str, strlen(command_str));

        if (find_script_in_file(token) != 0){
            printf("Failed to find script {%s} in script list file\r\n");
            ret = 1;
            goto END;
        }

        ret = system(concat_str);
    }
    
END:

    if (token){
        free(token);
        token = NULL;
    }

    
    if (concat_str){
        free(concat_str);
        concat_str = NULL;
    }
    

    if (command_str){
        free(command_str_cp);
        command_str_cp = NULL;
    }

    if (rest_of_str){
        free(rest_of_str);
        rest_of_str = NULL;
    }
    return ret;
}

static void on_command(IotclEventData data) {
    char *command = iotcl_clone_command(data);
    if (NULL != command) {
        
        printf("received command:\r\n%s\r\n", command);
        
        bool success = command_parser(command) ? true : false;

        success = !success;

        //printf("success %d %s\r\n", success, success ? "true": "false");

        command_status(data, command, success);
        free(command);
    } else {
        command_status(data, "Internal error", false);
    }
}

static bool is_app_version_same_as_ota(const char *version) {
    return strcmp(APP_VERSION, version) == 0;
}

static bool app_needs_ota_update(const char *version) {
    return strcmp(APP_VERSION, version) < 0;
}

static void on_ota(IotclEventData data) {
    const char *message = NULL;
    char *url = iotcl_clone_download_url(data, 0);
    bool success = false;
    if (NULL != url) {
        printf("Download URL is: %s\n", url);
        const char *version = iotcl_clone_sw_version(data);
        if (is_app_version_same_as_ota(version)) {
            printf("OTA request for same version %s. Sending success\n", version);
            success = true;
            message = "Version is matching";
        } else if (app_needs_ota_update(version)) {
            printf("OTA update is required for version %s.\n", version);
            success = false;
            message = "Not implemented";
        } else {
            printf("Device firmware version %s is newer than OTA version %s. Sending failure\n", APP_VERSION,
                   version);
            // Not sure what to do here. The app version is better than OTA version.
            // Probably a development version, so return failure?
            // The user should decide here.
            success = false;
            message = "Device firmware version is newer";
        }

        free((void *) url);
        free((void *) version);
    } else {
        // compatibility with older events
        // This app does not support FOTA with older back ends, but the user can add the functionality
        const char *command = iotcl_clone_command(data);
        if (NULL != command) {
            // URL will be inside the command
            printf("Command is: %s\n", command);
            message = "Old back end URLS are not supported by the app";
            free((void *) command);
        }
    }
    const char *ack = iotcl_create_ack_string_and_destroy_event(data, success, message);
    if (NULL != ack) {
        printf("Sent OTA ack: %s\n", ack);
        iotconnect_sdk_send_packet(ack);
        free((void *) ack);
    }
}


static void publish_telemetry(sensors_data_t sensors) {
    IotclMessageHandle msg = iotcl_telemetry_create();

    // Optional. The first time you create a data point, the current timestamp will be automatically added
    // TelemetryAddWith* calls are only required if sending multiple data points in one packet.
    iotcl_telemetry_add_with_iso_time(msg, iotcl_iso_timestamp_now());
    iotcl_telemetry_set_string(msg, "version", APP_VERSION);
    iotcl_telemetry_set_number(msg, "cpu", 3.123123); // test floating point numbers

    for (int i = 0; i < sensors.size; i++){

        if (sensors.sensor[i].reading){
        switch (sensors.sensor[i].mode)
            {
            case FMODE_ASCII:

                iotcl_telemetry_set_string(msg, sensors.sensor[i].s_name, (char*)sensors.sensor[i].reading);
                break;
            case FMODE_BIN:

                // TODO: we might get inacurate readings (for numbers with decimal points) with IEEE floats and doubles 
                iotcl_telemetry_set_number(msg, sensors.sensor[i].s_name, *(float*)sensors.sensor[i].reading);
                break;
            case FMODE_END:
            default:
                printf("Unsupported read mode. Skipping\r\n");
                break;
            }
        }
        
    }

    const char *str = iotcl_create_serialized_string(msg, false);
    iotcl_telemetry_destroy(msg);
    printf("Sending: %s\n", str);
    iotconnect_sdk_send_packet(str); // underlying code will report an error
    iotcl_destroy_serialized(str);
}


static void publish_message(const char* key_str,const char* value_str) {
    IotclMessageHandle msg = iotcl_telemetry_create();

    // Optional. The first time you create a data point, the current timestamp will be automatically added
    // TelemetryAddWith* calls are only required if sending multiple data points in one packet.
    iotcl_telemetry_add_with_iso_time(msg, iotcl_iso_timestamp_now());
    iotcl_telemetry_set_string(msg, key_str, value_str);

    const char *str = iotcl_create_serialized_string(msg, false);
    iotcl_telemetry_destroy(msg);
    printf("Sending: %s\n", str);
    iotconnect_sdk_send_packet(str); // underlying code will report an error
    iotcl_destroy_serialized(str);
}






static void free_local_data() {

    printf("freeing local data\r\n");

    for (int i = 0; i < local_data.commands.counter; i++){
        
#ifdef COMMAND_FUNCTION_PTR_PARSING
        if (local_data.commands.commands[i].f_ptr){
            free(local_data.commands.commands[i].f_ptr);
            local_data.commands.commands[i].f_ptr = NULL;
        }
#endif

        if (local_data.commands.commands[i].name){
            free(local_data.commands.commands[i].name);
            local_data.commands.commands[i].name = NULL;
        }

        if (local_data.commands.commands[i].private_data){
            free(local_data.commands.commands[i].private_data);
            local_data.commands.commands[i].private_data = NULL;
        }

    }

    if (local_data.board_name){
        free(local_data.board_name);
        local_data.board_name = NULL;
    }
    
    if (local_data.scripts_list){
        free(local_data.scripts_list);
        local_data.scripts_list = NULL;
    }

    if (local_data.scripts_path) {
        free(local_data.scripts_path);
        local_data.scripts_path = NULL;
    }

    if (local_data.commands.scripts){
        free(local_data.commands.scripts);
        local_data.commands.scripts = NULL;
    }

    free_sensor_data(&local_data.sensors);

}

static void free_sensor_data(sensors_data_t *sensors) {

    printf("freeing sensor data\r\n");

    for (int i = 0; i < sensors->size; i++){
        if (sensors->sensor[i].s_name){
            free(sensors->sensor[i].s_name);
            sensors->sensor[i].s_name = NULL;
        }

        if (sensors->sensor[i].s_path){
            free(sensors->sensor[i].s_path);
            sensors->sensor[i].s_path = NULL;
        }

        if (sensors->sensor[i].reading){
            free(sensors->sensor[i].reading);
            sensors->sensor[i].reading = NULL;
        }

    }

    if (sensors->sensor){
        free(sensors->sensor);
        sensors->sensor = NULL;
    }

}

static void free_iotc_config(IotConnectClientConfig* iotc_config) {
    
    printf("freeing iotconnect conf\r\n");

    if (iotc_config->cpid){
        free(iotc_config->cpid);
        iotc_config->cpid = NULL;
    }


    if (iotc_config->duid){
        free(iotc_config->duid);
        iotc_config->duid = NULL;
    }


    if (iotc_config->env){
        free(iotc_config->env);
        iotc_config->env = NULL;
    }

    if (iotc_config->auth_info.trust_store){
        free(iotc_config->auth_info.trust_store);
        iotc_config->auth_info.trust_store = NULL;
    }


    if (iotc_config->auth_info.data.cert_info.device_cert){
        free(iotc_config->auth_info.data.cert_info.device_cert);
        iotc_config->auth_info.data.cert_info.device_cert = NULL;
    }


    if (iotc_config->auth_info.data.cert_info.device_key){
        free(iotc_config->auth_info.data.cert_info.device_key);
        iotc_config->auth_info.data.cert_info.device_key = NULL;
    }


    if (iotc_config->auth_info.data.symmetric_key){
        free(iotc_config->auth_info.data.symmetric_key);
        iotc_config->auth_info.data.symmetric_key = NULL;
    }


}

static long get_file_length(FILE* fd){
    
    if (!fd){
        printf("NULL PTR\r\n");
        return -1;
    }

    fseek(fd, 0l, SEEK_END);
    long file_len = ftell(fd);

    if (file_len <= 0){
        printf("failed calculating file length: %ld. Aborting\n", file_len);
        return -1;
    }

    rewind(fd);

    return file_len;
    
}


// TODO: currently returns only floats, idk if that's required atm
static int read_sensor_ascii(sensor_info_t *sensor_data){

    if(access(sensor_data->s_path, F_OK) != 0){
        printf("failed to access sensor file - %s ; Aborting\n", sensor_data->s_path);
        return 1;
    }

    
    FILE* fd = NULL;

    fd = fopen(sensor_data->s_path, "r");

    long file_len = 0;
    file_len = get_file_length(fd);

    if (file_len <= 0){
        printf("failed to calculate file len.\r\n");
        fclose(fd);
        return -1;
    }

    char *buff = (char*)calloc(file_len+1, sizeof(char));

    if (!buff){
        printf("failed to calloc\r\n");
        fclose(fd);
        return -1;
    }
    float reading = 0;

    size_t read_n = 0;
    read_n = fread((void*)buff, sizeof(char), file_len, fd);

    fclose(fd);

    reading = (float)atof(buff);

    if (sensor_data->reading){
        free(sensor_data->reading);
        sensor_data->reading = NULL;
    }

    sensor_data->reading = strdup(buff);

    if (buff){
        free(buff);
        buff = NULL;
    }
    
    return 0;
}

static float read_sensor_raw(sensor_info_t *sensors_data){

    if(access(sensors_data->s_path, F_OK) != 0){
        printf("failed to access sensor file - %s ; Aborting\n", sensors_data->s_path);
        return 1;
    }

    FILE *fd = fopen(sensors_data->s_path, "rb");
    if (!fd)
    {
        printf("File failed to open - %s", sensors_data->s_path);
        return EXIT_FAILURE;
    }
    
    long file_len = 0;
    file_len = get_file_length(fd);

    if (file_len <= 0){
        printf("failed to calculate file len.\r\n");
        fclose(fd);
        return -1;
    }


    float reading = 0;

    size_t read_n;

    // ASSUMING WE'RE READING FLOATS

    __uint16_t req_size = 0;
 
    req_size = file_len/sizeof(float);
    void* buff = (void*)calloc(req_size, sizeof(float));

    read_n = fread(buff, sizeof(float), req_size, fd);
    
    if (read_n != req_size){
        printf("warning - read size if different from the required in file %s\r\n", sensors_data->s_path);
    }

    fclose(fd);

    reading = *(float*)buff;

    if (buff){
        free(buff);
        buff = NULL;
    }

    if (sensors_data->reading){
        free(sensors_data->reading);
        sensors_data->reading = NULL;
    }
    sensors_data->reading = (float*)malloc(sizeof(float));
    
    *(float*)sensors_data->reading = reading;

    return 0;
}

bool string_ends_with(const char * needle, const char* haystack){
    const char *str_end = haystack + strlen(haystack) -  strlen(needle);
    return (strncmp(str_end, needle, strlen(needle) ) == 0);
}

static int get_commands_path(char* commands_file_path) {

    char* new_str = NULL;

    new_str = strdup(commands_file_path);
    char* new_str_ptr_bg = &new_str[0];

    int str_len = strlen(new_str);

    int pos = 0;

    for (int i = 0; i < str_len; i++){

        if (*new_str == '/'){
            pos = i;
        }
        new_str+=(sizeof(char));
    }

    new_str = new_str_ptr_bg;

    for (int i = 0; i < pos+1; i++){
        new_str+=(sizeof(char));
    }

    new_str = new_str_ptr_bg;

    local_data.scripts_path = (char*)calloc(pos+2, sizeof(char));

    if (!local_data.scripts_path){
        printf("failed to calloc.\r\n");
        free(new_str_ptr_bg);
        new_str_ptr_bg = NULL;
        return 1;
    }
    

    for (int i = 0; i < pos+1; i++){
        local_data.scripts_path[i] = *new_str;
        new_str+=(sizeof(char));
    }
    local_data.scripts_path[pos+2] = '\0';
    printf("fullpath: {%s}\r\n", local_data.scripts_path);

    if (new_str_ptr_bg){
        free(new_str_ptr_bg);
        new_str_ptr_bg = NULL;
    }

    return 0;
}

int main(int argc, char *argv[]) {

    char* input_json_file = NULL;

    sensors_data_t sensors;
    sensors.size = 0;

    IotConnectClientConfig *config = iotconnect_sdk_init_and_get_config();
    // leaving this non-modifiable or now
    // 

    if (argc == 2) {
        // assuming only 1 parameters for now
        local_data.commands.counter = 0;
        local_data.sensors.size = 0;
        local_data.commands.scripts_counter = 0;

        if (!string_ends_with(".json", argv[1]))
        {
            printf("File extension is not .json of filename %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    
        input_json_file = argv[1];

        printf("file: %s\n", input_json_file);

        if(access(input_json_file, F_OK) != 0){
            printf("failed to access input json file - %s ; Aborting\n", input_json_file);
            return 1;
        }

        FILE *fd = fopen(input_json_file, "r");
        if (!fd)
        {
            printf("File failed to open - %s", input_json_file);
            return EXIT_FAILURE;
        }
        fseek(fd, 0l, SEEK_END);
        long file_len = ftell(fd);

        if (file_len <= 0){
            printf("failed calculating file length: %ld. Aborting\n", file_len);
            return 1;
        }

        rewind(fd);


        char* json_str = (char*)calloc(file_len+1, sizeof(char));

        if (!json_str) {
            printf("failed to calloc. Aborting\n");
            json_str = NULL;
            return 1;
        }

        for (int i = 0; i < file_len; i++){
            json_str[i] = fgetc(fd);
        }


        fclose(fd);

        if (parse_json_config(json_str, config, &local_data.commands, &local_data.sensors, &local_data.board_name, &local_data.scripts_list) != 0) {
            printf("Failed to parse input JSON file. Aborting\n");
            if (json_str != NULL) {
                free(json_str);
                json_str = NULL;
            }
            free_local_data();
            free_iotc_config(config);


            return 1;
        }

        if (get_commands_path(local_data.scripts_list) != 0) {
            printf("failed to get commands path.\r\n");
            // TODO: do we need to fail here?
        } else {
            if (find_scripts() != 0){
                printf("Failed to get script names\r\n");
                // TODO: do we need to fail here?
            }

        }
        

        
        printf("DUID in main: %s\r\n", config->duid);

        

        if (config->auth_info.type == IOTC_AT_X509){
            printf("id cert path: {%s}\n", config->auth_info.data.cert_info.device_cert);
            printf("id key path: {%s}\n", config->auth_info.data.cert_info.device_key);
            if(access(config->auth_info.data.cert_info.device_cert, F_OK) != 0){
                printf("failed to access parameter 1 - %s ; Aborting\n", config->auth_info.data.cert_info.device_cert);
                return 1;
            }
            

            if(access(config->auth_info.data.cert_info.device_key, F_OK) != 0){
                printf("failed to access parameter 2 - %s ; Aborting\n", config->auth_info.data.cert_info.device_key);
                return 1;
            }
        }
        
        /*
        for (int i = 0; i < local_data.sensors.size; i++){
            printf("id: %d;\n name %s;\n path %s;\r\n_____________\r\n", i, local_data.sensors.sensor[i].s_name, local_data.sensors.sensor[i].s_path);
        }
        */

#ifdef COMMAND_FUNCTION_PTR_PARSING

/*
        for (int i = 0; i < local_data.commands.counter; i++) {
            printf("name: %s\r\nprivate_data: %s\r\n", local_data.commands.commands[i].name, (char*)local_data.commands.commands[i].f_ptr, local_data.commands.commands[i].private_data);
        }
*/
#endif


    } else {
        
        printf("json file path is mandatory in this version of basic sample. Aborting\r\n");
        return 1;
    }

    config->status_cb = on_connection_status;
    config->ota_cb = on_ota;
    config->cmd_cb = on_command;


    int reading = 0;

    // run a dozen connect/send/disconnect cycles with each cycle being about a minute
    for (int j = 0; j < 10; j++) {
        int ret = iotconnect_sdk_init();
        if (0 != ret) {
            fprintf(stderr, "IoTConnect exited with error code %d\n", ret);
            return ret;
        }

        // send 10 messages
        for (int i = 0; iotconnect_sdk_is_connected() && i < 10; i++) {

            for (int i = 0; i < local_data.sensors.size; i++){
    

                switch (local_data.sensors.sensor[i].mode)
                {
                case FMODE_ASCII:

                    if (read_sensor_ascii(&local_data.sensors.sensor[i]) != 0){
                        printf("failed to read data from file in ascii mode\r\n");
                    }
                    break;
                case FMODE_BIN:
                    
                    if (read_sensor_raw(&local_data.sensors.sensor[i]) != 0){
                        printf("failed to read data from file in binary mode\r\n");
                    }

                    break;
                case FMODE_END:
                default:
                    printf("Unsupported read mode. Skipping\r\n");
                    break;
                }
        

            }
            publish_telemetry(local_data.sensors);
            // repeat approximately evey ~5 seconds
            for (int k = 0; k < 500; k++) {
                iotconnect_sdk_receive();
                usleep(10000); // 10ms
            }
        }
        iotconnect_sdk_disconnect();

    }
    int ret = 0;

MAIN_END:
    free_iotc_config(config);

    free_local_data();

    return ret;
}

