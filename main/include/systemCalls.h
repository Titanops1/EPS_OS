#include <stdio.h>
#include <string.h>
#include <stdint.h>

// OS-Implementierungen der Systemcalls
uint16_t getAppsRunning();
void delay_ms(int ms);
void delay(int s);
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
int registerApp(const char *appname);
int unregisterApp(const char *appname);
int registerSysApp(void *func, const char *appname);
int unregisterSysApp(const char *appname);
int printAppList(int argc, char **argv);
void initApps();