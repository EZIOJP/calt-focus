#include "app.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  FocusApp app;
  return app.Run(instance);
}
