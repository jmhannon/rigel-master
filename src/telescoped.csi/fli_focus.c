/*
    FLI focuser support functions for focus.c

    Note: This file is brought into focus.c via #include
    Not meant to be added directly to project or compiled directly.
    See the use of flags controlling use of this code in the Makefile
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // for usleep

#include <libfli.h>

#define FLI_NO_DEVICE -1

// Handle to the FLI focuser, or FLI_NO_DEVICE if not connected
static flidev_t fliFocuser = FLI_NO_DEVICE;

// Upper limit of position, in counts. Populated on focuser initialization
static long maxPosition = 0; 

/*
   Search for any FLI USB focuser devices.

   If one is found, 1 will be returned, and the memory pointed 
   to by outDeviceName and outDevicePath will be populated with null-terminated
   strings containing a user-friendly device name and a path
   that can be passed to FLIOpen(). Use deviceNameMaxSize and
   devicePathMaxSize to specify the size of the allocated memory
   that is passed in for deviceName and devicePath -- returned strings will
   be truncated if they are longer than the max.

   If no device is found, return 0

   If there is a library error, return -1
*/
int find_fli_focuser(char *outDeviceName, char *outDevicePath, char *outErrorMsg, 
                   int deviceNameMaxSize, int devicePathMaxSize, int errorMsgMaxSize)
{
    int i;
    int err = 0;
    char **deviceNames;

    err = FLIList(FLIDOMAIN_USB | FLIDEVICE_FOCUSER, &deviceNames);
    if (err) {
        snprintf(outErrorMsg, errorMsgMaxSize, "FLIList error: %s\n", strerror(-err));
        return -1;
    }

    // NOTE - FLIList seems to recognize both focusers and filter wheels
    // when given either FLIDEVICE_FOCUSER or FLIDEVICE_FILTERWHEEL in the
    // domain argument. So we need to use the returned description to
    // narrow it down for ourselves.

    int deviceIndex = -1;

    for (i = 0; deviceNames[i] != NULL; i++) {
        char lowerDeviceName[1024];
        int j;

        // Create lowercase copy of name for case-insensitive comparison
        strncpy(lowerDeviceName, deviceNames[i], 1023);
        for (j = 0; j < 1023 && lowerDeviceName[j] != 0; j++) {
            lowerDeviceName[j] = tolower(lowerDeviceName[j]);
        }

        if (strstr(lowerDeviceName, "focus") != NULL) {
            deviceIndex = i;
            break;
        }
    }

    if (deviceIndex < 0) {
        snprintf(outErrorMsg, errorMsgMaxSize, "No focuser found. Check permissions on /dev/fli*\n");
        return 0;
    }

    char *devicePath = deviceNames[deviceIndex];
    char *deviceName = devicePath; // default

    /* FLIList returns strings in the form "devicePath;deviceName",
     * so we split things up here */
    for (i = 0; devicePath[i] != '\0'; i++) {
        if (devicePath[i] == ';') {
            devicePath[i] = '\0';
            deviceName = &devicePath[i+1];
        }
    }

    // Copy into buffers passed in by user so that we
    // can free the FLI-allocated memory
    strncpy(outDeviceName, deviceName, deviceNameMaxSize);
    strncpy(outDevicePath, devicePath, devicePathMaxSize);

    err = FLIFreeList(deviceNames);
    if (err) {
        snprintf(outErrorMsg, errorMsgMaxSize, "FLIFreeList error: %s\n", strerror(-err));
        return -1;
    }

    return 1; // Found a device
}


/* Disconnect from the focuser if currently connected.
   If the focuser is not connected, do nothing.
   Return 0 if error, 1 if success */
int fli_focus_shutdown()
{
    int err = 0;

    if (fliFocuser == FLI_NO_DEVICE) {
        return 1; // Not connected
    }

    err = FLIClose(fliFocuser);
    if (err) {
        fifoWrite(Focus_Id, -50, "FLIClose error: %s\n", strerror(-err));
        return 0;
    }

    return 1; // Successfully closed
}

/* Connect to FLI driver and focuser; return 0 if error, 1 if success */
int fli_focus_reset()
{
    int err = 0;
    char libver[1024];

    // Make sure we can talk to the library
    err = FLIGetLibVersion(libver, sizeof(libver));
    if (err) {
        fifoWrite(Focus_Id, -51, "FLI GetLibVersion error: %s", strerror(-err));
        return 0;
    }

    tdlog("FLI Library version '%s'\n", libver);

    // Close any active connection to the focuser
    err = fli_focus_shutdown();
    if (err == 0) {
        fifoWrite(Focus_Id, -52, "fli_focus_shutdown error");
        return 0;
    }

    char focusName[1024], focusPath[1024], focusError[1024];
    int foundFocuser = find_fli_focuser(focusName, focusPath, focusError,
                        sizeof(focusName), sizeof(focusPath), sizeof(focusError));

    if (foundFocuser == 1) {
        tdlog("Found FLI focuser: '%s' at '%s'\n", focusName, focusPath);
    }
    else {
        fifoWrite(Focus_Id, -53, "FLI focuser error: '%s'", focusError);
        return 0;
    }

    
    err = FLIOpen(&fliFocuser, focusPath, FLIDOMAIN_USB | FLIDEVICE_FOCUSER);
    if (err) {
        fifoWrite(Focus_Id, -54, "FLIOpen error: %s\n", strerror(-err));
        return 0;
    }

    tdlog("Connected to FLI focuser\n");

    err = FLIGetFocuserExtent(fliFocuser, &maxPosition);
    if (err) {
        fifoWrite(Focus_Id, -55, "FLIGetFocuserExtent error: %s\n", strerror(-err));
        return 0;
    }

    return 1;
}

/* Read the current focus position. Return 0 if error, 1 if success.
   If successful, populate *position with the current position. */
int fli_focus_read_position(int *position)
{
    int err = 0;
    long fliPosition = 0;

    err = FLIGetStepperPosition(fliFocuser, &fliPosition);
    if (err) {
        fifoWrite(Focus_Id, -56, "FLIGetStepperPosition error: %s\n", strerror(-err));
        return 0;
    }

    *position = (int)fliPosition;
    return 1;
}

/* Home the focuser. 
 * If first == 1, this is the first call that should kick off motion.
 * Otherwise we just monitor progress.
 *
 * Return:
 *   0 = Done
 *   1 = In progress
 *  -1 = Error
 */
int fli_focus_home(int first)
{
    int err;

    if (first) {
        // Kick off home process
        tdlog("FLI focuser homing...\n");

        err = FLIHomeDevice(fliFocuser);
        if (err) {
            fifoWrite(Focus_Id, -57, "FLIHomeDevice error: %s\n", strerror(-err));
            return -1;
        }
    }

    // Indicate to XObs that we are doing something (so that
    // position gets updated when we refresh mip later on)
    MotorInfo *mip = OMOT;
    mip->cvel = 1;


    // Are we done yet?
    long focusStatus;
    err = FLIGetDeviceStatus(fliFocuser, &focusStatus);
    if (err) {
        fifoWrite(Focus_Id, -59, "FLIGetDeviceStatus error: %s\n", strerror(-err));
        return -1;
    }

    if ((focusStatus & FLI_FOCUSER_STATUS_MOVING_MASK) == 0) {
        return 0; // Home complete!
    }

    // Still moving...
    return 1; 
}

/* Stop the focuser motion, if any.
 * Return 0 on error, 1 on success */
int fli_focus_stop()
{
    // Issue a relative move of 0 steps to stop the focuser
    int err = FLIStepMotorAsync(fliFocuser, 0);
    if (err) {
        fifoWrite(Focus_Id, -60, "FLIStepMotorAsync error: %s\n", strerror(-err));
        return 0;
    }

    OMOT->cvel = 0; // Report that we have stopped moving

    return 1;
}

int fli_focus_goto(int first, int target)
{
    int err;
    MotorInfo *mip = OMOT;

    if (first) {
        tdlog("FLI focuser moving to %ld counts...\n", target);
    }

    // FLI library only lets us make relative moves, and only 4096 counts
    // at a time. For slews longer than this, we need to make multiple
    // "jumps" -- slew 4096 steps, wait until complete, slew the next 4096
    // steps, etc., until we are within 4096, at which point we slew the
    // remaining number of steps to hit the target.

    // First, check to see if we are in the middle of a "jump"
    long focuserStepsRemaining;
    err = FLIGetStepsRemaining(fliFocuser, &focuserStepsRemaining);
    if (err) {
        fifoWrite(Focus_Id, -61, "FLIGetStepsRemaining error: %s\n", strerror(-err));
        return -1;
    }

    //tdlog("FLI focuser steps remaining: %d\n", focuserStepsRemaining);
    if (focuserStepsRemaining > 0) {
        // Still in the middle of one jump
        return 1;
    }

    // At the end of a jump or a final approach. Check to see if we
    // need another jump
    long focuserPosition;
    err = FLIGetStepperPosition(fliFocuser, &focuserPosition);
    if (err) {
        fifoWrite(Focus_Id, -62, "FLIGetStepperPosition error: %s\n", strerror(-err));
        return -1;
    }
    if (focuserPosition == target) {
        // We're done!!
        tdlog("FLI focus move complete; at %d\n", focuserPosition);
        mip->cvel = 0; // Tell xobs that we're no longer busy
        return 0;
    }

    tdlog("FLI focuser still moving from %d to %d\n", focuserPosition, target);
    // Still got work to do. How many steps do we need to go this time?
    long relativeSteps = target - focuserPosition;
    if (relativeSteps > 4095) {
        relativeSteps = 4095; // Clamp
    }
    if (relativeSteps < -4095) {
        relativeSteps = -4095; // Clamp
    }

    tdlog("FLI focuser next interval: %ld steps...\n", relativeSteps);

    err = FLIStepMotorAsync(fliFocuser, relativeSteps);
    if (err) {
        fifoWrite(Focus_Id, -63, "FLIStepMotorAsync error: %s\n", strerror(-err));
        mip->cvel = 1; // Tell xobs that we are moving
        return -1;
    }

    tdlog("FLI focuser still working...\n");
    return 1; // Still working
}

// cmd can be:
//      1 to jog positive
//     -1 to jog negative
//      0 to monitor
// Returns 1 on success, 0 on error
int fli_focus_jog(int cmd)
{
    static int lastDirection = 0;

    if (cmd == 1) {
        lastDirection = 1;
    }
    else if (cmd == -1) {
        lastDirection = -1;
    }

    int result;
    // Activate or monitor jog
    if (lastDirection == 1) {
        // Slew towards upper limit
        result = fli_focus_goto(0, maxPosition);
        if (result == -1) {
            return 0;
        }
        else {
            return 1;
        }
    }
    else if (lastDirection == -1) {
        // Slew towards lower limit
        result = fli_focus_goto(0, 0);
        if (result == -1) {
            return 0;
        }
        else {
            return 1;
        }
    }

    return 0; // Bad argument
}

int fli_focus_get_temp(double *tempOut)
{
    int err = FLIReadTemperature(fliFocuser, FLI_TEMPERATURE_INTERNAL, tempOut);
    if (err) {
        fifoWrite(Focus_Id, -64, "FLIReadTemperature error: %s\n", strerror(-err));
        return 0;
    }

    return 1;
}


/* Connect this implementation via our function pointers */
void register_fli_focuser()
{
    extFocus_reset_func = fli_focus_reset;
    extFocus_shutdown_func = fli_focus_shutdown;
    extFocus_read_position_func = fli_focus_read_position;
    extFocus_home_func = fli_focus_home;
    extFocus_stop_func = fli_focus_stop;
    extFocus_goto_func = fli_focus_goto;
    extFocus_jog_func = fli_focus_jog;
    extFocus_gettemp_func = fli_focus_get_temp;
}
