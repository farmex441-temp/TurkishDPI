#define HOST_MAXLEN 253    // Maximum length of a hostname according to DNS standards
#define MAX_PACKET_SIZE 9016   // Maximum packet size to process

#ifndef DEBUG
#define debug(...) do {} while (0)    // No-op debug function when not in debug mode
#else
#define debug(...) printf(__VA_ARGS__)    // Print debug messages when in debug mode
#endif

int gdpi_main(int argc, char *argv[]);    // Engine entry point (called by the TurkishDPI launcher)
void deinit_all();    // Clean up and free all resources before exiting
