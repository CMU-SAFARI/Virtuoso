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
# $Id: msg.py,v 1.38 2015/05/19 17:10:03 tmstall Exp tmstall $

# Print out messages, including error messages
#
#

import os
import string
import sys
import subprocess


def PrintMsg(msg):
    """
    Prints a message to stdout.

    Use the method subprocess.Popen() in order to
    ensure the strings printed are in order.
    """

    # Import util.py, config.py here instead of at the top level to keep from having recusive module includes, as they
    # both import msg.
    #
    import util, config

    platform = util.Platform()

    # Remove any non-printing char
    #
    msg = filter(lambda x: x in string.printable, msg)

    # Escape the char '`' so the shell won't interpret it.
    #
    # import pdb;  pdb.set_trace()
    if '`' in msg:
        msg = msg.replace('`', '\`')
    if '"' in msg:
        # String 'msg' has embeded double quotes.  Need to use
        # single quotes to bracket 'msg'.
        #
        if platform == config.WIN_NATIVE:
            cmd = "echo " + msg
        else:
            cmd = "echo '" + msg + "'"
    else:
        # String 'msg' does not have embeded double quotes. Thus
        # can use double quotes to bracket 'msg'.
        #
        if platform == config.WIN_NATIVE:
            if msg == '':
                cmd = 'echo['
            else:
                cmd = 'echo ' + msg
        else:
            cmd = 'echo "' + msg + '"'
    p = subprocess.Popen(cmd, shell=True)
    p.wait()


def PrintMsgNoCR(msg):
    """
    Prints a message to stdout, but don't add CR.

    Use the method subprocess.Popen() in order to
    ensure the strings printed are in order.
    """

    # Import util.py here instead of at the top level to keep from having recusive module includes, as util
    # imports msg.
    #
    import util, config

    platform = util.Platform()

    # Remove any non-printing char
    #
    msg = filter(lambda x: x in string.printable, msg)

    # Escape the char '`' so the shell won't interpret it.
    #
    if '`' in msg:
        msg = msg.replace('`', '\`')
    # import pdb;  pdb.set_trace()
    if '"' in msg:
        # String 'msg' has embeded double quotes.  Need to use
        # single quotes to bracket 'msg'.
        #
        if platform == config.WIN_NATIVE:
            cmd = "echo " + msg
        else:
            cmd = "echo '" + msg + "'"
    else:
        # String 'msg' does not have embeded double quotes. Thus
        # can use double quotes to bracket 'msg'.
        #
        if platform == config.WIN_NATIVE:
            if msg == '':
                cmd = 'echo['
            else:
                cmd = 'echo|set /p=' + msg
        else:
            cmd = 'echo -n "' + msg + '"'
    p = subprocess.Popen(cmd, shell=True)
    p.wait()


def PrintAndExit(msg):
    """
    Prints an error message exit.

    Use the method subprocess.Popen() in order to
    ensure the strings printed are in order.
    """

    string = os.path.basename(sys.argv[0]) + ' ERROR: ' + msg + '\n'
    cmd = 'echo ; echo "' + string + '"'
    p = subprocess.Popen(cmd, shell=True)
    p.wait()
    sys.exit(-1)


def PrintHelpAndExit(msg):
    """ Prints an error message with help to stderr and exits """

    string = os.path.basename(sys.argv[0]) + ' ERROR: ' + \
                msg + '.\n' + 'Use --help to see valid argument options.\n'
    sys.stderr.write(string)
    sys.exit(-1)


def PrintMsgDate(string):
    """
    Print out a msg with three '*' and a timestamp.

    Use the method subprocess.Popen() in order to
    ensure the strings printed are in order.
    """

    import time

    pr_str = '***  ' + string + '  ***    ' + time.strftime('%B %d, %Y %H:%M:%S')
    PrintMsg('')
    PrintMsg(pr_str)


def PrintMsgPlus(string):
    """
    Print out a msg with three '+'.

    Use the method subprocess.Popen() in order to
    ensure the strings printed are in order.
    """

    pr_str = '+++  ' + string
    PrintMsg('')
    PrintMsg(pr_str)


def PrintStart(options, start_str):
    """If not listing, print a string."""

    if not options.list:
        # import pdb;  pdb.set_trace()
        PrintMsgDate(start_str)
