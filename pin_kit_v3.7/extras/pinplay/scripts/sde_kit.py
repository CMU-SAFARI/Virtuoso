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
# $Id: sde_kit.py,v 1.47 2015/06/12 22:20:26 tmstall Exp tmstall $

import sys
import os

# Local modules
#
import config
import kit
import msg
import util


class SDEKit(kit.Kit):
    """
    Get the path and pintool for an SDE kit
    """

    # Path to the top level directory of the kit.
    #
    path = ''

    # What type of a kit is this.
    #
    kit_type = config.SDE

    # Chose the appropriate Pin binary/tool for this platform.
    #
    platform = util.Platform()
    if platform is None:
        msg.PrintAndExit(
            'Could not identify the OS of the system where scripts are being run.')
    if platform == config.WIN_NATIVE or platform == config.WIN_CYGWIN:
        # Windows/Cygwin
        pinbin = 'pin.exe'
        pintool = 'sde-pinplay-driver.dll'
        nullapp = 'nullapp.exe'
        simpoint_path = os.path.join('pinplay-scripts', 'PinPointsHome',
                                     'Windows', 'bin')
    else:
        # Linux
        pinbin = 'pinbin'
        pintool = ''  # No pintool required for SDE, it has a default tool 'sde-mix-mt.so'.
        nullapp = 'nullapp'
        simpoint_path = os.path.join('pinplay-scripts', 'PinPointsHome',
                                     'Linux', 'bin')

    # Some definitions for the kit.
    #
    default_dir = 'SDE'
    pin = 'sde'
    type = 'SDE'
    prefix = '-p'

    # Any additional knobs required by the kit.  Usually not defined.
    #
    default_knobs = ''

    # Paths to the Pin binary itself for both architectures.
    #
    pin_dir = ''
    pinbin_intel64 = os.path.join('intel64', pinbin)
    pinbin_ia32 = os.path.join('ia32', pinbin)

    # Path to the shell scripts in this kit
    #
    script_path = config.sde_script_path

    # Knobs which have the same behavior in the various kits, but a different
    # name in each kit.

    # New names for knobs when the PinPlay controller is replaced by the SDE controler.
    #
    knob_length = '-length'
    knob_skip = '-skip'
    knob_regions_epilog = '-regions:epilog'
    knob_regions_in = '-regions:in'
    knob_regions_out = '-regions:out'
    knob_regions_prolog = '-regions:prolog'
    knob_regions_warmup = '-regions:warmup'
    knob_pcregions_in = '-pcregions:in'
    knob_pcregions_out = '-pcregions:out'
    knob_pcregions_merge_warmup = '-pcregions:merge_warmup'

    # Original knob names.
    #
    # knob_length = '-log:length'
    # knob_skip   = '-log:skip'
    # knob_regions_epilog = '-log:regions:epilog'
    # knob_regions_in     = '-log:regions:in'
    # knob_regions_out    = '-log:regions:out'
    # knob_regions_prolog = '-log:regions:prolog'
    # knob_regions_warmup = '-log:regions:warmup'

    def __init__(self):

        self.InitKit(self.script_path)

    def ValidDriver(self, path):
        """
        For SDE, make sure the 'simpoint' binary is in the kit instead of
        verifying the drivers are valid.

        No need to verify drivers for SDE, as the SDE tool doesn't use a
        pinplay driver any more.  (There is a unified controller.)

        @param path Path to kit to be validated

        @return True if Simpoint found, otherwise exit with an error msg
        """

        # Chose the appropriate Pin binary/tool for this platform.
        #
        platform = util.Platform()
        if platform == config.WIN_NATIVE or platform == config.WIN_CYGWIN:
            path = os.path.join(path, self.simpoint_path, 'simpoint.exe')
        else:
            path = os.path.join(path, self.simpoint_path, 'simpoint')
        if not os.path.isfile(path):
            msg.PrintMsg(
                'ERROR: The required binary \'simpoint\' was not found.')
            msg.PrintMsg('   ' + path)
            msg.PrintMsg('Perhaps the SDE kit installation was incomplete. Check to make sure\n' \
                'there weren\'t any errors during the install.')
            sys.exit(1)

        return True

    def GetHomeDir(self):
        """
        Get the location defined by the user's 'home' parameter/option.
        """

        return config.sdehome

    def GetPinTool(self, pinball):
        """
        Just return the Pintool.  Even thought SDE no longer requires a pintool
        (because of unified controller) keep this method just in case it's needed.

        @param pinball Pinball kit is processing

        @return Path to the pintool for this kit
        """

        return self.pintool

    def GetPinToolKnob(self, pinball=''):
        """
        SDE by default does not need a pintool.  However, if user defines one
        then need to return a knob which uses this tool.

        @param pinball Optional - pinball kit is processing

        @return Either empty string or knob with user defined pintool
        """

        if self.pintool:
            return ' -t ' + self.pintool
        else:
            return ''

    def GetTraceinfoBlank(self):
        """
        Get the location of the traceinfo 'blank' XML file fragments.

        Need to remove two levels of directories names from self.simpoint_path to
        get the location where the fragments reside.

        @return Path to blank file fragments
        """

        path = os.path.dirname(os.path.dirname(self.simpoint_path))
        return os.path.join(self.path, path)
