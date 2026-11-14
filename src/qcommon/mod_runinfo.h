#pragma once

// mod: what a run is played with, for speedruns and streams (cl_scrn_mp.cpp draws it on the loading screen and, with
// bo1_runinfo 2, in a corner in game). The hash identifies the program and the game files it loaded: SHA-512 over the
// executable's own SHA-512, then each zone the database loaded (name, CRC32 of its file, size), in name order. Anyone
// can build a program that prints another hash: it tells runs apart that use different files, it proves nothing.

#include <string>

// The database thread, after a zone file was read whole (db_file_load.cpp computes the CRC32 as it reads it).
void RunInfo_ZoneLoaded(const char *zoneName, unsigned int crc32, unsigned long long size);

// The current hash as 128 lowercase hex digits (the executable's alone before any zone), and the zones in it.
std::string RunInfo_Hash(int *zoneCount);

// The executable's own SHA-512 (128 hex digits, sha512sum's), or "" when it could not be read.
std::string RunInfo_ProgramHash();

// The settings that change a run, from their dvars, in one line ("mods: zinfo admin | god | 10000 points | ...");
// "standard" when none differs from the game's own.
std::string RunInfo_Options();
