#pragma once

#ifndef CPPGRAM_API
#    if !defined(CPPGRAM_DLL)
#        define CPPGRAM_API
#    elif defined(_WIN32) || defined(__CYGWIN__)
#        if defined(cppgram_EXPORTS)
#            define CPPGRAM_API __declspec(dllexport)
#        else
#            define CPPGRAM_API __declspec(dllimport)
#        endif
#    elif defined(__GNUC__) && __GNUC__ >= 4
#        define CPPGRAM_API __attribute__((visibility("default")))
#    else
#        define CPPGRAM_API
#    endif
#endif
