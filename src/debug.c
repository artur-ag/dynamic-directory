#include <stdio.h>
#include <stdarg.h>

#include "globals.h"

/** \brief Prints a logging message, depending on the current verbose mode and the priority of the message.
 *
 * \param priority int Priority. If higher than global variable 'verbose', message will not be printed.
 * \param format const char* Format of the string to print, similar to printf().
 * \param ... Arguments to print, similar to printf().
 *
 */
void logm(int priority, const char* format, ...)
{
    va_list args;

    // Ignore low priority, verbose messages
    if (priority > verbose)
        return;

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

