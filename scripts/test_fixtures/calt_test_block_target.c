#include <stdio.h>
#include <windows.h>
int main(void) {
  /* Stay alive until killed or ~120s ? disposable Arm kill target */
  for (int i = 0; i < 120; ++i) Sleep(1000);
  return 0;
}
