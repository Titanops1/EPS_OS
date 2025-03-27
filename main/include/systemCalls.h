#include <stdio.h>
#include <string.h>
#include <stdint.h>

// OS-Implementierungen der Systemcalls
int sys_openqueue(const char *name);
int sys_findqueue(const char *name);
int sys_closequeue(int fd);
int sys_sendmsg(int fd, const char *msg, size_t len);
int sys_recvmsg(int fd, char *buffer, size_t len);
uint16_t getAppsRunning();
int8_t init_systemcalls();
void sys_led(int value);
void delay_ms(int ms);
void delay(int s);
void sys_led_mode(uint8_t mode);
float readGyroX();
float readGyroY();
float readGyroZ();
float readAccelX();
float readAccelY();
float readAccelZ();
void printNumber(int number);
void printString(const char *string);
void printChar(char c);
void printFloat(float f);
void printNewLine();

//OS Functions
uint16_t getAppsRunning();
void start_app();
int16_t checkAppRegister(const char *filename);
int16_t findFreeAppSlot();
int registerApp(const char *filename);
int unregisterApp(const char *filename);
int printAppList(int argc, char **argv);
void initApps();