#include "signal_helper.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

bool process_is_alive(int pidId) { return (-1 != kill(pidId, 0)); }
