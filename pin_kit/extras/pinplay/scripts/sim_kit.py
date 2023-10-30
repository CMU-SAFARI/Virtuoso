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
# $Id: sim_kit.py,v 1.10 2015/05/19 17:01:59 tmstall Exp tmstall $

#
# Kit which defines a simulator.
#

import sys
import os

# Local modules
#
import config
import msg
import util


class SimKit(object):
    """Setup the path, directories and binary for the x86noas simulator."""

    # Path to the top level directory of the kit.
    #
    sim_path = ''

    # What type of a kit is this.
    #
    kit_type = config.X86NOAS

    # Some definitions for the kit.
    #
    default_sim_dir = os.path.join('sims', 'latest_keiko')
    binary = 'x86noas'

    # Default knobs needed for this binary.
    #
    default_knobs = '-heartbeat 1000000'

    # Path to the configuration files.
    #
    config_dir = 'funcsim-config'

    def __init__(self):
        """Method called when object is instantiated to initalize object."""

        self.InitKit()

    def ValidKit(self, path):
        """
        Is this a path to a valid simulator?
        """

        # See if it's a valid path.
        #
        # import pdb;  pdb.set_trace()
        if os.path.isdir(path):

            # See if the configuration directory exists
            #
            if os.path.isdir(os.path.join(path, self.config_dir)):

                # See if the binary exists
                #
                if os.path.isfile(os.path.join(path, self.binary)):
                    return True

        return False

    def GetDefaultDir(self, kit_dir, home):
        """Look in several locations to find a kit."""

        # Get path to the default version of the kit in users
        # home directory.
        #
        # import pdb;  pdb.set_trace()
        user_home = os.path.expanduser("~")
        path = os.path.join(user_home, kit_dir)

        # If default dir not found in home directory, then try the default name
        # in the current directory.
        #
        if not os.path.exists(path):
            path = os.path.join(os.getcwd(), kit_dir)

        # If the path is set in the tracing configuration file, then override the default
        # value and use this instead.
        #
        # import pdb;  pdb.set_trace()
        if home:
            if home[0] == os.sep:
                # Absolute path name, use as is.
                #
                path = home
            else:
                # Else assume it's a directory in the users home directory.
                #
                path = os.path.join(user_home, home)

        return path

    def InitKit(self):
        """
        Get the path to a valid kit, the appropriate tool name and add a path
        to the environment variable PATH required to find the binaries.
        """

        # Get the paths to config directory.
        #
        # import pdb;  pdb.set_trace()
        self.sim_path = self.GetDefaultDir(self.default_sim_dir, config.simhome)

        # Check to see if it points to a valid kit.
        #
        if not self.ValidKit(self.sim_path):
            msg.PrintMsg(
                'ERROR: Path to the ' + self.binary + ' kit was not found.')
            msg.PrintMsg('Default kit location is: ' + \
                os.path.realpath(os.path.join(os.path.expanduser("~"), self.default_sim_dir)))
            msg.PrintMsg('Use option \'--simhome\' to define kit location.')
            sys.exit(1)

        # Add the binary's directory in the kit to the environment variable PATH.
        #
        # import pdb;  pdb.set_trace()
        os.environ["PATH"] += os.pathsep + self.sim_path

    def GetSimCfgFile(self, options):
        """
        Get the path to the processor configuration file for this simulator.
        """

        # import pdb;  pdb.set_trace()
        cfg_file = os.path.join(self.sim_path, self.config_dir,
                                options.processor)
        if not os.path.isfile(cfg_file):
            # Try addding the configuration file extension.
            #
            cfg_file = cfg_file + config.config_ext
            if not os.path.isfile(cfg_file):
                msg.PrintMsg('ERROR: Processor config file not found:\n     ' +  \
                    cfg_file + '\n' \
                    'Please make sure the processor TLA is correct and the simulators are installed correctly.')
                return ''

        return (cfg_file)
