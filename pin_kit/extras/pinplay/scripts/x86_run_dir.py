#!/usr/bin/env python
#
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
# $Id: x86_run_dir.py,v 1.12 2015/05/19 19:48:55 tmstall Exp tmstall $

import sys
import os

# Local modules
#
import cmd_options
import config
import msg
import replay_dir
import sim_run_dir
import util
import x86_kit


class X86RunMulti(sim_run_dir.SimRunMulti):
    """
    Run x86 on multiple pinballs.

    This class is a wrapper which processes either one LIT file or the
    set of LIT files in a directory
    """

    # Script to replay one LIT file.
    #
    replayer_cmd = 'x86_run.py'

    def GetKit(self):
        """ Get the x86 simulator kit. """

        return x86_kit.X86Kit()

    def AddAdditionalOptions(self, parser):
        """Add any additional options besides the default for all *_dir.py scripts."""

        cmd_options.archsim_config_dir(parser, '')
        cmd_options.processor(parser, '')
        cmd_options.simhome(parser, '')
        cmd_options.sim_options(parser, '')


def main():
    """ Process command line arguments and run the replayer """

    replay = X86RunMulti()
    result = replay.Run()
    return result

# If module is called in stand along mode, then run it.
#
if __name__ == "__main__":
    result = main()
    sys.exit(result)
