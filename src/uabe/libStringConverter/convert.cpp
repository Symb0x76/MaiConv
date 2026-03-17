#include "convert.h"
#include <codecvt>
#include <cstring>
#include <locale>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

const char *_ConvertCPP_EmptyCHAR = "";
const wchar_t *_ConvertCPP_EmptyWCHAR = L"";
char *_WideToMultiByte(const wchar_t *wi, size_t &len) {
  if (!wi)
    return nullptr;
#ifdef _WIN32
  int wcLen = (int)(wcslen(wi) & 0x7FFFFFFF);
  int mbLen = WideCharToMultiByte(CP_UTF8, 0, wi, wcLen, NULL, 0, NULL, NULL);
  char *ret = new char[mbLen + 1];
  WideCharToMultiByte(CP_UTF8, 0, wi, wcLen, ret, mbLen, NULL, NULL);
  ret[mbLen] = 0;
  len = (size_t)mbLen;
  return ret;
#else
  try {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conv;
    std::string s = conv.to_bytes(wi);
    char *ret = new char[s.size() + 1];
    memcpy(ret, s.c_str(), s.size() + 1);
    len = s.size();
    return ret;
  } catch (const std::range_error &) {
    len = 0;
    return nullptr;
  }
#endif
}
wchar_t *_MultiByteToWide(const char *mb, size_t &len) {
  if (!mb)
    return nullptr;
#ifdef _WIN32
  int mbLen = (int)(strlen(mb) & 0x7FFFFFFF);
  int wcLen = MultiByteToWideChar(CP_UTF8, 0, mb, mbLen, NULL, 0);
  wchar_t *ret = new wchar_t[wcLen + 1];
  MultiByteToWideChar(CP_UTF8, 0, mb, mbLen, ret, wcLen);
  ret[wcLen] = 0;
  len = (size_t)wcLen;
  return ret;
#else
  try {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conv;
    std::wstring w = conv.from_bytes(mb);
    wchar_t *ret = new wchar_t[w.size() + 1];
    memcpy(ret, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
    len = w.size();
    return ret;
  } catch (const std::range_error &) {
    len = 0;
    return nullptr;
  }
#endif
}
wchar_t *_WideToWide(const wchar_t *wi, size_t &len) {
  if (!wi)
    return nullptr;
  len = wcslen(wi);
  wchar_t *ret = new wchar_t[len + 1];
  memcpy(ret, wi, (len + 1) * sizeof(wchar_t));
  return ret;
}
char *_MultiByteToMultiByte(const char *mb, size_t &len) {
  if (!mb)
    return nullptr;
  len = strlen(mb);
  char *ret = new char[len + 1];
  memcpy(ret, mb, len + 1);
  return ret;
}
void _FreeCHAR(char *c) {
  if (c != nullptr)
    delete[] c;
}
void _FreeWCHAR(wchar_t *c) {
  if (c != nullptr)
    delete[] c;
}
