

#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

#ifdef DEBUG_COLORS
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define LOG_ERROR(fmt, ...)   printf(COLOR_RED "[ERROR] " fmt COLOR_RESET "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)    printf(COLOR_YELLOW "[WARNING] " fmt COLOR_RESET "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    printf(COLOR_GREEN "[INFO] " fmt COLOR_RESET "\n", ##__VA_ARGS__)
#else
#define COLOR_RESET   ""
#define COLOR_RED     ""
#define COLOR_GREEN   ""
#define COLOR_YELLOW  ""
#define COLOR_BLUE    ""
#define COLOR_CYAN    ""
#define LOG_ERROR(fmt, ...)   printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)    printf("[WARNING] " fmt "\n", ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#endif

#endif // LOGGING_H