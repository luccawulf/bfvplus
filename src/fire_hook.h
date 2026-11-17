// The fire hook (plan-1b Tasks 5 and 9): each of FireArms::Fire's six calls to fireBarrel (0x0067D7E0)
// goes through a thunk that hands the round to the probe ([replay] probe) and to the replay recorder
// ([replay] recordReplays), then calls fireBarrel. It only observes. Patches nothing unless all six call
// sites and fireBarrel's entry match (bfv_addresses.h SITE_fireBarrel*). Install once, at start, and only
// when one of the two is on.
#pragma once

bool installFireHook();
