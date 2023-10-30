#!/usr/bin/env python

# BEGIN_LEGAL
# BSD License
#
# Copyright (c)2016 Intel Corporation. All rights reserved.
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
# $Id: drd_util.py,v 1.16 2016/01/15 23:01:35 tmstall Exp tmstall $
#
# This module contains various utility functions used in the DrDebug scripts.

import fcntl
import getpass
import os
import re
import sys
import subprocess
import time

# Local modules
#
import cmd_options
import config
import msg
import util
"""
@package drd_util

Utility functions used for DeDebug scripts.
"""

# Attributes used in this module
#
gdb_path = ''  # Explicit path to GDB binary


def debug(parser):
    """
    Redefine this option because we don't want to confuse users
    by using the name 'debug' for an option which is used inside 
    the debugger GDB.

    @param parser Command line parser

    @return no return
    """

    parser.add_option(
        "-d", "--dry_run",
        dest="debug",
        action="store_true",
        default=False,
        metavar="COMP",
        help="Print out the command(s), but do not execute.  Also prints out diagnostic "
        "information as well.")


def AddAdditionalOptions(parser):
    """
    Add additional options which GDB version of scripts need.

    @param parser Optparse object

    @return No return value
    """

    cmd_options.debug_port(parser, '')
    cmd_options.gdb(parser, '')
    cmd_options.gdb_options(parser, '')


def CheckGdb(options, check_version=True):
    """
    Check GDB to see if it's compatible with the DrDebug environment.

    Make sure GDB is built with Python support and, if required, GDB version is
    new enough.  A base version of GDB is required because PinADX uses a new
    shared-library related remote command (only available in/after version 7.4)
    to convey shared library load locations etc to GDB.

    Prints a warning message if either check fails, but continues to run.

    NOTE: Sets global attribute gdb_path

    @return no return
    """

    def RunCmd(cmd):
        """
        Run a command and return stdout/stderrr.
        """

        p = subprocess.Popen(cmd,
                             shell=True,
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        stdout, stderr = p.communicate()

        return stdout, stderr

    global gdb_path

    # Get GDB from either the user option or the current PATH.
    #
    # import pdb;  pdb.set_trace()
    if options.gdb:
        gdb_path = util.Which(options.gdb)
    else:
        gdb_path = util.Which('gdb')
    if not gdb_path:
        if options.gdb:
            msg.PrintAndExit('gdb binary not found: ' + options.gdb)
        else:
            msg.PrintAndExit('gdb not found in PATH')

    # Is there Python support in GDB?
    #
    err_str = 'Python scripting is not supported in this copy of GDB'
    cmd = gdb_path + ' --batch -ex \'python print "hello"\''
    stdout, stderr = RunCmd(cmd)
    if err_str in stderr:
        python_support = False
        msg.PrintMsg(
            '\nWARNING: This version of gdb (%s) does not support Python.\n'
            'As a result, \'pin\' commands will not work.  Try \'monitor\' versions\n'
            'of the commands instead.\n' % (gdb_path))
    else:
        python_support = True

    if not check_version:
        return

    # Get version info from gdb
    #
    cmd = gdb_path + ' --version'
    stdout, stderr = RunCmd(cmd)

    # Parse the version string to get the actual version number.
    # Here's an example of the entire version string:
    #
    #   $ gdb --version
    #   GNU gdb (GDB) Red Hat Enterprise Linux (7.2-60.el6)
    #   Copyright (C) 2010 Free Software Foundation, Inc.
    #   License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
    #   This is free software: you are free to change and redistribute it.
    #   There is NO WARRANTY, to the extent permitted by law.  Type "show copying"
    #   and "show warranty" for details.
    #   This GDB was configured as "x86_64-redhat-linux-gnu".
    #   For bug reporting instructions, please see:
    #   <http://www.gnu.org/software/gdb/bugs/>.
    #
    line = stdout.split('\n', 1)[0]
    last = line.split()[-1]
    float_pattern = '[1-9][0-9]*\.?[0-9]*'
    f = re.search(float_pattern, last)
    version = f.group(0)
    major=int(version.split('.')[0])
    minor=int(version.split('.')[1])
    cmajor=int(config.gdb_base_version.split('.')[0])
    cminor=int(config.gdb_base_version.split('.')[1])

    # Check to make sure it's at least the base version required for DrDebug.
    #
    if (major < cmajor) or ((major == cmajor) and (minor < cminor)) :
        if python_support:
            msg.PrintMsg('\n')
        msg.PrintMsg(
            'WARNING: This version of gdb is: %s  When using versions of gdb < %s'
            '\nthe script will run, but with reduced functionality.\n' %
            (version, config.gdb_base_version))


def SetGdbCmdFile():
    """
    Get a user specific file name for the commands which will be used to invoke
    GDB.  This is a 'hidden' file which starts with the char '.'.

    @return file name
    """

    config.gdb_cmd_file = '.gdb.cmd.%s' % (getpass.getuser())

    # Create a new file or truncate the file if it already exists.
    #
    try:
        open(config.gdb_cmd_file, 'w').close()
    except:
        msg.PrintAndExit(
            'Unable to open gdb command file: ' + config.gdb_cmd_file)


def GdbInitialize(options, check_version=True):
    """
    Ensure the correct version of GDB is installed and initialize the GDB command file.

    Need to ensure running on a system with more than 1 core.  Must do this
    because scripts need to be able to run jobs in the background.  This is not
    possible if there's only 1 core.  [See util.RunCmd() for details]  This is
    only required for the GDB versions of the DrDebug scripts.

    @return no return
    """

    CheckGdb(options, check_version)
    SetGdbCmdFile()

    if util.NumCores() < 2:
        msg.PrintAndExit(
            """Unable to use GDB version of DrDebug scripts on systems with
              only 1 core.  If running on a VM with only one core, please reconfigure
              to provide at least 2 cores.""")


def DrDebugScriptCmd(script, pin_knobs, options):
    """
    Format the initial section of the script command line, including the
    appropriate script, used to execute the DrDebug low level scripts.  It formats
    commands for both logging and replay.  The parameter 'script' determines the
    'base' of the script to execute.

    The type of kit from which the script is called deterimines the specific
    lower level scripts to be called called.  If called are from an SDE kit,
    the string 'sde_' is prepended to the base script name.

    Also generate the explicit path to the directory in the specific kit
    where the scripts are located.

    @param script Base name of a lower level script to be run by current script
    @param pin_knobs List of Pin knobs (not pintool) used when running low level script
    @param options Options given on cmd line

    @return tuple with:
    @return     - string with command to run
    @return     - path to scripts in kit
    """

    # Get the type of kit from which the script was executed and print
    # out this info if desired.
    #
    # import pdb;  pdb.set_trace()
    kit_type, base_dir = util.GetKitType()
    if (hasattr(options, 'verbose') and options.verbose):
        string = 'Dir where was script found indicates kit type: '
        if kit_type == config.PINPLAY:
            string += 'PinPlay'
        elif kit_type == config.SDE:
            string += 'SDE'
        else:
            string += 'Unknown '
        string += ', located at: ' + base_dir
        msg.PrintMsg(string)

    # Add the appropriate 'home' directory for the kit to the command line.  If
    # user has given 'home' directory, then use it instead.  
    #
    kit_knob = ''
    if kit_type == config.PINPLAY:
        if config.pinplayhome:
            kit_knob += ' --pinplayhome ' + config.pinplayhome
        else:
            kit_knob += ' --pinplayhome ' + base_dir
        kit_script_path = os.path.join(base_dir, config.pin_script_path)
    elif kit_type == config.SDE:
        script = 'sde_' + script
        if config.sdehome:
            kit_knob += ' --sdehome ' + config.sdehome
        else:
            kit_knob += ' --sdehome ' + base_dir
        kit_script_path = os.path.join(base_dir, config.sde_script_path)
    else:
        # Should never get here as error checking already taken care of in
        # GetKitType()
        #
        pass
    if (hasattr(options, 'verbose') and options.verbose):
        msg.PrintMsg('Kit knob actually used: ' + kit_knob)

    # Start formatting command by adding the required Pin/SDE knobs.
    #
    cmd = script
    if kit_type == config.PINPLAY:
        popts = '-follow_execv'
    elif kit_type == config.SDE:
        popts = '-p -follow_execv'
    popts += pin_knobs

    # Add any user defined 'pin_options' 
    #
    if hasattr(options, 'pin_options') and options.pin_options:
        popts += ' ' + options.pin_options
    cmd += ' --pin_options "%s"' % (popts)

    # If use has given a pintool, add it & format final cmd
    #
    # import pdb;  pdb.set_trace()
    if hasattr(options, 'pintool') and options.pintool:
        cmd += ' --pintool ' + options.pintool
    cmd += kit_knob

    return cmd, kit_script_path


def GdbBaseLogOpt():
    """
    Get the DrDebug specific logging options.  When using GDB, there are additional
    knobs required in addition to the basic logging knobs.

    NOTE: SDE requires the pintool to be explicitly given on the command line when
    runnning with GDB.  Normally, the pintool isn't required for SDE.

    @return string with knobs for logging
    """

    kit_type, base_dir = util.GetKitType()
    if kit_type == config.PINPLAY:
        kit_knobs = ' -log:controller_default_start 0'
    else:
        kit_knobs = ' -t sde-pinplay-driver.so  -controller_default_start 0'
    log_opts = config.drdebug_base_log_options + kit_knobs + \
                ' -gdb:cmd_file ' + config.gdb_cmd_file

    return log_opts


def PintoolHelpCmd(cmd, options):
    """
    Print the pintool help msg using the script given in the
    string 'cmd'.

    @param cmd Command line to run lower level script
    @param options Options given on cmd line

    @return exit code from running cmd
    """

    # Do we want to print out the command executed by util.RunCmd() or
    # just run silently?
    #
    if options.verbose or options.debug:
        pcmd = True
    else:
        pcmd = False
    cmd += ' --pintool_help'
    result = util.RunCmd(cmd, options, '',
                         concurrent=False,
                         print_time=False,
                         print_cmd=pcmd)
    return result


def AddPinKnobsGDB(options):
    """
    Add pin knobs (not pintool knobs) needed when running with GDB.

    @return string of knobs
    """

    kit_type, base_dir = util.GetKitType()
    pin_knobs = ''

    # Add the string '-p' for each Pin option for SDE
    #
    if kit_type == config.PINPLAY:
        p = ''
    elif kit_type == config.SDE:
        p = '-p'

    # If user gives a port to use for GDB, then format the appdebug string to use it
    #
    if options.debug_port:
        pin_knobs += ' %s -appdebug %s -appdebug_silent %s -appdebug_server_port %s %s' % (
            p, p, p, p, options.debug_port)
    else:
        pin_knobs += ' %s -appdebug' % p

    return pin_knobs


def FinalizeNoGDB(kit_script_path, options):
    """
    Execute the final section of the program.  This version is for
    the scripts which don't use GDB.   If user gave option '--pid'
    then wait until the process exits.  Otherwise, just return.

    @param kit_script_path Explicit path to location in kit where scripts are located
    @param options Options given on cmd line

    @return no return
    """

    # If attaching to a process, spin here until the process has finished
    # running.
    #
    if (hasattr(options, 'pid') and options.pid):
        msg.PrintMsg('Waiting for process to exit, PID: ' + str(options.pid))
        while util.CheckPID(options.pid):
            if (hasattr(options, 'verbose') and options.verbose):
                msg.PrintMsgNoCR('.')
            time.sleep(5)
        if (hasattr(options, 'verbose') and options.verbose):
            msg.PrintMsg('\nProcess has exited')
        else:
            msg.PrintMsg('Process has exited')

    if (hasattr(options, 'verbose') and options.verbose):
        if hasattr(options, 'log_file') and options.log_file:
            msg.PrintMsg('Output pinball: ' + options.log_file)
        else:
            msg.PrintMsg('Output pinball: default file name (pinball/log)')

    return


def FinalizeGDB(kit_script_path, options):
    """
    Execute the final section of the program.  If the user has not explicitly
    given the port to use for communications between Pin and GDB, look in the
    GDB command output file for the string 'target remote'.  Timeout if not
    found in a reasonable amount of time.  Then add some more commands to the
    GDB command file and run GDB.

    @param kit_script_path Explicit path to location in kit where scripts are located
    @param options Options given on cmd line

    @return exit code from running GDB
    """

    # Format the string 'target_str' to use the appropriate port
    #
    if options.debug_port:
        # Use a specific port if the user defines one
        #
        # import pdb;  pdb.set_trace()
        target_str = 'target remote :%s' % options.debug_port
        gdb_file = open(config.gdb_cmd_file, 'w')
    else:
        # Use the port chose by Pin.  Pin will write it out to the
        # GDB command file.   Exit with an error if it's not there 
        # within 30 seconds.
        #
        target_str = 'target remote'
        timeout = 30
        count = 0
        found = False
        while count < timeout:
            with open(config.gdb_cmd_file, 'r') as gdb_file:
                if target_str in gdb_file.read():
                    found = True
                    break
            time.sleep(1)
            if hasattr(options, 'verbose') and options.verbose:
                msg.PrintMsg('Waiting for "target remote"')
            count += 1
        found = True
        if not found:
            msg.PrintAndExit('Unable to find GDB string \'%s\' in file %s' %
                             (target_str, config.gdb_cmd_file))
        time.sleep(1)
        if hasattr(options, 'verbose') and options.verbose:
            with open(config.gdb_cmd_file, 'r') as gdb_file:
                msg.PrintMsg('Target cmd:  ' + gdb_file.read())

        # Get the port selected by Pin
        #
        # import pdb;  pdb.set_trace()
        with open(config.gdb_cmd_file, 'r') as gdb_file:
            target_str = gdb_file.read()
        if hasattr(options, 'verbose') and options.verbose:
            msg.PrintMsg('Target cmd:  ' + target_str)
        gdb_file = open(config.gdb_cmd_file, 'w')

    # Write some control info and the command to load Pin Python file to the
    # GDB command file. Set PYTHONPATH to the location of the scripts.
    #
    # import pdb;  pdb.set_trace()
    time.sleep(1)
    gdb_file.write('set remotetimeout 30000\n')
    pin_python = os.path.join(kit_script_path, 'pin.py')
    gdb_file.write('source %s\n' % (pin_python))
    gdb_file.write('%s\n' % (target_str))
    gdb_file.close()
    time.sleep(1)
    os.environ["PYTHONPATH"] = kit_script_path
    if options.verbose:
        print "\nGDB cmd file:"
        os.system('cat ' + config.gdb_cmd_file)
        print

    # Format command and run GDB
    #
    cmd = gdb_path
    cmd += ' --command=%s' % config.gdb_cmd_file
    if hasattr(options, "gdb_options") and options.gdb_options:
        cmd += ' %s' % options.gdb_options
    cmd += ' %s' % options.command
    if hasattr(options, 'verbose') and options.verbose and \
       not options.debug:
        msg.PrintMsg(cmd)
    result = util.RunCmd(cmd, options, '',
                         concurrent=False,
                         print_time=False,
                         print_cmd=False)
    return result


def RunScriptBack(cmd, options):
    """
    Run the script in the background, not the foreground.

    Look for any errors from SDE which indicate there was a problem
    setting up the port used to connect SDE with GDB.  If detected,
    then print error msg and exit script.

    @param cmd Command line to run lower level script
    @param options Options given on cmd line

    @return result from running SDE
    """

    # Do we want to print out the command executed by util.RunCmd() or
    # just run silently?
    #
    if options.verbose or options.debug:
        pcmd = True
    else:
        pcmd = False

    result = 0
    if not options.debug:
        result = util.RunCmd(cmd, options, '',
                             concurrent=True,
                             print_time=False,
                             print_cmd=pcmd)
    return result


def RunScriptFore(cmd, options):
    """
    Run the script in the foreground.

    @param cmd Command line to run lower level script
    @param options Options given on cmd line

    @return error code from running script
    """

    result = 0
    if not options.debug:
        result = util.RunCmd(cmd, options, '',
                             concurrent=False,
                             print_time=False,
                             print_cmd=False)
    return result
