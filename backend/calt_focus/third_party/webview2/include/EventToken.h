#pragma once
/* Minimal EventRegistrationToken for WebView2.h under MinGW.
   Do NOT #include <eventtoken.h> here — on Windows that resolves to this
   same file (case-insensitive) and leaves the type undefined. */
#ifndef CALT_EVENTTOKEN_SHIM_
#define CALT_EVENTTOKEN_SHIM_
#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif
typedef struct EventRegistrationToken {
  int64_t value;
} EventRegistrationToken;
#endif
