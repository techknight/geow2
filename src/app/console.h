#pragma once

namespace geow2 {

// On Windows the game is a window program, with no console of its own:
// started from a terminal, its standard output and error go to that
// terminal unless they were redirected. Elsewhere it does nothing.
void attach_parent_console();

}  // namespace geow2
