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
# $Id: x86_kit.py,v 1.10 2015/05/19 17:01:59 tmstall Exp tmstall $

# Run x86.
#
#

import sys
import os

# Local modules
#
import config
import msg
import sim_kit
import util


class X86Kit(sim_kit.SimKit):
    """Setup the path, directories and binary for the x86 simulator."""

    # What type of a kit is this.
    #
    kit_type = config.X86

    # Some definitions for the kit.
    #
    binary = 'x86'
    default_sim_dir = os.path.join('sims', 'latest_keiko')
    default_archsim_dir = os.path.join('sims', 'latest_archsim',
                                       'skl-archsim-def-files', 'ww47-2011')

    # Default knobs needed for this binary.
    #
    default_knobs = '-heartbeat 10000000'

    # Path to the configuration files.
    #
    config_dir = 'config'

    def InitKit(self):
        """
        Get the path to a valid kit, the appropriate tool name and add a path
        to the environment variable PATH required to find the binaries.
        """

        # Get the paths to x86 and archsim config directories.
        #
        # import pdb;  pdb.set_trace()
        self.sim_path = self.GetDefaultDir(self.default_sim_dir, config.simhome)
        self.archsim_path = self.GetDefaultDir(self.default_archsim_dir,
                                               config.archsim_config_dir)

        # Check to see if it points to a valid kit.
        #
        if not self.ValidKit(self.sim_path):
            msg.PrintMsg(
                'ERROR: Path to the ' + self.binary + ' kit was not found.')
            msg.PrintMsg('Default kit location is: ' + \
                os.path.realpath(os.path.join(os.path.expanduser("~"), self.self.default_sim_dir)))
            msg.PrintMsg('Use option \'--simhome\' to define kit location.')
            sys.exit(1)
        if not os.path.isdir(self.archsim_path):
            msg.PrintMsg('ERROR: Path to the archsim config files used by the ' \
                + self.binary + ' kit was not found.')
            msg.PrintMsg('Default kit location is: ' + \
                os.path.realpath(os.path.join(os.path.expanduser("~"), \
                os.path.join(self.sim_dir, self.default_archsim_dir))))
            msg.PrintMsg(
                'Use option \'--archsim_config_dir\' to define this directory.')
            sys.exit(1)

        # Add the binary's directory in the kit to the environment variable PATH.
        #
        # import pdb;  pdb.set_trace()
        os.environ["PATH"] += os.pathsep + self.sim_path
