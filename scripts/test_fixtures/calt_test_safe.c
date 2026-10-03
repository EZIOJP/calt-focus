#include <stdio.h>
#include <windows.h>
int main(void) {
  /* Must NEVER be on kill list ? misfire canary */
  for (int i = 0; i < 120; ++i) Sleep(1000);
  return 0;
}
