#include "console.h"
#include "commands.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include <stdio.h>
#include <string.h>

#define INPUT_BUF_SIZE 256
#define MAX_ARGS       8

static char input_buf[INPUT_BUF_SIZE];
static int  input_len     = 0;
static bool was_connected = false;

static void print_prompt(void) {
    if (commands_is_admin()) {
        printf("hslock [admin]> ");
    } else {
        printf("hslock> ");
    }
    fflush(stdout);
}

static void process_line(void) {
    char *argv[MAX_ARGS];
    int   argc = 0;
    char *p    = input_buf;

    while (*p && argc < MAX_ARGS) {
        while (*p == ' ')
            p++; // skip whitespace
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ')
            p++; // find end of token
        if (*p)
            *p++ = '\0';
    }

    if (argc == 0)
        return;

    commands_dispatch(argc, argv);
}

void console_init(void) {
    // Nothing to do yet - USB stdio enabled via CMakeLists
}

void console_task(void) {
    bool connected = stdio_usb_connected();

    // USB just connected
    if (connected && !was_connected) {
        printf("\r\n");
        printf("=============================\r\n");
        printf("  hslock serial console\r\n");
        printf("  type 'help' for commands\r\n");
        printf("=============================\r\n");
        print_prompt();
        was_connected = true;
    }

    // USB just disconnected - auto-logout
    if (!connected && was_connected) {
        commands_on_disconnect();
        input_len     = 0;
        was_connected = false;
        return;
    }

    if (!connected)
        return;

    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT)
        return;

    if (c == '\r' || c == '\n') {
        printf("\r\n");
        if (input_len > 0) {
            input_buf[input_len] = '\0';
            process_line();
            input_len = 0;
        }
        print_prompt();
    } else if ((c == 127 || c == '\b') && input_len > 0) {
        input_len--;
        printf("\b \b");
        fflush(stdout);
    } else if (c >= 32 && input_len < INPUT_BUF_SIZE - 1) {
        input_buf[input_len++] = (char)c;
        putchar(c);
        fflush(stdout);
    }
}
