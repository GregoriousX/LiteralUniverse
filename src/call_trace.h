#pragma once

namespace lu {

// Diagnostics (F12): the first press starts counting calls to the virtual methods of game classes
// whose names match a list of keywords; the second press writes the counts to the log and stops.
void ToggleCallTrace();

}  // namespace lu
