// Last update: 21/06/2021 - 22:00
#include <stdio.h>
#include <stdint.h>

int version_cmd(int argc, char **argv);
int get_task_list_cmd(int argc, char **argv);
int startApp_cmd(int argc, char **argv);
int stopApp_cmd(int argc, char **argv);
int move_cmd(int argc, char **argv);
int remove_cmd(int argc, char **argv);
int list_file_cmd(int argc, char **argv);
int print_file_cmd(int argc, char **argv);
void printMemorySize(uint32_t size);
void print_heap_info();
int free_mem_cmd(int argc, char **argv);
int wlan_cmd(int argc, char **argv);
int wlan_ip_cmd(int argc, char **argv);
int cmd_wifi_scan(int argc, char **argv);
int cmd_wifi_auto(int argc, char **argv);
int cmd_wifi_forget(int argc, char **argv);
int cmd_shutdown(int argc, char **argv);
int interface_cmd(int argc, char **argv);
void register_commands(void);