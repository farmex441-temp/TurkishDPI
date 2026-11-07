// Windows service management functions for GoodbyeDPI
int service_register(int argc, char *argv[]);    // Register and start the Windows service
void WINAPI service_main(DWORD argc, LPSTR *argv);       // Service entry point called by Windows
void WINAPI service_controlhandler(DWORD request);      // Handle service control events (stop/shutdown)
