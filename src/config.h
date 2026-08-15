#pragma once

// Read bfvplus.ini from the same directory as BfVietnam.exe. Parses the
// [presetcolors] section and pushes the result into buddycolors via
// setPresetColors. If no INI is present, writes a default one with the
// built-in preset list.
void loadConfig();
