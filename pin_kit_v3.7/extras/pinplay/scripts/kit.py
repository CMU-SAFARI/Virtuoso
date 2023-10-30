#!/usr/bin/env python

# BEGIN_LEGAL
# BSD License
#
# Copyright (c)2015 Intel Corporation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.  Redistributions
# in binary form must reproduce the above copyright notice, this list of
# conditions and the following disclaimer in the documentation and/or
# other materials provided with the distribution.  Neither the name of
# the Intel Corporation nor the names of its contributors may be used to
# endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
# ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# END_LEGAL
#
#
# @ORIGINAL_AUTHORS: T. Mack Stallcup, Cristiano Pereira, Harish Patil, Chuck Yount
#
#
# $Id: kit.py,v 1.57 2015/06/12 22:20:26 tmstall Exp tmstall $

# Print out messages, including error messages
#
#

import subprocess
import sys
import os

# Local modules
#
import config
import msg
import util


class Kit(object):
    """Setup the path and pintools for the PinPlay kit."""

    # First, initalize all the variables in the kit to default values.
    #

    # Path to the top level directory of the kit.
    #
    path = ''

    # What type of a kit is this.
    #
    kit_type = config.PINPLAY

    # Chose the appropriate Pin binary/tool for this platform.
    #
    platform = util.Platform()
    if platform is None:
        msg.PrintAndExit(
            'Could not identify the OS of the system where scripts are being run.')
    if platform == config.WIN_NATIVE or platform == config.WIN_CYGWIN:
        # Windows
        pinbin = 'pin.exe'
        pintool = 'pinplay-driver.dll'  # Need to verify this is the correct name
        nullapp = 'nullapp.exe'
    else:
        # Linux
        pinbin = 'pinbin'
        pintool = 'pinplay-driver.so'
        nullapp = 'nullapp'

    # Some definitions for the kit.
    #
    default_dir = 'pinplay'
    pin = 'pin'
    type = 'PinPlay'
    prefix = ''

    # In case there are any default knobs needed for this pintool.
    #
    default_knobs = ''

    # Path to the Pin binary itself for both architectures.
    #
    pin_dir = os.path.join('extras', 'pinplay', 'bin')

    # Paths to the PinPlay driver for both architectures.
    #
    pinbin_intel64 = os.path.join('intel64', 'bin', pinbin)
    pinbin_ia32 = os.path.join('ia32', 'bin', pinbin)

    # Paths to the Pin binary itself for both architectures.
    #
    driver_intel64 = os.path.join(pin_dir, 'intel64', 'pinplay-driver.so')
    driver_ia32 = os.path.join(pin_dir, 'ia32', 'pinplay-driver.so')

    # Path to the shell scripts in this kit
    #
    script_path = config.pin_script_path

    # Path to simpoint
    #
    simpoint_path = os.path.join('extras', 'pinplay', 'PinPoints', 'bin')

    # Knobs which have the same behavior in the various kits, but a different
    # name in each kit.
    #
    knob_length = '-log:length'
    knob_skip = '-log:skip'
    knob_regions_epilog = '-log:regions:epilog'
    knob_regions_in = '-log:regions:in'
    knob_regions_out = '-log:regions:out'
    knob_regions_prolog = '-log:regions:prolog'
    knob_regions_warmup = '-log:regions:warmup'
    knob_pcregions_in = '-log:pcregions:in'
    knob_pcregions_out = '-log:pcregions:out'
    knob_pcregions_merge_warmup = '-log:pcregions:merge_warmup'

    # Is the binary 32-bit or 64-bit?  Only needed for the logging phase.
    #
    binary_type = config.ARCH_INVALID

    def __init__(self):
        """
        Method called when object is instantiated to initalize object.

        @return No return value
        """

        self.InitKit(self.script_path)

    def SetBinaryType(self, binary):
        """
        Set the file type: either 32-bit or 64-bit.

        @param binary Binary used to determine type

        @return No return value
        """

        self.binary_type = util.FileType(binary)

    def ValidDriver(self, path):
        """
        Is this a path to a kit with a valid pinplay driver?

        @param path Path to kit to be validated

        @return True if valid drivers found, otherwise exit with an error msg
        """

        # See if the 64-bit driver exists
        #
        # import pdb;  pdb.set_trace()
        arch = 'intel64'
        if os.path.isfile(os.path.join(path, self.driver_intel64)):

            # See if the 32-bit driver exists
            #
            arch = 'ia32'
            if os.path.isfile(os.path.join(path, self.driver_ia32)):
                return True

        # There is a valid 'pinbin' binary, or this method wouldn't get called, but
        # there isn't a valid pinplay-driver.
        #
        msg.PrintMsg('ERROR: The required PinTool \'' + self.pintool +
                     '\' for arch \'' + arch + '\' was not found.')
        msg.PrintMsg('Perhaps the PinPlay kit installation was incomplete. Check to make sure\n' \
            'there weren\'t any errors during the install.')
        sys.exit(1)

    def ValidKit(self, path):
        """
        Is this a path to a valid kit?

        A valid kit must contain both the binary 'pinbin' and the
        PinPlay driver 'pintool' for both intel64 and ia32.

        @param path Path to kit to be validated

        @return False if kit not valid, else the return value from self.ValidDriver()
        """

        if os.path.isdir(path):

            # See if the 64-bit pinbin binary exists
            #
            if os.path.isfile(os.path.join(path, self.pinbin_intel64)):

                # See if the 32-bit pinbin binary exists
                #
                if os.path.isfile(os.path.join(path, self.pinbin_ia32)):
                    return self.ValidDriver(path)
        return False

    def GetHomeDir(self):
        """
        Get the location defined by the user's 'home' parameter/option.
        """

        return config.pinplayhome

    def GetKitLocation(self, script_path):
        """
        Look for a kit in several locations, including the 'home' directory, if it's defined.

        @param script_path Path to scripts directory in a kit

        @return Path to PinPlay kit
        """

        # Get path to the default version of the kit in users
        # home directory.
        #
        # import pdb;  pdb.set_trace()
        home = os.path.expanduser("~")
        path = os.path.join(home, self.default_dir)

        # If default dir name not found in home directory, then try the default
        # in the current directory.
        #
        if not os.path.exists(path):
            path = os.path.join(os.getcwd(), self.default_dir)

        # If default dir name is not found in the current directory, then check
        # to see if this Python script resides in a valid kit.  If so, then use
        # this as the kit location.  Assume if the scripts are in a valid kit,
        # they reside in the directory:  $PINPLAYHOME/script_path,  where
        # PINPLAYHOME is the root directory of the kit.
        #
        if not os.path.exists(path):
            script_dir = util.GetScriptDir()
            base_dir = script_dir.replace(script_path, '')
            if base_dir != script_dir:
                path = base_dir

        # If a 'home' directory is given for the kit, override any kit
        # locations just discovered and use the location given in this
        # parameter.
        #
        kit_home_dir = self.GetHomeDir()
        if kit_home_dir:
            if kit_home_dir[0] == os.sep:
                # Absolute path name, use as is.
                #
                path = kit_home_dir
            else:
                # Else assume it's a directory in the users home directory.
                #
                path = os.path.join(home, kit_home_dir)

        return path

    def InitKit(self, script_path):
        """
        Get the path to a valid kit, the appropriate tool name and add several paths
        to the environment variable PATH required to find script/utilities.

        @param script_path Path to scripts directory in a kit

        @return No return value
        """

        self.path = self.GetKitLocation(script_path)

        # Check to see if it's a valid kit. If not, exit with an error.
        #
        if not self.ValidKit(self.path):
            msg.PrintMsg(
                'ERROR: Path to the ' + self.type + ' kit was not found.')
            msg.PrintMsg('Default kit location is: ' + \
                os.path.realpath(os.path.join(os.path.expanduser("~"), self.default_dir)))
            sys.exit(1)

        # Add several directories in the PinPlay kit to the environment variable PATH.
        #
        os.environ["PATH"] += os.pathsep + os.path.join(self.path,
                                                        self.script_path)
        if self.simpoint_path != self.script_path:
            os.environ["PATH"] += os.pathsep + os.path.join(self.path,
                                                            self.simpoint_path)

    def ArchSpecificDir(self, arch):
        """
        Get the architecture dependent directory where the pintools are located.

        @param arch Architecture of the binary/pinball kit is using

        @return Explicit path to directory
        """

        # import pdb;  pdb.set_trace()
        pintool_path = os.path.join(self.path, self.pin_dir)
        if arch == config.ARCH_IA32:
            pintool_path = os.path.join(pintool_path, 'ia32')
        elif arch == config.ARCH_INTEL64:
            pintool_path = os.path.join(pintool_path, 'intel64')
        else:
            msg.PrintAndExit('Could not identify the architecture of the pintools to run.\n' \
                'Perhaps you forgot to set the binary type using the parameter \'mode\'.')

        return pintool_path

    def SetPinTool(self, user_pintool, pinball=''):
        """
        Set the pintool to the users tool instead of the default for this kit.

        User can give either an explicit path to the tool or put the tool in
        the architecture dependent directory.  In either case, check to make
        sure the pintool exists.

        @param user_pintool User defined pintool to use in this kit
        @param pinball Optional - pinball kit is processing

        @return
        """

        if os.path.isfile(os.path.realpath(user_pintool)):
            self.pintool = user_pintool
        else:
            # If pinball is given, use it to find the architecture specific directory, 
            # othwise just use the parameter 'binary_type'.
            #
            if pinball:
                arch = util.FindArchitecture(pinball)
                tool = os.path.join(self.ArchSpecificDir(arch), user_pintool)
            else:
                tool = os.path.join(self.ArchSpecificDir(self.binary_type),
                                    user_pintool)
            if not os.path.isfile(os.path.realpath(tool)):
                msg.PrintAndExit(
                    'Could not find user defined pintool: ' + user_pintool)
            self.pintool = user_pintool

    def GetPinTool(self, pinball):
        """
        Get the path to the pintool for the required architecture.

        If a pinball is given to the method, figures out the correct
        architecture for the pintool from the pinball.

        @param pinball Pinball kit is processing

        @return Path to the pintool for this kit
        """

        # import pdb;  pdb.set_trace()
        if os.path.isfile(os.path.realpath(self.pintool)):
            # If the pintool already has an explicit path, possible if the user has defined the pintool,
            # then just use it as is.
            #
            pintool_path = self.pintool
        else:
            # Otherwise, assume the tool is in the architecture dependent pintool directory.
            #
            if pinball:
                arch = util.FindArchitecture(pinball)
            else:
                arch = self.binary_type
            pintool_path = os.path.join(self.ArchSpecificDir(arch), self.pintool)

        return pintool_path

    def GetPinToolKnob(self, pinball=''):
        """
        Get the knob required to add the pintool for this kit to the Pin command line.

        Some kits don't required a pintool knob.  If that the case, just return an empty string.
        Pin based kits require a pintool knob, so return it.

        @param pinball Optional - pinball kit is processing

        @return String, including '-t', which defines explict path to pintool
        """

        return ' -t ' + self.GetPinTool(pinball)

    def GetNullapp(self, basename):
        """
        Get the path to the nullapp for the required platform and architecture.

        @param basename Basename (file name w/o extension) of pinball to process

        @return Explicit path to nullapp
        """

        # Get explicit path to the correct nullapp for this arch.
        #
        arch = util.FindArchitecture(basename)
        nullapp_path = os.path.join(self.ArchSpecificDir(arch), self.nullapp)

        # import pdb;  pdb.set_trace()
        platform = util.Platform()
        if platform == config.WIN_CYGWIN:
            # Need to get path to nullapp using Windows format.  This is required
            # because SDE is a native Windows app and requires the path to be 
            # in Windows format.  However, the path set above is in Cygwin format,
            # hence it must be converted.
            #
            try:
                nullapp_path = subprocess.check_output(['cygpath', '-w',
                                                        nullapp_path])
            except (subprocess.CalledProcessError, OSError):
                msg.PrintAndExit(
                    'Could not get a valid Windows path from the Cygwin path to nullapp')

            # Use forward slashes for the directory separator in the Windows path
            # (which is acceptable) because backslashes are treated as the escape character.
            #
            nullapp_path = nullapp_path.replace('\\', '/')
            nullapp_path = nullapp_path.rstrip()

        # Final check to ensure it's a valid nullapp binary
        #
        if not os.path.isfile(nullapp_path):
            msg.PrintAndExit('Unable to find valid nullapp')

        return nullapp_path
