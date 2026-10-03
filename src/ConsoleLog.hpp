#pragma once

// Internal header: the default log destination of a world. It is not installed and everything in it sits
// in an anonymous namespace, so it adds no exported name to the library.

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#endif

namespace ecs
{
    namespace
    {
        // console-output: begin

        /*! The two standard streams the default destination can write to. */
        enum class Stream
        {
            Output,
            Error
        };

        /*! True when the stream is an interactive terminal. A closed or invalid stream is not interactive.
         *  On Windows the stream must be a console (a handle for which GetConsoleMode succeeds, which is not
         *  true of the null device or a file), and virtual terminal processing is switched on for it so the
         *  colour codes are interpreted; if that cannot be done the stream counts as not interactive. */
        inline bool streamIsTerminal(Stream stream)
        {
#ifdef _WIN32
            const HANDLE handle = GetStdHandle(stream == Stream::Output ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
            if(handle == nullptr || handle == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            DWORD mode = 0;
            if(!GetConsoleMode(handle, &mode))
            {
                return false;
            }
            return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
            return isatty(stream == Stream::Output ? STDOUT_FILENO : STDERR_FILENO) == 1;
#endif
        }

        /*! Colour is wanted only on an interactive terminal when the user has not asked for none. */
        inline bool colourWanted(bool isTerminal, bool noColour)
        {
            return isTerminal && !noColour;
        }

        /*! Where the default destination writes and whether each stream gets colour codes. The two
         *  streams are decided separately. */
        struct Console
        {
            std::ostream *output;
            std::ostream *error;
            bool colourOutput;
            bool colourError;

            /*! The standard streams, each coloured only when it is an interactive terminal and NO_COLOR is
             *  unset or empty. The decision is made here, once, and does not change if the stream is
             *  redirected later. */
            static Console fromEnvironment()
            {
                const char *value = std::getenv("NO_COLOR");
                const bool noColour = value != nullptr && value[0] != '\0';
                return Console{&std::cout,
                               &std::cerr,
                               colourWanted(streamIsTerminal(Stream::Output), noColour),
                               colourWanted(streamIsTerminal(Stream::Error), noColour)};
            }
        };

        /*! A log destination writing "[level] message" to the console: error and warning to the error
         *  stream, everything else to the output stream. Where colour is on, the tag is wrapped in the colour
         *  code of the severity (error 91, warning 93, debug 97, anything else 92) and a reset. */
        inline std::function<void(const std::string &, const std::string &)> consoleLogger(Console console)
        {
            return [console](const std::string &message, const std::string &level) {
                const bool toError = level == "error" || level == "warning";
                std::ostream &stream = toError ? *console.error : *console.output;
                const bool colour = toError ? console.colourError : console.colourOutput;
                if(!colour)
                {
                    stream << "[" << level << "] " << message << std::endl;
                    return;
                }
                const char *code = "92";
                if(level == "error")
                {
                    code = "91";
                }
                else if(level == "warning")
                {
                    code = "93";
                }
                else if(level == "debug")
                {
                    code = "97";
                }
                stream << "\033[" << code << "m[" << level << "]\033[0m " << message << std::endl;
            };
        }

        // console-output: end
    }
}
