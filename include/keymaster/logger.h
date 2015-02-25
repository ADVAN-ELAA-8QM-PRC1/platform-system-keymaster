/*
 * Copyright 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SYSTEM_KEYMASTER_LOGGER_H_
#define SYSTEM_KEYMASTER_LOGGER_H_

#include <stdarg.h>

namespace keymaster {

class Logger {
  public:
    Logger() {}
    virtual ~Logger() {}

    enum LogLevel {
        DEBUG_LVL,    // Messages used only for debugging
        INFO_LVL,     // Informational messages; something is unusual but not wrong
        WARNING_LVL,  // There's an indication of trouble, but it may be okay.
        ERROR_LVL,    // A problem has occurred, but processing can continue
        SEVERE_LVL,   // A severe problem has occurred; likely indicates a defect.
    };

    virtual int log_msg(LogLevel level, const char* fmt, va_list args) const = 0;

    // These methods are deprecated.  Use the static methods or, better yet, the macros.
    int log(LogLevel level, const char* fmt, ...) const;
    int debug(const char* fmt, ...) const;
    int info(const char* fmt, ...) const;
    int warning(const char* fmt, ...) const;
    int error(const char* fmt, ...) const;
    int severe(const char* fmt, ...) const;

    static int Log(LogLevel level, const char* fmt, va_list args);
    static int Log(LogLevel level, const char* fmt, ...);
    static int Debug(const char* fmt, ...);
    static int Info(const char* fmt, ...);
    static int Warning(const char* fmt, ...);
    static int Error(const char* fmt, ...);
    static int Severe(const char* fmt, ...);

  protected:
    static void set_instance(Logger* logger) { instance_ = logger; }

  private:
    // Disallow copying.
    Logger(const Logger&);
    void operator=(const Logger&);

    static Logger* instance_;
};

#ifdef DEBUG

#define STR(x) #x
#define STRINGIFY(x) STR(x)
#define FILE_LINE __FILE__ ":" STRINGIFY(__LINE__) ": "

#define LOG_D(fmt, ...) Logger::Debug(FILE_LINE fmt, __VA_ARGS__)
#define LOG_I(fmt, ...) Logger::Info(FILE_LINE fmt, __VA_ARGS__)

#else  // not DEBUG

// For production builds, don't bloat the executable with FILE_LINE strings or DEBUG or INFO log
// messages.
#define FILE_LINE
#define LOG_D(fmt, ...)
#define LOG_I(fmt, ...)

#endif  // not DEBUG

#define LOG_W(fmt, ...) Logger::Warning(FILE_LINE fmt, __VA_ARGS__)
#define LOG_E(fmt, ...) Logger::Error(FILE_LINE fmt, __VA_ARGS__)
#define LOG_S(fmt, ...) Logger::Severe(FILE_LINE fmt, __VA_ARGS__)

}  // namespace keymaster

#endif  // SYSTEM_KEYMASTER_LOGGER_H_
