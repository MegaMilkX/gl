#ifndef UTIL_WIN32_MODULE_HPP
#define UTIL_WIN32_MODULE_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>


HMODULE win32GetThisModuleHandle();
std::string win32GetThisModuleName();


#endif
