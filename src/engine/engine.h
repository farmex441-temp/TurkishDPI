/*
 * TurkishDPI - the packet engine (TurkishDPI.exe --engine ...).
 */
#ifndef TURKISHDPI_ENGINE_H
#define TURKISHDPI_ENGINE_H

/* Runs the engine with its own command line (argv[0] is ignored); returns
 * the process exit code. Prints "Filter activated" once the filter is open,
 * "Error opening filter: <code>" when it cannot be, and - when
 * engine_report_stats is set - "STATS ..." counter lines for the app. */
int engine_main(int argc, char *argv[]);
extern int engine_report_stats;

#endif
