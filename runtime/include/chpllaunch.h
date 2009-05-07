#ifndef _CHPL_LAUNCH_H_
#define _CHPL_LAUNCH_H_

#include "chpltypes.h"

//
// Defined in main_launcher.c
//
void chpl_launch_using_system(char* command, char* argv0);


//
// Defined in chpl_cfg_launch.c
//
void chpl_launch(int argc, char* argv[], int32_t numLocales);
int chpl_launch_handle_arg(int argc, char* argv[], int argNum, 
                           int32_t lineno, chpl_string filename);

#endif
