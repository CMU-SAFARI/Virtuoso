#!/usr/bin/env python

#BEGIN_LEGAL
# BSD License
#
# Copyright (c)2017 Intel Corporation. All rights reserved.
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
# $Id: util.py,v 1.132 2015/12/09 21:26:18 tmstall Exp tmstall $
#
# This module contains various utility functions used in the tracing script.

import datetime
import glob
import os
import re
import platform
import subprocess
import optparse
import string
import sys
import time
import types

# Local modules
#
import config
import msg

"""
@package util

Utility functions used PinPlay & DrDebug scripts.
"""

Config = config.ConfigClass()

#######################################################################
#
# These are global variables because there can only be one copy of these
# variables even when there are multiple instances of Util objects.
#
#######################################################################

# Number of concurrent jobs running.
#
jobs = 0

# The list of PIDs for the running processes.
#
running_pid = []

# The strings which are printed when each process is finished.  The strings in this
# list are in the same order as the processes which are running.
#
end_str = []


def Platform():
    """
    What type of platform is being used?

    @return config.LINUX/WIN_CYGWIN/WIN_NATIVE, None if unable to identify platform
    """

    name = os.name
    system = platform.system()

    result = None
    if name == 'nt':
        result = config.WIN_NATIVE
    elif name == 'posix':
        if 'CYGWIN_NT' in system:
            result = config.WIN_CYGWIN
        elif 'Linux' in system:
            result = config.LINUX

    return result


def WindowsNativeCheck(options):
    """
    If running on native Windows environment, the scripts currently only
    run on one core.  Force 'num_cores' to be 1 on this platform.

    Restriction to be removed once the scripts work on more cores.

    @return no return
    """

    platform = Platform()
    if platform == config.WIN_NATIVE and hasattr(options, 'num_cores'):
        options.num_cores = config.num_cores = 1

    return


def CheckNonPrintChar(argv):
    """
    Check a list of strings to see if it contains any non-printing characters.
    Exit with an error msg if one is found.

    @return no return

    """

    for s in argv:
        new_s = filter(lambda x: x in string.printable, s)
        if new_s != s:
            msg.PrintMsg('ERROR: Non-printable char found in command line option "%s".  Possibly the\n' \
                  'string "--" (two hyphens) which Windows has turned into the non-printable\n' \
                  'char "-" (dash).' % s)
            sys.exit(1)


def SetCombineDefault(options):
    """
    If a value for the option combine has not been already set, then set it
    to the default value.

    @return no return
    """

    if hasattr(options, 'combine') and options.combine == -1.0:
        options.combine = 0.5


def SetArch(options):
    """
    Translate the 'arch' string given by the user in options into the internal
    arch type used by the scripts.

    If user string is not a legal arch, set to 'config.ARCH_INVALID'.

    @return no return
    """

    if hasattr(options, 'arch') and options.arch:
        if 'intel64' in options.arch:
            options.arch = config.ARCH_INTEL64
        elif 'ia32' in options.arch:
            options.arch = config.ARCH_IA32
        else:
            options.arch = config.ARCH_INVALID


def CheckCombine(options):
    """
    Check to make sure the value for the attribute 'combine' is a legal value.
    Exit with an error msg if an illegal value is given.

    @return no return
    """

    if options.combine:
        value = float(options.combine)
        if value < 0.0 or value > 1.0:
            msg.PrintAndExit(
                'Scaling factor \'combine\' (%.3f) must be between 0.0 and 1.0'
                % value)

#################################################################
#
# Functions to run a job either in the foreground, or in the background.
# When jobs are run in the background, then need to wait for a given number
# of jobs to complete before starting more jobs.  Don't want to over
# subscribe the cores, only one job/core.
#
#################################################################


def JoinOptionsList(cmd):
    """
    For spawning processes using os.spawnv() to call Python, the options
    between double quotes (") must be put into just one element of the
    list. Turn the 'cmd' string into a list and consolidate all options
    between double quotes into one element.

    Currently not used, but kept in case it might be needed.

    @return list containing the elements of command
    """

    cmd = cmd.split()
    new_cmd = []
    in_quote = False
    quote_elem = ''
    for elem in cmd:
        pos = elem.find('"')
        if pos != -1:
            # Found the char "
            #
            if in_quote:
                # Found 2nd char "
                #
                quote_elem += ' ' + elem + ' '
                new_cmd.append(quote_elem)
                in_quote = False
            else:
                # Found 1st char "
                #
                quote_elem = elem
                in_quote = True
        else:
            # Did not find the char "
            #
            if in_quote:
                # Add to exising quoted element.
                #
                quote_elem += ' ' + elem + ' '
            else:
                # Add to the new cmd list.
                #
                new_cmd.append(elem)

    return new_cmd


def GetJob(options):
    """
    Wait for a job to complete. Then remove it from the lists.

    @return exit code from job
    @return -1 if job not found
    """

    global jobs, running_pid, end_str

    # Decode the tuple 'status' and get the corresponding end string.
    # status is a 3-element tuple containing:
    #   child's PID
    #   exit status info
    #   resource usage info
    #
    # os.wait3() only is available on Linux/Cygwin.  This code will NOT work
    # on native Windows.  run with '--num_cores 1' in order to not
    # invoke this code in native Windows.
    #
    status = os.wait3(0)  # Block and wait for a job to complete
    pid = status[0]
    result = status[1] >> 8
    try:
        num = running_pid.index(pid)
        string = end_str[num]  # Get end_str for the PID
    except ValueError:
        msg.PrintMsgPlus(
            'WARNING: util.GetJob() unable to find running PID: %d' % pid)
        string = ''
        result = -1

    # If there is an end string, then print it.
    #
    # import pdb;  pdb.set_trace()
    if string:
        msg.PrintMsgPlus('Finished processing: ' + string)
    if hasattr(options, 'verbose') and options.verbose:
        msg.PrintMsg('Concurrent job finished, PID: ' + str(pid))

    # Remove values from the lists & decrement the number of
    # running jobs.
    #
    # import pdb;  pdb.set_trace()
    try:
        running_pid.remove(pid)
    except ValueError:
        pass  # Do nothing. Warning already printed out
    try:
        end_str.remove(string)
    except ValueError:
        pass
    jobs -= 1

    return result


def WaitJobs(options, wait_all=True):
    """
    Wait for either one or all processes concurrently running to complete.  Uses
    wait_all to determine which action to take.

    @return 0 if successfully waited for job(s)
    @return non-zero on error
    """

    global jobs, running_pid

    if hasattr(options, 'verbose') and options.verbose:
        msg.PrintMsgPlus('Waiting for background job(s) to finish: ' + str(jobs))

    if wait_all:

        # While there are still jobs running, wait for all them to complete.
        #
        # import pdb;  pdb.set_trace()
        result = 0
        while jobs > 0:

            # For each job left in the list of running jobs, see if it's finished.
            #
            # import pdb;  pdb.set_trace()
            while running_pid != []:

                # Wait for one of the running processes to finish. Then remove
                # it from the list of running jobs.  If the job ended in an
                # error, print a message and return an error.
                #
                # import pdb;  pdb.set_trace()
                result = GetJob(options)
                if result != 0:
                    msg.PrintMsg(
                        'WaitJobs() unexpected error occurred: non-zero exit code')
                    return result

            # Wait a while before checking again for finished jobs.
            #
            time.sleep(1)

        # All jobs should have finished by this point. Check to make sure
        # the number of jobs is indeed 0.
        #
        if jobs > 0:
            msg.PrintAndExit(
                'WaitJobs() unexpected error occurred: number of jobs runing > 0')
    else:

        # Only wait for one job to finish before returning. Remove it from the
        # list of running jobs.  If the job ended in an error, print a message
        # and return an error.
        #
        # import pdb;  pdb.set_trace()
        result = GetJob(options)
        if result != 0:
            msg.PrintMsg(
                'WaitJobs() unexpected error occurred: non-zero exit code')
            return result

    return result


def FormatCmd(cmd, print_time=True):
    """
    Format the command strings for the appropriate platform.

    'cmd' has added 'python' as the first element. However the binary 'simpoint.exe' is not a Python
    script, so the first element must be removed.

    For native Windows, must use the explicit path name for the Python script.

    @param cmd command to run in the background
    @param print_time if true, get time required to run the command

    @return command string to run on this platform
    """

    platform = Platform()
    if platform == config.WIN_NATIVE:
        # import pdb;  pdb.set_trace()
        command = JoinOptionsList(cmd)
        if '.py' in command[0]:
            command[0] = os.path.join(GetScriptDir(), command[0])
            cmd = ['python'] + command
        else:
            cmd = command
    elif platform == config.LINUX or platform == config.WIN_CYGWIN:
        if print_time:
            cmd = 'time ' + cmd
        cmd = ['bash', '-c', cmd]
    return (cmd)


def BackgroundRunCmd(cmd, print_time=True, print_cmd=True):
    """
    Run a job in the background using the appropriate process for the
    current platform.

    @param cmd command to run in the background
    @param print_time if true, get time required to run the command
    @param print_cmd if true, print the command before executing it

    @return PID of running job
    """

    if print_cmd:
        msg.PrintMsg(''.join(cmd))
    cmd = FormatCmd(cmd, print_time)
    platform = Platform()
    if platform == config.WIN_NATIVE:
        # Use os.spawnv() to run python to execute the script.
        #
        # First, must turn the string cmd into a list, consolidating
        # all options between double quotes into one element in the
        # list.  Then prepend the string 'python' to the list as this
        # needs to be the first string passed to os.spawnv().
        #
        pid = os.spawnv(os.P_NOWAIT, sys.executable, cmd)
    elif platform == config.LINUX or platform == config.WIN_CYGWIN:
        # Add time to the command string.  os.spawnvp() uses bash to run
        # the command 'time' which then executes the command.  It's assumed
        # that all Linux machines have bash.
        #
        pid = os.spawnvp(os.P_NOWAIT, 'bash', cmd)

    return pid


def RunCmd(cmd, options, string, concurrent=False, print_time=True, print_cmd=True, \
           f_stdout=None, f_stderr=None):
    """
    Execute a command and return the exit code.

    If the job is to be run concurrently, check to see if there is an
    unused core on which to run the job.  If there is a free core on
    which to run a job, run it in the background. Return once the job
    is started in the background.

    If a core is not available, then wait until there is a core available
    and then run the job in the background.

    If not running concurrently, run the job and wait until it completes
    before returning.

    If stdout/stderr are defined, then run the command using these values.

    @param cmd command to run
    @param concurrent controls if job should be run in background
    @param print_time if true, print time required to run each command
    @param print_cmd if true, print the command before executing it
    @param f_stdout file pointer for standard output
    @param f_stderr file pointer for standard error

    @result 0 if successful
    @result Non-zero if an error occurs,
    """

    global jobs, running_pid, end_str

    # If just listing the command or debugging, then print it, but
    # don't execute it.
    #
    result = 0
    if (hasattr(options, 'list') and options.list) or \
       (hasattr(options, 'debug') and options.debug):
        msg.PrintMsg(''.join(cmd))
    else:
        # If user specific num_cores, use it.  Else use the
        # number of cores in the system.
        #
        if config.num_cores > 0:
            max_cores = config.num_cores
        else:
            max_cores = NumCores()

        # If the user has specified to only use one core (or there really is
        # only one core) then run the job serially.
        #
        # import pdb;  pdb.set_trace()
        if concurrent and max_cores > 1:
            # If running concurrently, either run the job or wait
            # until it can be run.
            #
            if jobs >= max_cores:
                if hasattr(options, 'verbose') and options.verbose:
                    msg.PrintMsg('Calling WaitJobs() from RunCmd()')
                result = WaitJobs(options, wait_all=False)

            # There is a free core, so start the current job and add it to
            # the list 'running_pid'.  Also add the string to list 'end_str'.
            #
            if hasattr(options, 'verbose') and options.verbose:
                msg.PrintMsgPlus('Running job in background')
            if print_cmd:
                msg.PrintMsgPlus('Processing: ' + string)
            jobs += 1
            end_str.append(string)
            pid = BackgroundRunCmd(cmd, print_time, print_cmd)
            running_pid.append(pid)
            if hasattr(options, 'verbose') and options.verbose:
                msg.PrintMsg('Job PID: %d' % pid)
        else:

            # Run the process in the foreground.  The method communicate()
            # waits until the process completes.
            #
            # import pdb;  pdb.set_trace()
            if hasattr(options, 'verbose') and options.verbose:
                msg.PrintMsgNoCR('Starting serial job: ')
            if print_cmd:
                msg.PrintMsg(cmd)
            cmd = FormatCmd(cmd, print_time)
            p = subprocess.Popen(cmd,
                                 stdout=f_stdout,
                                 stderr=f_stderr,
                                 shell=False)
            if hasattr(options, 'verbose') and options.verbose:
                msg.PrintMsg('Job PID: %d' % p.pid)
            p.communicate()
            if string:
                msg.PrintMsgDate(string)
            result = p.returncode
            if hasattr(options, 'verbose') and options.verbose:
                msg.PrintMsg('Serial job finished, PID: %d ' % p.pid)

    return result

#################################################################
#
# Functions used to execute a given method on all pinballs in a set of
# directories.  These directories all contain the same string.
#
#################################################################


def GetAllIcount(dirname, file_name):
    """
    Get the instruction count for all threads in a pinball.

    Use the result file from the pinball to get 'inscount'.

    @return Sorted list (max->min) of tuples (icount, name). 
    @return None if error is detected
    """

    # Remove extensions '.address'/'result' and the TID.
    #
    file_name = ChangeExtension(file_name, '.address', '')
    file_name = ChangeExtension(file_name, '.result', '')
    file_name = RemoveTID(file_name)

    # Get the result file(s) for the pinball and the icount for each thread.
    #
    #import pdb ; pdb.set_trace()
    all_icount = []
    files = glob.glob(os.path.join(dirname, file_name + '*' + '.result'))
    for pfile in files:
        field = FindResultString(pfile, 'inscount:')
        icount = field[0]
        icountFound = (icount != None)
        if icountFound:
            icount = int(icount)
            pid = field[1]
            tid = field[2]
        else:
            icount = 0
        if icountFound:
            pfile = ChangeExtension(pfile, '.result', '')
            all_icount += [(icount, pfile)]
        else:
            # The icount was not found.
            #
            msg.PrintMsg('\nWARNING: util.GetAllIcount(), string \'inscount\' not found.\n' \
                'Check out possible corruption in pinball:\n' + '   ' + pfile)
            continue
    # Sort max->min by icount
    #
    all_icount.sort(key=lambda f: f[0], reverse=True)

    return all_icount


def GetMaxIcount(dirname, file_name):
    """
    Get the maximum instruction count for a pinball.

    Use the result file from the pinball to get 'inscount'.  For multi-threaded
    pinballs, get the icount for the thread with the most instructions.

    @return pinball instruction count, or 0 if an error occurs
    """

    # If the file name contains the extension '.address', then remove it.
    #
    file_name = ChangeExtension(file_name, '.address', '')

    # Get the result file(s) for the pinball and find the max icount.
    #
    # import pdb ; pdb.set_trace()
    max_icount = 0
    files = glob.glob(os.path.join(dirname, file_name + '*' + '.result'))
    for pfile in files:
        field = FindResultString(pfile, 'inscount:')
        icount = field[0]
        icountFound = (icount != None)
        if icountFound:
            icount = int(icount)
            pid = field[1]
            tid = field[2]
        else:
            icount = 0
        if icountFound:
            icount = int(icount)
        else:
            # The icount was not found.
            #
            msg.PrintMsg(
                '\nWARNING: util.GetMaxIcount(), string \'inscount\' not found, possible corruption in pinball:\n'
                + '   ' + pfile)
            continue
        if icount > max_icount:
            max_icount = icount

    return max_icount


def GetMinIcount(dirname, file_name):
    """
    Get the minimum instruction count for a pinball.

    Use the result file from the pinball to get 'inscount'.  For multi-threaded
    pinballs, get the icount for the thread with the least number of instructions.

    @return pinball instruction count, or 0 if an error occurs
    """

    # If the file name contains the extension '.address', then remove it.
    #
    file_name = ChangeExtension(file_name, '.address', '')

    # Get the result file(s) for the pinball and find the minimum icount.
    #
    # import pdb ; pdb.set_trace()
    min_icount = sys.maxint
    files = glob.glob(os.path.join(dirname, file_name + '*' + '.result'))
    for pfile in files:
        field = FindResultString(pfile, 'inscount:')
        icount = field[0]
        if icount:
            icount = int(icount)
            pid = field[1]
            tid = field[2]
        else:
            icount = 0
        if icount:
            icount = int(icount)
        else:
            # The icount was not found.
            #
            msg.PrintMsg(
                '\nWARNING: util.GetMinIcount(), string \'inscount\' not found, possible corruption in pinball:\n'
                + '   ' + pfile)
            continue
        if icount < min_icount:
            min_icount = icount

    # Check to make sure we really found an icount.  If not, this is
    # an error.
    #
    if min_icount == sys.maxint:
        min_icount = 0

    return min_icount

# Return code because the callback for os.path.walk() doesn't handle return values.
#
_util_result = 0


def walk_callback(param, dirname, fnames):
    """
    Callback used when walking the files in a directory to execute a method on
    each pinball.  Use the '.address' file for each pinball to identify the
    pinball.

    If the options.replay_filter is not blank, then skip any files which match
    this string, do not run the method on these files.

    @param param dict containing: 'options' and the metod executed on each pinball
    @param dirname directory containing the pinballs to process
    @param fnames the list of file names in the directory

    @return no return value, result of running method returned in '_util_result'
    """

    # Get items from the param dictionary.
    #
    if param.has_key('options'):
        options = param['options']
    else:
        msg.PrintAndExit(
            'function util.walk_callback() failed to get param \'options\'')
    if param.has_key('method'):
        method = param['method']
    else:
        err_msg('method')
        msg.PrintAndExit(
            'function util.walk_callback() failed to get param \'method\'')

    # Need to use the global _util_result because the return values from the callback
    # are not passed back to the calling function.
    #
    global _util_result
    _util_result = 0

    # Get just the address files.
    #
    file_list = []
    for addr_file in fnames:
        pos = addr_file.find(".address")
        if pos != -1:
            file_list.append(addr_file)

    # Sort the pinballs from high to low according to the icount given in
    # the pinball result file.  Use the icount as a 'decoration' to sort the
    # file names.
    #
    decor = []
    for f in file_list:
        tmp = (GetMaxIcount(dirname, f), f)
        if tmp[0] == 0:
            _util_result = -1
            return

        decor.append(tmp)
    decor.sort(reverse=True)
    sorted_list = []
    for tmp in decor:
        # Need to remove file extension '.address' before the file name
        # is put into the list.
        #
        sorted_list.append(ChangeExtension(tmp[1], '.address', ''))

    # import pdb ; pdb.set_trace()
    for basename_file in sorted_list:

        # Error check the results of the previous call to method().
        # If it had an error, then exit with the result code.
        #
        if _util_result != 0:
            return

        # See if this should be filtered out.
        #
        found = True
        if hasattr(options, 'replay_filter'):
            if options.replay_filter != "":
                if (basename_file.find(options.replay_filter) != -1):
                    found = False
                    if not options.list:
                        msg.PrintMsg('Filtering on string: ' + options.replay_filter + '\n' \
                                  'Ignoring pinball:    ' + os.path.join(dirname, basename_file))

        # Execute the method on the pinball.
        #
        if found:
            if hasattr(options, 'verbose') and options.verbose:
                # Need to remove '>' and '<' because they cause
                # problems on native Windows.
                #
                s = 'calling method: ' + str(method)
                s = s.replace('>', ' ').replace('<', '')
                msg.PrintMsg(s)
            _util_result = method(param, dirname, basename_file)


def RunAllDir(dir_string, method, no_glob, param):
    """
    Run 'method' on the pinballs in all directories which start with the string 'dir_string'.

    Uses os.path.walk() to run the method on all pinballs in each directory (or directories).

    @param dir_string string used to determine directories to process
    @param method function/method to be executed

    @param no_glob If true, don't expand the directory names in dir_string with
                   wildcards to include all directories.  Default is to expand
                   the file names unless this is set.

    @param param Dictionary containing object passed used in the function which
                 are used by 'method'.  Must at least contain an 'options' object.

    @return exit code from running methods
    """

    # import pdb ; pdb.set_trace()
    if dir_string:

        # Generate a list of directories.
        #
        dir_list = []
        if no_glob:
            # Don't expand the directory name.
            #
            dir_list.append(dir_string)
        else:
            # Find each directory which starts with 'dir_string'.
            #
            for path in os.listdir(os.getcwd()):
                if os.path.isdir(path) and path.startswith(dir_string):
                    dir_list.append(path)

        # Get 'param' from 'options' and add 'method'.
        #
        # import pdb ; pdb.set_trace()
        if param.has_key('options'):
            options = param['options']
        else:
            msg.PrintAndExit(
                'function util.RunAllDir() failed to get param \'options\'')
        param['method'] = method

        # Walk each directory in the list.
        #
        for path in dir_list:
            if config.debug and hasattr(options, 'list') and not options.list:
                msg.PrintMsg('Processing pinballs in directory \'' + path + \
                                      '\' using method: ' + str(method))
            if hasattr(options, 'verbose') and options.verbose:
                msg.PrintMsg('Calling os.path.walk() with path: %s' % path)
            os.path.walk(path, walk_callback, param)

            # Need to use the global _util_result because the return values from the callback
            # are not passed back to the calling function.
            #
            if _util_result != 0:
                return _util_result
    else:
        msg.PrintAndExit(
            'function util.RunAllDir() called with empty string \'dir_string\'')

    # If an error occurs, function already returned with the error code.
    #
    return 0

#################################################################
#
# Functions to search for a string in all the *.result files in a set
# of directories.
#
#################################################################


def FindResultString(filename, search_string, add_tid=False):
    """
    Get the value associated with 'search_string' from one result file.  If the boolean 'add_tid'
    is true, then add a TID to the file name before looking for the result file.

    Example files input names:

      whole_program.milc-test/milc.2.milc-test_68183.0.result

      milc.2.milc-test_68184.pp/milc.2.milc-test_68184_t0r7_warmup5000000_prolog346789_region2000017_epilog567890_007_0-06573.0.result

    The result files contain a key, value pair. For instance,

        inscount: 8002788

        focus_thread: 2

    @return list containing three items: [value, pid, tid]
    @return if item not found, return 'None'
    """

    # See if the file name contains PID/TID information according
    # to the default naming convention for WP pinballs.
    #
    pid = None
    tid = None
    pattern = '_[0-9]*\.[0-9]*\.result'
    # import pdb ; pdb.set_trace()
    if re.search(pattern, filename):
        # Get the PID/TID info.
        #
        string = re.findall(pattern, filename)
        string = string[0][1:]  # Get pattern as a string & remove leading '_'
        field = string.split('.')
        pid = field[0]
        tid = field[1]

    # If the file name doesn't have the file extension '.result', then add it to the name.
    # Also add TID if this is indicated.
    #
    # import pdb ; pdb.set_trace()
    if filename.find('result') == -1:
        if add_tid:
            # Sort so the TID 0 result file will be first, if there are more than one.
            #
            names = sorted(glob.glob(filename + '*.result'))
            if names != []:
                filename = names[0]
        else:
            filename += '.result'

    # If 'search_string' does not have a trailing ':', then add it. We are looking for the
    # key 'search_string', not just this string.
    #
    if search_string.find(':') == -1:
        search_string += ':'

    # Look for string 'search_string' and add the value of this key to the list 'result'.
    #
    # import pdb ; pdb.set_trace()
    val = None
    if os.path.isfile(filename):
        try:
            f = open(filename, 'r')
        except IOError:
            msg.PrintAndExit(
                'function util.FindResultString(), can\'t open file: ' + filename)

        for line in f.readlines():
            if line.find(search_string) != -1:
                val = line.split()[1]

    return [val, pid, tid]

def FindDynamicICount(filename, search_string):
    """
    Get the value associated with 'search_string' from one bb file.  

    The bb file contains  string of the following form
        Dynamic instruction count 617601078

    @return list containing two items: [tid, icount]
    @return if item not found, return 'None'
    """

    tid = None
    pattern = '[^.]*.T.([0-9][0-9]*).bb'
    # import pdb ; pdb.set_trace()
    sresult =  re.search(pattern, filename)
    val = None
    if sresult:
        # Get the TID info.
        #
        tid = sresult.group(1)
    if tid == None:
      sresult =  re.search('global', filename)
      if sresult:
        tid = 'global'

    val = FindStringRaw(filename, search_string)

    return [tid, val]

def FindStringRaw(filename, search_string):
    """
    Get the last word in the first line with 'search_string' from a file.

    @return the last word, if search_string found, else 'None'
    """

    val = None
    if os.path.isfile(filename):
        try:
            f = open(filename, 'r')
        except IOError:
            msg.PrintAndExit(
                'function util.FindStringRaw(), can\'t open file: ' + filename)

        for line in f.readlines():
            if line.find(search_string) != -1:
                wordlist = line.split()
                wordcount = len(wordlist)
                val = wordlist[wordcount-1]
    return val

def FindString(filename, search_string, add_tid=False):
    """
    Get the value associated with 'search_string' from a file.  
    If the boolean 'add_tid' is true, then add a TID to the file name 
    before looking for the result file.

    Example files input names:

      whole_program.milc-test/milc.2.milc-test_68183.0.result

      milc.2.milc-test_68184.pp/milc.2.milc-test_68184_t0r7_warmup5000000_prolog346789_region2000017_epilog567890_007_0-06573.0.result

    The result files contain a key, value pair. For instance,

        inscount: 8002788

        focus_thread: 2

    @return list containing three items: [value, pid, tid]
    @return if item not found, return 'None'
    """
    pid = None
    tid = None
    # If 'search_string' does not have a trailing ':', then add it. We are looking for the
    # key 'search_string', not just this string.
    #
    if search_string.find(':') == -1:
        search_string += ':'

    # Look for string 'search_string' and add the value of this key to the list 'result'.
    #
    # import pdb ; pdb.set_trace()
    val = FindStringRaw(filename, search_string)

    return [val, pid, tid]


def ProcessAllFiles(options, dir_name, search_string, file_string,
                    ignore_string, method):
    """
    Execute 'method' on all files in directory 'dir_name' which contain the
    string 'file_string', but not the string 'ignore_string'.

    @param options Options given on cmd line
    @param dir_name
    @param search_string
    @param ignore_string
    @param method

    @return A list of return values from 'method'.  One for each file which matches the
    search criteria.
    """

    if (config.verbose):
        msg.PrintMsg('Processing all files in ' + dir_name + ' which contains '\
                'the string \'' + file_string + '\' but not the string \'' + \
                ignore_string + '\'')

    # Look for all files in directory 'dir_name' which contain string
    # 'file_string' but does not contain 'ignore_string'.
    #
    result = 0
    files = []
    for path in glob.glob(os.path.join(dir_name, '*' + file_string + '*')):
        if path.find(ignore_string) != -1:
            continue  # Don't want this file
        else:
            files.append(path)

    # Sort the list of file names.
    #
    files.sort()

    # Run the method on each file which matched the requirements.
    #
    result = []
    for f in files:
        if (config.verbose):
            msg.PrintMsg('Processing file ' + f)
        # import pdb ; pdb.set_trace()
        result.append(method(f, search_string))

    return result


def PrintInstrCount(dirname, options):
    """
    Print out the instruction counts for all whole program pinballs.

    @return no return
    """

    # Setup locale so we can print integers with commas.
    #
    import locale
    locale.setlocale(locale.LC_ALL, "")

    # Get a list which contains a set of lists. Each member of the list contains:
    #     instruction count, PID and TID
    #
    # import pdb;  pdb.set_trace()
    p_list = ProcessAllFiles(options, dirname, 'inscount:', 'result', 'result_play', \
                  FindResultString)

    # Print out the instruction counts.
    #
    if not options.list:
        old_pid = -1
        gicount = 0
        msg.PrintMsg('Instruction count')
        for field in p_list:
            icount = field[0]
            pid = field[1]
            tid = field[2]
            if icount:
                icount = int(icount)
                gicount += icount
            else:
                # The icount was not found.
                #
                msg.PrintMsg('\nWARNING: In PrintInstrCount() string ' \
                    '\'inscount\' not found.  Possible corruption in a pinball.')
                icount = 0
                continue
            # Format output based on pid and tid
            #
            if pid:
                if pid != old_pid:
                    msg.PrintMsg(' Process: ' + pid)
                    old_pid = pid
            if tid:
                msg.PrintMsg('   TID: ' + tid + ' ' +
                             locale.format('%16d', int(icount), True))
            else:
                # Otherwise, just print the icount
                #
                msg.PrintMsg(locale.format('%16d', int(icount), True))
        msg.PrintMsg('   global:' +
          locale.format('%16d', int(gicount), True))


def GetNumThreadsWP(options):
    """
    Get the number of threads for result files in the tracing instance WP pinball directory.

    If there are pinballs with different number of threads, return the maximum number of
    threads found.

    If the pinball log file format is version 2.4, or lower, the number of
    threads will be in a *.result file.  However, as of version 2.5, this info
    is now in the *.global.log file.

    @return integer of max # threads
    @return -1 if # threads not found
    """

    # Get a list which contains a set of lists. Each member of the list contains:
    #     instruction count, PID and TID
    #
    p_list = ProcessAllFiles(options, GetWPDir(options), 'num_static_threads:', 'result', 'result_play', \
                  FindResultString)

    # import pdb ; pdb.set_trace()
    max_num_thread = -1
    for field in p_list:
        num_thread = field[0]
        if num_thread:
            num_thread = int(num_thread)
            if num_thread > max_num_thread:
                max_num_thread = num_thread
    if max_num_thread != -1:
        return max_num_thread

    p_list = ProcessAllFiles(options, GetWPDir(options), 'num_static_threads:', 'global.log', 'result_play', \
                  FindString)

    # import pdb ; pdb.set_trace()
    max_num_thread = -1
    for field in p_list:
        num_thread = field[0]
        if num_thread:
            num_thread = int(num_thread)
            if num_thread > max_num_thread:
                max_num_thread = num_thread
    return max_num_thread


def GetNumThreadsPB(filename):
    """
    Get the number of threads for result files in the tracing instance WP pinball directory.

    If there are pinballs with different number of threads, return the maximum number of
    threads found.

    @return integer of max # threads
    @return 0 if # threads not found
    """

    field = FindResultString(filename, 'num_static_threads:')

    # import pdb ; pdb.set_trace()
    num_thread = -1
    nthrd = field[0]
    if nthrd:
        num_thread = int(nthrd)

    return num_thread


def GetFocusThreadWP(options):
    """
    Get the focus thread for result files in the tracing instance WP pinball directory.

    If the pinball log file format is version 2.4, or lower, the focus thread
    info will be in a *.result file.  However, as of version 2.5, this info is
    now in the *.global.log file.

    @return integer with focus thread
    @return -1 if no focus thread found
    """

    # Get a list which contains a set of lists. Each member of the list contains:
    #     instruction count, PID and TID
    #
    p_list = ProcessAllFiles(options, GetWPDir(), 'focus_thread:', 'result', 'result_play', \
                  FindResultString)

    # If there is a focus thread then it should be the same for all TIDs.
    #
    # import pdb ; pdb.set_trace()
    ft = -1
    for field in p_list:
        ft = field[0]
        if ft:
            ft = int(ft)
    if ft != -1:
        return ft
    p_list = ProcessAllFiles(options, GetWPDir(), 'focus_thread:', 'global.log', 'result_play', \
                  FindString)

    # If there is a focus thread then it should be the same for all TIDs.
    #
    # import pdb ; pdb.set_trace()
    ft = -1
    for field in p_list:
        ft = field[0]
        if ft:
            ft = int(ft)
    return ft


def GetFocusThreadPB(filename):
    """
    Get the focus thread for a pinball.

    If the pinball log file format is version 2.4, or lower, the focus thread
    info will be in a *.result file.  However, as of version 2.5, this info is
    now in the *.global.log file.

    @return integer with focus thread
    @return -1 if no focus thread found
    """

    # First look in the *.result file
    #
    field = FindResultString(filename, 'focus_thread:')
    if not field[0]:
        # Then look in the *.global.result file
        #
        if '.global.log' not in filename:
            filename += '.global.log'
        field = FindString(filename, 'focus_thread:')

    ft = field[0]
    if ft:
        ft = int(ft)
    else:
        ft = -1

    return ft

#################################################################
#
# Functions which add options to a PinTool command line 
#
#################################################################


def AddCfgFile(options):
    """
    If this script was called with at least one configuration file, then
    add the configuration file(s) to the cmd string.

    @return string containing knob
    """

    string = ''
    if hasattr(options, 'config_file'):
        if options.config_file:
            for c_file in options.config_file:
                string += ' --cfg '
                string += c_file + ' '

    return string


def AddCfgFileList(options):
    """
    If this script was called with at least one configuration file, then
    add the configuration file(s) to the cmd list.

    @return string containing knob
    """

    string = ''
    if hasattr(options, 'config_file'):
        if options.config_file:
            tmp = ' --cfg '
            for c_file in options.config_file:
                tmp += c_file + ' '
            string += [tmp]

    return string


def AddCompressed(options):
    """
    If the option 'compressed' is defined, add it to the cmd string with the
    type of compression.

    @return string containing knob
    """

    string = ''
    if hasattr(options, 'compressed'):
        comp = options.compressed
        if comp == "none" or comp == "bzip2" or comp == "gzip":
            string = ' -log:compressed ' + comp + ' '
        else:
            msg.PrintHelpAndExit('Invalid compression: \'' + comp +
                                 '\' passed to -c or --compressed')

    return string


def AddGlobalFile(gfile, options):
    """
    Add a global file to the cmd string.

    @return string containing knob
    """

    string = ''
    if gfile:
        string = '  --global_file ' + gfile

    return string


def AddMt(options, file_name=None):
    """
    If necessary, add the knob for replaying multi-threaded pinballs.

    Use either the option 'mode' or figure it out from the type of the
    (optional) pinball.  If a pinball is given, it over rides the user option.

    Explicitly specify the knob '-log:mt 0' if mode is ST_MODE or pinball is
    single threaded.  Do this because the default for PinPlay is '-log:mt ON'.
    Logging with this knob enabled for PinPlay in single threaded pinballs
    significantly shows down the logging process and is not necessary.
    Explicitly specifying this knob for SDE is fine.

    @return string containing knob
    """

    mt = ' -log:mt 1'
    no_mt = ' -log:mt 0'
    string = ''
    if hasattr(options, 'mode'):

        # If the option 'mode' indicates it's a multi-threaded run, add the
        # multi-threaded knob.
        #
        if options.mode == config.MT_MODE or \
           options.mode == config.MPI_MT_MODE or \
           options.mode == config.MP_MT_MODE:
            string = mt

        # Need explicit knob to disable multi-thread processing
        #
        if options.mode == config.ST_MODE:
            string = no_mt

        # If the WP pinballs have been relogged with a focus thread, then
        # they are per thread.  In addition, any pinballs generated from
        # relogging WP pinballs with a focus thread will also be per
        # thread.
        #
        if hasattr(options, 'use_relog_focus') and options.use_relog_focus:
            string = no_mt

    if file_name != None:

        # Called with a pinball.  Figure out if it'a cooperative pinball and add
        # the knob if it is.
        #
        if IsCoopPinball(file_name):
            string = mt
        else:
            # Need explicit knob to disable multi-thread processing
            #
            string = no_mt

    return string


def AddNoGlob(options):
    """
    If the option 'no_glob' is defined, add it to the cmd string.

    @return string containing knob
    """

    string = ''
    if hasattr(options, 'no_glob') and options.no_glob:
        string = ' --no_glob'

    return string

#################################################################
#
# Helper Functions
#
#################################################################


def NativeWinRm(options, string):
    """
    Delete a file or a directory in native Windows.

    Unfortuantely, there isn't a command which does the same thing
    as "rm -rf string", so must delete files and directories using
    different commands.

    @return no return
    """
    platform = Platform()
    if platform != config.WIN_NATIVE:
        return

    if os.path.isfile(string):
        cmd = 'del /q ' + string
        if not options.debug:
            os.system(cmd)
        else:
            msg.PrintMsg('       ' + cmd)
            msg.PrintMsg('       Debugging so files/dirs were not deleted')
    else:
        files = glob.glob(string)
        if not files:
            return
        for f in files:
            if os.path.isfile(f):
                cmd = 'del /q /s '
            if os.path.isdir(f):
                cmd = 'rmdir /q /s  '
            cmd += string
            if not options.debug:
                os.system(cmd)
            else:
                msg.PrintMsg('       ' + cmd)
                msg.PrintMsg('       Debugging so files/dirs were not deleted')


def Delete(options, string):
    """
    Delete the directory/file given by 'string'.

    @return no return
    """

    if string == '':
        return
    msg.PrintMsgPlus('Deleting: ' + string)
    # import pdb ; pdb.set_trace()
    platform = Platform()
    if platform == config.WIN_NATIVE:
        NativeWinRm(options, string)
    else:
        # Same command for Linux/Cygwin
        #
        cmd = 'rm -rf '
        cmd += string
        if not options.debug:
            os.system(cmd)
        else:
            msg.PrintMsg('       ' + cmd)
            msg.PrintMsg('       Debugging so files/dirs were not deleted')


def NumCores():
    """
    Get the number of cores in the system.

    @return number of cores
    @return 1 if unable to determine number
    """

    # linux code
    #
    if hasattr(os, "sysconf"):
        if os.sysconf_names.has_key("SC_NPROCESSORS_ONLN"):
            num_cpus = os.sysconf("SC_NPROCESSORS_ONLN")
            if isinstance(num_cpus, int) and num_cpus > 0:
                return num_cpus
            else:
                # code for OS X
                #
                return int(os.popen2("sysctl -n hw.ncpu")[1].read())

    # Windows specific code
    #
    if os.environ.has_key("NUMBER_OF_PROCESSORS"):
        num_cpus = int(os.environ["NUMBER_OF_PROCESSORS"])
        if num_cpus > 0:
            return num_cpus

    # If can't find the number of cores, return 1.
    #
    return 1


def ParseMode(mode):
    """
    Parse logging mode, exiting with error msg if mode not valid.

    @return Mode type
    """

    try:
        return {
            'st': config.ST_MODE,
            'mt': config.MT_MODE,
            'mpi': config.MPI_MODE,
            'mpi_mt': config.MPI_MT_MODE,
            'mp': config.MP_MODE,
            'mp_mt': config.MP_MT_MODE
        }[mode]
    except:
        msg.PrintHelpAndExit('Invalid mode \'' + mode + '\' passed to --mode')


# Global boolean variable used to cache the information if running CBSP script, or PinPoints.
# Set to None before it's initialized.
#
_util_cbsp = None

def cbsp(self):
    """
    Are we running CBSP (Cross Binary SimPoints)?

    Assuming this method has been added to the opt.parse.Values object
    'options'.  Check to see if parameter 'cbsp_name' is defined.  First time
    this is run, cache the value.  Thereafter, used the cached value so don't
    have to always take an exception in PinPlay.

    @return True if running CBPS, else False
    """

    global _util_cbsp

    # import pdb ; pdb.set_trace()
    if _util_cbsp == None:
        try:
            if self.cbsp_name:
                _util_cbsp =  True
            else:
                _util_cbsp =  False
        except:
            _util_cbsp =  False

    return _util_cbsp


def AddMethodcbsp(options):
    """
    Add the cbsp() method to a object of type optparse.Values.
    """

    options.cbsp = types.MethodType(cbsp, options, optparse.Values)

    return options


def Which(program):
    """
    See if a program is in the user's path and if it's executable.

    @return string with path to program
    @return None if not found, or not executable
    """

    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)

    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file

    return None


def CheckPID(pid):
    """
    See if a process with given PID is still running.

    @return boolean
    """

    try:
        os.kill(pid, 0)
    except OSError:
        return False
    else:
        return True


def FileType(binary):
    """
    Determine if a binary is 32 or 64-bit.

    @return type of file (config.ARCH_INVALID/ARCH_INTEL64/ARCH_IA32)
    """

    import platform

    ftype = config.ARCH_INVALID
    info = platform.architecture(binary)
    for i in info:
        if i.find('64bit') != -1:
            ftype = config.ARCH_INTEL64
            break
        if i.find('32bit') != -1:
            ftype = config.ARCH_IA32
            break

    return ftype


def FindArchitecture(basename):
    """
    Find out if a pinball is from a 32 or 64-bit binary.

    @return arch of pinball (config.ARCH_INVALID/ARCH_INTEL64/ARCH_IA32)
    """

    if os.path.exists(basename + ".result.0"):
        filename = basename + ".result.0"
    elif os.path.exists(basename + ".0.result"):
        filename = basename + ".0.result"
    elif os.path.exists(basename + ".result"):
        filename = basename + ".result"
    else:
        msg.PrintAndExit('Can\'t find result.0 or 0.result for basename: ' + \
            basename)

    arch = config.ARCH_INVALID
    try:
        result_file = open(filename, "r")
    except IOError:
        msg.PrintAndExit('Can\'t open result file: ' + filename)

    line = result_file.readline()
    while line:
        if line.find("arch: x86_32") != -1:
            arch = config.ARCH_IA32
            break
        elif line.find("arch: x86_64") != -1:
            arch = config.ARCH_INTEL64
            break
        line = result_file.readline()
    result_file.close()
    
    if arch != config.ARCH_INVALID:
        return arch
    filename = basename + ".global.log"
    try:
        result_file = open(filename, "r")
    except IOError:
        msg.PrintAndExit('Can\'t open file: ' + filename)
    line = result_file.readline()
    while line:
        if line.find("arch: x86_32") != -1:
            arch = config.ARCH_IA32
            break
        elif line.find("arch: x86_64") != -1:
            arch = config.ARCH_INTEL64
            break
        line = result_file.readline()
    result_file.close()

    return arch


def GetScriptDir():
    """
    Get the directory where the PinPlay Python scripts are located.

    @return script directory
    """

    pathname = os.path.dirname(sys.argv[0])

    return os.path.realpath(os.path.abspath(pathname))


def AddScriptPath():
    """
    Add the directory from which a script is executed to the PATH
    environment variable.

    @return no return
    """

    os.environ["PATH"] += os.pathsep + GetScriptDir()


def RoundupPow2(a):
    """
    Round an integer up to the nearest power of 2.

    @return integer
    """

    if type(a) != type(0): raise RuntimeError, "Only works for ints!"
    if a <= 0: raise RuntimeError, "Oops, doesn't handle negative values"

    next_one_up = 1
    while (next_one_up < a):
        next_one_up = next_one_up << 1
    return next_one_up


def CleanupTraceEnd(options=None):
    """
    Do any cleanup required at the end of the script.

    @return no return
    """

    # Clean up any global data files which still exist.
    #
    gv = config.GlobalVar()
    gv.RmGlobalFiles(options)


def PrintTraceEnd(options=None):
    """
    Print a string at the end of the script.

    @return no return
    """

    # Print the list of options doesn't have the list attribute, or list is True.
    #
    if options.cbsp():
        string = "CBSP"
    else:
        string = "TRACING"
    if not hasattr(options, 'list') or (
       hasattr(options, 'list') and not options.list):
        pr_str = '***  %s: END' % (string) + '  ***    ' + time.strftime(
            '%B %d, %Y %H:%M:%S')
        msg.PrintMsg('')
        msg.PrintMsg(pr_str)

# Global which contains the timestamp when a phase starts.
#
phase_start = None


def PhaseBegin(options):
    """
    Save a timestamp when a phase starts.

    @return no return
    """

    global phase_start
    phase_start = datetime.datetime.now()


def IntermediatePhase(options):
    """
    Get a string which indictes the delta between the current time
    and the beginning of the phase stored in 'phase_start'.

    Does not reset the global 'phase_start' to zero.

    @return string with delta
    """

    global phase_start

    # import pdb ; pdb.set_trace()
    if phase_start != None:
        delta = datetime.datetime.now() - phase_start
    else:
        delta = None

    return delta


def PhaseEnd(options):
    """
    Get a string which indictes the delta between the current time
    and the beginning of the phase stored in 'phase_start'.

    @return string with delta
    """

    global phase_start

    # import pdb ; pdb.set_trace()
    if phase_start != None:
        delta = datetime.datetime.now() - phase_start
    else:
        delta = None
    phase_start = None

    return delta


def CheckResult(result, options, phase, intermediate_phase=False):
    """
    Check the result of running a given phase.  If it fails, print an error msg and
    exits with exit code -1.

    Also records in a status file if the phase passed or failed and the time
    used to run the current phase.

    @return no return
    """

    # Write the status of the phase to the appropriate file.
    #
    # import pdb ; pdb.set_trace()
    if options.cbsp():
        s_file = GetStatusFileName(options, cbsp=True)
    else:
        s_file = GetStatusFileName(options)
    try:
        f = open(s_file, 'a+')
    except IOError:
        msg.PrintAndExit('method util.CheckResult(), can\'t open status file: ' + \
            s_file)
    if not (hasattr(options, 'list') and options.list) and \
       not (hasattr(options, 'debug') and options.debug):
        if result == 0:
            f.write('Phase: ' + phase.ljust(75) + ': Passed ')
        else:
            f.write('Phase: ' + phase.ljust(75) + ': Failed ')

        # If a start time has been recorded, get the time required to run the
        # current phase.
        #
        if intermediate_phase:
            td = IntermediatePhase(options)
        else:
            td = PhaseEnd(options)
        if td != None:
            f.write('%s (%s.%06d)\n' % (str(td), ((td.seconds + \
                td.days * 24 * 3600) * 10 ** 6) / 10 ** 6, td.microseconds))
        else:
            f.write('\n')
    f.close()

    # If an error occurred, let the user know, clean up and quit with an error code.
    #
    if result != 0:
        if not (hasattr(options, 'list') and options.list) and \
           not (hasattr(options, 'debug') and options.debug):
            # import pdb ; pdb.set_trace()
            msg.PrintMsg("\n**************************************************"
                         "*********************************************")
            msg.PrintMsg(os.path.basename(sys.argv[0]) +
                         ' ERROR: A problem occurred in phase - ' + phase)

        # Cleanup and exit.
        #
        CleanupTraceEnd(options)
        PrintTraceEnd(options)
        sys.exit(-1)


def PrintModuleVersions():
    """
    Print out all the Python module versions.  Useful for debugging.

    @return no return
    """

    # Get all the Python files & sort them.
    #
    # import pdb;  pdb.set_trace()
    py_files = glob.glob('*.py')
    py_files += glob.glob('base/*.py')
    py_files.sort()

    platform = Platform()
    msg.PrintMsg('Python module versions:')
    for p_file in py_files:
        # Get the line with the RCS 'Id:' string.
        #
        if platform == config.WIN_NATIVE:
            cmd = 'findstr Id: ' + p_file
        else:
            cmd = 'grep Id: ' + p_file
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
        (stdout, stderr) = p.communicate()

        # Print the file name and version.
        #
        pos = stdout.find('Id:')
        if pos != -1:
            words = stdout.split(' ')
            file_name = words[2].replace(',v', '')
            msg.PrintMsg(file_name.ljust(26) + words[3])
    msg.PrintMsg('')


def MPICmdLine(options):
    """
    Generate the MPI command line.

    All MPI command line options are defined here:

       -env I_MPI_DEVICE shm               Use shared memory

       -env I_MPI_SPIN_COUNT 2147483647    Spin 2 billion times

       -env I_MPI_PIN_MODE lib             Use MPI library pinning method

       -env I_MPI_PIN_PROCS 3,5,2,4        List of cores to use for pinning

       -env I_MPI_DEBUG 4                  Debug level to print pinning info

    If you change these default values, then also change the values used to
    print the default in the module cmd_options for the option 'mpi_options'.

    @return string with MPI command
    """

    # Use MPI command user gives in 'mpi_options', else see if 'num_proc' is
    # defined.
    #
    # If param 'num_proc' is not a command line option (thus 'num_proc' is not
    # in options), but it's defined in a configuration file, this info has
    # already been stored in 'config.num_proc'.  Hence need to check both
    # config/options.
    #
    nproc = 0
    if hasattr(options, 'mpi_options') and options.mpi_options:
        mpi_cmd = 'mpirun ' + options.mpi_options
    elif hasattr(options, 'num_proc') and options.num_proc > 0:
        nproc = options.num_proc
    elif config.num_proc > 0:
        nproc = config.num_proc
    else:
        string = "Must use either 'num_proc' to define the number of MPI processes\n" \
                 "or 'mpi_options' to define a MPI command line (w/o string 'mpirun').\n" \
                 "These can be defined in a configuation file using the option '--cfg'"
        msg.PrintHelpAndExit(string)

    # If needed, format the default MPI command line
    #
    if nproc > 0:
        mpi_cmd = 'mpirun -n ' + str(nproc)
        mpi_cmd += ' -env I_MPI_DEVICE shm -env I_MPI_SPIN_COUNT 2147483647 -env I_MPI_PIN_MODE lib '
        mpi_cmd += ' -env I_MPI_PIN_PROCS 3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12 -env I_MPI_DEBUG 4 '

    return mpi_cmd


def CountFiles(string):
    """
    Count the number of files in the current directory which contain string 'str'.

    @return integer with count
    """

    return len(glob.glob('*' + string + '*'))

#################################################################
#
# Functions to parse region pinball/LIT file names and return the
# fields contained in the file name.
#
#################################################################

# Regular expression patterns used for matching region pinball file names.
# There are three patterns:  user_pattern, pp_pattern and full_pattern
#
# The full_pattern is composed of two parts: user_pattern & pp_pattern
#
#   user_pattern - defined by either:
#       1) The user parameters 'program_name', 'input_name' and the PID
#       2) By the names of the whole program pinballs given by the user
#
#   pp_pattern - generated by the PinPlay tools when creating region pinballs 
#
# Here're two example of a file name generated by using the default naming
# convention of the pinpoints.py script:
#
#   omnetpp.p10000-s10_57015_t0r5_warmup1001500_prolog0_region3500003_epilog0_005_0-00970.0.address
#
#   specrand.test_13923_t0r1_warmupendPC0x0004385f0_warmupendPCCount3125_warmuplength1000009_endPC0x000417cc0_endPCCount1377_length1000002_multiplier1-001_001_0-00162.0 
#
# Here are examples of the two sub-component patterns for this full name:
#
#   user_pattern
#       omnetpp.p10000-s10_57015
#
#   pp_pattern
#       _t0r5_warmup1001500_prolog0_region3500003_epilog0_005_0-00970.
#
# A partial file name only contains a match for 'pp_pattern'.  The initial part of the file name
# has no limits on the format of the string.
#
# Here's an example of a file name which is a 'partial' file name.
#
#   test-abc_one.two_t0r5_warmup1001500_prolog0_region3500003_epilog0_005_0-00970.0.address
#
user_pattern = '[a-zA-Z0-9-+%]+\.[a-zA-Z0-9-+%]+_[0-9]+'
pp_iregion_pattern = '_t([0-9])+r([0-9]+)_warmup([0-9]+)_prolog([0-9]+)_region([0-9]+)_epilog([0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
pp_global_iregion_pattern = '_globalr([0-9]+)_warmup([0-9]+)_prolog([0-9]+)_region([0-9]+)_epilog([0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
pp_pcregion_pattern = '_t([0-9])+r([0-9]+)_warmupendPC(0x[0-9A-Fa-f]+)_warmupendPCCount([0-9]+)_warmuplength([0-9]+)_endPC(0x[0-9A-Fa-f]+)_endPCCount([0-9]+)_length([0-9]+)_multiplier([0-9]+-[0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
pp_global_pcregion_pattern = '_globalr([0-9]+)_warmupendPC(0x[0-9A-Fa-f]+)_warmupendPCCount([0-9]+)_warmuplength([0-9]+)_endPC(0x[0-9A-Fa-f]+)_endPCCount([0-9]+)_length([0-9]+)_multiplier([0-9]+-[0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
full_iregion_pattern = user_pattern + pp_iregion_pattern
full_pcregion_pattern = user_pattern + pp_pcregion_pattern
full_global_iregion_pattern = user_pattern + pp_global_iregion_pattern
full_global_pcregion_pattern = user_pattern + pp_global_pcregion_pattern

# Currently, this is not used, but should probably be added to the algorithm for parsing
# file names...
#
# For backwards compatability, a pattern is given for the older file names:
#
#   old_user_pattern - generated by PinPlay using the three parameters 'program_name', 'num_proc'
#       and 'input_name' instead of just 'program_name' and 'input_name' as in 'user_pattern'.
#
# Here are examples of the two sub-component patterns for this full name:
#   user_pattern
#       omnetpp.2.p10000-s10
#
# old_user_pattern = '[a-zA-Z0-9-+%]+\.[0-9]+\.[a-zA-Z0-9-+%]+_[0-9]+'
# old_full_pattern = old_user_pattern + pp_pattern


def ParseFileName(file_name):
    """
    Determine the type of file name name, either full or partial.  Then
    call the appropriate function to parse the file name.

    See comments above for definitions of full and partial.

    @return dictionary which contains fields successfully parsed
    """

    result = {}
    file_name = os.path.basename(file_name)
    #import pdb;  pdb.set_trace()
    if config.global_regions:
      local_full_iregion_pattern = full_global_iregion_pattern
      local_full_pcregion_pattern = full_global_pcregion_pattern
      local_pp_iregion_pattern = pp_global_iregion_pattern
      local_pp_pcregion_pattern = pp_global_pcregion_pattern
    else:
      local_full_iregion_pattern = full_iregion_pattern
      local_full_pcregion_pattern = full_pcregion_pattern
      local_pp_iregion_pattern = pp_iregion_pattern
      local_pp_pcregion_pattern = pp_pcregion_pattern
    if re.search(local_full_iregion_pattern, file_name):
        result = ParseFullFileName(file_name, local_full_iregion_pattern)
    elif re.search(local_full_pcregion_pattern, file_name):
        result = ParseFullFileName(file_name, local_full_pcregion_pattern)
    elif re.search(local_pp_iregion_pattern, file_name):
        result = ParsePartialFileNameIregion(file_name)
    elif re.search(pp_pcregion_pattern, file_name):
        result = ParsePartialFileNamePCregion(file_name)
    else:
        msg.PrintAndExit('method util.ParseFileName() encountered a file name\n' 'which does not meet requirements for parsing a region pinball name:\n   ' + \
                file_name + '\nFile name must conform to the region pinball naming convention.')

    return result


def ParseFullFileName(file_name, myfull_pattern):
    """
    Parse a region pinball file name where all fields were generated by the
    pinpoints.py script.

        omnetpp.p10000-s10_57015_t0r5_warmup1001500_prolog0_region3500003_epilog0_005_0-00970.0.address

    The following information is contained in the first part of it:
        program name:    omnetpp
        input name:      p10000-s10
        PID:             57015

    The remainder of the file name is parsed by the function
    ParsePartialFileName() which is called by this function.

    @return a dictionary which contains the fields successfully parsed from
    the full file name
    """

    # Remove any directories from the name.
    #
    # import pdb;  pdb.set_trace()
    file_name = os.path.basename(file_name)

    # Make sure the file name matches the full file name pattern.
    #
    # import pdb;  pdb.set_trace()
    if not re.search(myfull_pattern, file_name):
        msg.PrintAndExit('method util.ParseFullFileName() encountered a file name\n' 'which does not meet requirements for parsing a region pinball name:\n   ' + \
                file_name + '\nFile name must conform to the region pinball naming convention.')

    # Directory containing the fields which were successfull parsed.
    #
    result = {}

    # Define a lambda function to print a standard error msg.
    #
    err_msg = lambda fname, string: msg.PrintAndExit(
        'method util.ParseFullFileName() encountered '
        'an error parsing \'' + string + '\' in \nfile name:  ' + fname)

    # Get the first 'user' part of the file name and parse it.  The example
    # given above produces this list:
    #
    #  ['omnetpp.p10000-s10_57015']
    #
    # import pdb;  pdb.set_trace()
    user_part = re.findall(user_pattern, file_name)
    field = user_part[0].split('.')
    tmp = field[1].split('_')
    result['program_name'] = field[0]  # No error check for this field
    result['input_name'] = tmp[0]  # No error check for this field

    # Old code to get the num_proc.  Curently not used. MUST be updated to use it
    # again here...  Also, code for PID must be modified if/when this code is used.
    #
    # Get the number of processes from the 1st 'user_part' string.  It
    # contains a string like '.X.', where X is num_proc.
    #
    # import pdb;  pdb.set_trace()
    # tmp = re.findall('[0-9]*\.[0-9]+\.', field[1])
    # if len(tmp) > 0:
    #     num_proc = tmp[0].replace('.', '')
    #     try:
    #         num_proc = int(num_proc)
    #     except ValueError:
    #         err_msg(file_name, 'num_proc')
    #     result['num_proc'] = num_proc

    # Get the PID
    #
    try:
        pid = int(tmp[1])
    except ValueError:
        err_msg(file_name, 'pid')
    result['pid'] = pid

    # Get the fields from the PinPlay part of the file name.
    #
    # import pdb;  pdb.set_trace()
    if config.global_regions:
      local_pp_iregion_pattern = pp_global_iregion_pattern
      local_pp_pcregion_pattern = pp_global_pcregion_pattern
    else:
      local_pp_iregion_pattern = pp_iregion_pattern
      local_pp_pcregion_pattern = pp_pcregion_pattern
    if re.search(local_pp_iregion_pattern, file_name):
        partial_result = ParsePartialFileNameIregion(file_name)
    elif re.search(local_pp_pcregion_pattern, file_name):
        partial_result = ParsePartialFileNamePCregion(file_name)

    # Return the fields from both parts of the file name.
    #
    return dict(result.items() + partial_result.items())


def ParsePartialFileNameIregion(file_name):
    """
    Parse the portion of a region pinball file name generated by the PinPlay logger pin tool.

    The PinPlay tools encode information about a representative region in the file name.
    For example, given this full file name:

        omnetpp.p10000-s10_57015_t0r5_warmup1001500_prolog0_region3500003_epilog0_005_0-00970.0.address

    The subset of the file name generated by PinPlay is:

        _t0r5_warmup1001500_prolog0_region3500003_epilog0_005_0-00970.0.address

    The first part this string contains:
        TID:                 0  (t0)
        region number:       5  (r5)

    The next part contains the number of instructions in each of
    the four 'section' of the region pinball.  The format is a section name,
    followed by the number of instructions in the section.

    The number of instructions in these sections are:
        warmup 1001500  (warmup1001500),
        prolog       0  (prolog0),
        region 3500003  (region3500003),
        epilog       0  (epilog0)

    Any of the sections, except for region itself, may contain 0 instructions.
    In this case, the section will still exist, but with 0 instructions.

    Finally, there are 2 more pieces of information:
        trace number:     5   (005)
        weight:     0.00970   (0-00970)

    The trace number is the same as the region number.  This information is duplicted in
    the file name.

    This code makes the assumption that the tracing parameters 'program_name' and
    'input_name' do NOT contain any of these chars: '.', '_'.

    @return dictionary which contains fields successfully parsed
    """

    # Remove any directories from the name.
    #
    # import pdb;  pdb.set_trace()
    file_name = os.path.basename(file_name)

    # Make sure the file name matches the PinPlay pattern.
    #
    # import pdb;  pdb.set_trace()
    if config.global_regions:
      local_pp_iregion_pattern = pp_global_iregion_pattern
    else:
      local_pp_iregion_pattern = pp_iregion_pattern
    if not re.search(local_pp_iregion_pattern, file_name):
        msg.PrintAndExit('method util.ParsePartialFileNameIregion() encountered a file name\n' 'which does not meet requirements for parsing a region pinball name:\n   ' + \
                file_name + '\nFile name must conform to the region pinball naming convention.')

    # Directory containing the fields which were successfull parsed.
    #
    result = {}

    # Define a lambda function to print a standard error msg.
    #
    err_msg = lambda fname, string: msg.PrintAndExit(
        'method util.ParsePartialFileNameIregion() encountered '
        'an error parsing \'' + string + '\' in \nfile name:  ' + fname)

    # Get the file extension
    #
    # import pdb;  pdb.set_trace()
    # pp_iregion_pattern = '_t([0-9])+r([0-9]+)_warmup([0-9]+)_prolog([0-9]+)_region([0-9]+)_epilog([0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
    #   group(1) tid, group(2) rid, group(3) warmup_length
    #   group(4) prolog_length, group(5) sim_length, group(6) epilog_length, 
    #   group(7) traceno, group(8) weight
    # pp_global_iregion_pattern = '_globalr([0-9]+)_warmup([0-9]+)_prolog([0-9]+)_region([0-9]+)_epilog([0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
    #   group(1) rid, group(2) warmup_length
    #   group(3) prolog_length, group(4) sim_length, group(5) epilog_length, 
    #   group(6) traceno, group(7) weight
    tmp = re.split(local_pp_iregion_pattern, file_name)
    if len(tmp) >= 2:
        file_ext = tmp[1]
        tmp = file_ext.split('.')
        if tmp[0].isdigit():
            file_ext = '.'.join(tmp[1:])  # Remove TID if it exists
        else:
            file_ext = '.'.join(tmp[0:])
        result['file_ext'] = file_ext

    # Get the PinPlay generated part of the file name.
    #
    # import pdb;  pdb.set_trace()
    pp_part = re.search(local_pp_iregion_pattern, file_name)
    
    if pp_part:
        if config.global_regions:
          gr_offset = -1
          tid =   -1
        else:
          gr_offset = 0
          tid =   int(pp_part.group(1))
        result['tid'] = tid

        region_num =   int(pp_part.group(gr_offset+2))
        result['region_num'] = region_num

        warmup = int(pp_part.group(gr_offset+3))
        result['warmup'] = warmup
        prolog =  int(pp_part.group(gr_offset+4))
        result['prolog'] = prolog
        region =  int(pp_part.group(gr_offset+5))
        result['region'] = region
        epilog =  int(pp_part.group(gr_offset+6))
        result['epilog'] = epilog
        trace_num =  int(pp_part.group(gr_offset+7))
        result['trace_num'] = trace_num

        # Final field contains the weight with '-' as the decimal point.
        # NOTE: string '1-00000' == 1.0
        #
        # import pdb ; pdb.set_trace()
        tmp =  pp_part.group(gr_offset+8) # string
        if tmp.find('1-0') != -1:
            weight = 1.0
        else:
            pos = tmp.find('.')
            tmp = tmp[:pos]  # Remove trailing '.' from the field
            try:
                weight = float(tmp.replace('-', '.'))
            except ValueError:
                err_msg(file_name, 'weight')
        result['weight'] = weight

    return result

def ParsePartialFileNamePCregion(file_name):
    """
    Parse the portion of a region pinball file name generated by the PinPlay logger pin tool.

    The PinPlay tools encode information about a representative region in the file name.
    For example, given this full file name:

       specrand.test_13923_t0r1_warmupendPC0x0004385f0_warmupendPCCount3125_warmuplength1000009_endPC0x000417cc0_endPCCount1377_length1000002_multiplier1-001_001_0-00162.0.address 

    The subset of the file name generated by PinPlay is:

       _t0r1_warmupendPC0x0004385f0_warmupendPCCount3125_warmuplength1000009_endPC0x000417cc0_endPCCount1377_length1000002_multiplier1-001_001_0-00162.0.address 

    The first part this string contains:
        TID:                 0  (t0)
        region number:       5  (r5)

    The next part contains the pc+count and the length for each of
    the two 'section' of the region pinball.  

    The items in these sections are:
        warmup  warmupendPC, warmupendPCCount, warmuplength
        region  endPC, endPCCount, length

    Any of the sections, except for region itself, may contain 0 pc,count, or
    length.
    In this case, the section will still exist, but with 0 pc,count, and 
    instructions.

    Finally, there are 3 more pieces of information:
        multiplier:     1.001   (1-001)
        trace number:     5   (005)
        weight:     0.00162   (0-00162)

    The trace number is the same as the region number.  This information is duplicted in
    the file name.

    This code makes the assumption that the tracing parameters 'program_name' and
    'input_name' do NOT contain any of these chars: '.', '_'.

    @return dictionary which contains fields successfully parsed
    """

    # Remove any directories from the name.
    #
    # import pdb;  pdb.set_trace()
    if config.global_regions:
      local_pp_pcregion_pattern = pp_global_pcregion_pattern
    else:
      local_pp_pcregion_pattern = pp_pcregion_pattern
    file_name = os.path.basename(file_name)

    # Make sure the file name matches the PinPlay pattern.
    #
    # import pdb;  pdb.set_trace()
    if not re.search(local_pp_pcregion_pattern, file_name):
        msg.PrintAndExit('method util.ParsePartialFileNamePCregion() encountered a file name\n' 'which does not meet requirements for parsing a region pinball name:\n   ' + \
                file_name + '\nFile name must conform to the region pinball naming convention.')

    # Directory containing the fields which were successfull parsed.
    #
    result = {}

    # Define a lambda function to print a standard error msg.
    #
    err_msg = lambda fname, string: msg.PrintAndExit(
        'method util.ParsePartialFileNamePCregion() encountered '
        'an error parsing \'' + string + '\' in \nfile name:  ' + fname)

    # Get the file extension
    #
    # import pdb;  pdb.set_trace()
    tmp = re.split(pp_pcregion_pattern, file_name)
    if len(tmp) >= 2:
        file_ext = tmp[1]
        tmp = file_ext.split('.')
        if tmp[0].isdigit():
            file_ext = '.'.join(tmp[1:])  # Remove TID if it exists
        else:
            file_ext = '.'.join(tmp[0:])
        result['file_ext'] = file_ext

    # Get the PinPlay generated part of the file name.
    #
    # import pdb;  pdb.set_trace()
    pp_part = re.search(local_pp_pcregion_pattern, file_name)

    # Divide the pp_part into strings separated by '_' which contain the
    # fields of the file name.  The example given above produces this
    # list:
    #
    #pp_pcregion_pattern = '_t([0-9])+r([0-9])+_warmupendPC(0x[0-9A-Fa-f]+)_warmupendPCCount([0-9]+)_warmuplength([0-9]+)_endPC(0x[0-9A-Fa-f]+)_endPCCount([0-9]+)_length([0-9]+)_multiplier([0-9]+-[0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
    #    group(1) tid, group(2) rid, group(3) warmupendPC, 
    #    group(4) warmupendPCCount, group(5) warmuplength, group(6) endPC, 
    #    group(7) endPCCount, group(8) length, group(9) multiplier,
    #    group(10) traceno, group(11) weight
    #pp_global_pcregion_pattern = '_globalr([0-9]+)_warmupendPC(0x[0-9A-Fa-f]+)_warmupendPCCount([0-9]+)_warmuplength([0-9]+)_endPC(0x[0-9A-Fa-f]+)_endPCCount([0-9]+)_length([0-9]+)_multiplier([0-9]+-[0-9]+)_([0-9]+)_([0-1]-[0-9]+)'
    #    group(1) rid, group(2) warmupendPC, 
    #    group(3) warmupendPCCount, group(4) warmuplength, group(5) endPC, 
    #    group(6) endPCCount, group(7) length, group(8) multiplier,
    #    group(9) traceno, group(10) weight
    if pp_part:
        if config.global_regions:
          gr_offset = -1
          tid =   -1
        else:
          gr_offset = 0
          tid =   int(pp_part.group(1))
        result['tid'] = tid
        region_num = int(pp_part.group(gr_offset+2))
        result['region_num'] = region_num
        warmupendPC =  pp_part.group(gr_offset+3) # string
        result['warmupendPC'] = warmupendPC
        warmupendPCCount =  int(pp_part.group(gr_offset+4))
        result['warmupendPCCount'] = warmupendPCCount
        warmuplength =  int(pp_part.group(gr_offset+5))
        result['warmuplength'] = warmuplength
        endPC =   pp_part.group(gr_offset+6) # string
        result['endPC'] = endPC
        endPCCount = int(pp_part.group(gr_offset+7))
        result['endPCCount'] = endPCCount
        length =   int(pp_part.group(gr_offset+8))
        result['length'] = length
        tmp =   pp_part.group(gr_offset+9) # string
        multiplier = float(tmp.replace('-', '.'))
        result['multiplier'] = multiplier
        trace_num =   int(pp_part.group(gr_offset+10))
        result['trace_num'] = trace_num

        # Final field contains the weight with '-' as the decimal point.
        # NOTE: string '1-00000' == 1.0
        #
        # import pdb ; pdb.set_trace()
        tmp =   pp_part.group(gr_offset+11) # string
        if tmp.find('1-0') != -1:
            weight = 1.0
        else:
            pos = tmp.find('.')
            tmp = tmp[:pos]  # Remove trailing '.' from the field
            try:
                weight = float(tmp.replace('-', '.'))
            except ValueError:
                err_msg(file_name, 'weight')
        result['weight'] = weight

    return result

def GetRegionInfo(file_name, options, use_file_fields=False):
    """
    Get information about a region pinball, including:
        1) Total icount
        2) Icount for each of the 4 possible regions
        3) TID
        4) Region number

    Boolean 'use_file_fields' allows two methods to be used to determine the region icount:
       Use the actual icount from the pinball for the number of instruction in region.  More accurate.
       Use the number given in the file name. The info in file name is not the exact count.

    @return list containing: total_icount, warmup_icount, prolog_icount, region_icount, epilog_icount, TID, region_num
    """

    # Define a lambda function to print an error message. This time include file name.
    #
    err_msg = lambda string, fname: msg.PrintMsg(
        'ERROR: method sde_phases.GenLitFiles() failed to '
        'get field: ' + string + '\nfrom file: ' + file_name)

    # Get the fields from parsing the pinball file name.  'fields' is a dictionary
    # with the fields in the file name.
    #
    #import pdb;  pdb.set_trace()
    fields = ParseFileName(file_name)
    prolog=0
    epilog=0
    warmup=0
    region=0
    try:
        region_num = fields['region_num']
    except KeyError:
        err_msg('region_num', file_name)
        return -1
    try:
        tid = fields['tid']
    except KeyError:
        err_msg('tid', file_name)
        return -1
    if hasattr(options, 'pccount_regions') and options.pccount_regions:
        try:
            warmup = fields['warmuplength']
        except KeyError:
            err_msg('warmuplength', file_name)
            return -1
        try:
            region = fields['length']
        except KeyError:
            err_msg('length', file_name)
            return -1
    else:
        try:
            epilog = fields['epilog']
        except KeyError:
            err_msg('epilog', file_name)
            return -1
        try:
            prolog = fields['prolog']
        except KeyError:
            err_msg('prolog', file_name)
            return -1
        try:
            region = fields['region']
        except KeyError:
            err_msg('region', file_name)
            return -1
        try:
            warmup = fields['warmup']
        except KeyError:
            err_msg('warmup', file_name)
            return -1
    # Calculate some metrics for the pinball.  Use the number of instructions from the
    # result file (icount) instead of using the count from the file name.  The icount
    # is the actual number of instructions in the region.  The number from the file name
    # is only an approximate number of instructions.
    #
    #import pdb;  pdb.set_trace()
    if config.global_regions:
      icount=0
      basename = RemoveTID(file_name)
      for fname in glob.glob(basename + '*.result'):
        ticount = int(FindResultString(fname, 'inscount')[0])
        icount += ticount
    else:
      field = FindResultString(glob.glob(file_name + '*.result')[0], 'inscount')
      icount = field[0]
      if icount:
          icount = int(icount)
      else:
          icount = 0
    # Two methods of determining the region icount:
    #   1) Calculate region length using icount from the *.result file  (default)
    #      This reflects the actual icount of the region pinball.
    #   2) Use the region icount found in the file name field.  This value could be
    #      much different than the actual value used to generate the pinball region.
    #
    calc_region = icount - warmup - prolog - epilog
    #import pdb;  pdb.set_trace()
    if use_file_fields:
        calc_region = region

    return (icount, warmup, prolog, calc_region, epilog, tid, region_num)


def GetIterDir(iteration):
    """
    Get the name of the CSV directory associated with 'iteration'

    @return string with directory name
    """

    return '%s-%02d' % (config.csv_dir_base, iteration)


def GetCSVFiles(file_name, param=None):
    """
    Get the region CSV files associated with file_name.

    If 'param' contains the key 'iteration', then use this to get a
    subdirectory of the *.Data directory where the CSV file is located.

    @return list containing:  region, input and output file name
    """

    basename = RemoveTID(file_name)
    data_dir = basename + '.Data'

    # Check to see if the base *.Data directory needs to be modified
    #
    # import pdb;  pdb.set_trace()
    if param:
        if param.has_key('iteration'):
            data_dir = os.path.join(data_dir, GetIterDir(param['iteration']))
        if param.has_key('in_lit_dir') and param['in_lit_dir']:
            data_dir = os.path.join('..', data_dir)

    if config.global_regions:
      region_file = os.path.join(data_dir, basename) + '.global.pinpoints.csv'
    else:
      region_file = os.path.join(data_dir, basename) + '.pinpoints.csv'
    csv_file = os.path.basename(region_file)
    base_name = csv_file.replace('.csv', '')

    in_file = base_name + '.in.csv'
    out_file = base_name + '.out.csv'

    # import pdb;  pdb.set_trace()
    return [region_file, in_file, out_file]


def GetClusterInfo(file_name, param):
    """
    Get the cluster information from a CSV file.

    The info is stored in a dictionary using the cluster number as the key
    and a string containing the cluster info from the CSV file.  A line of
    cluster info contains 6 fields, using a comma as the field separator.

    Need to skip comments denoted by '#'. Also need to skip the 2nd line in
    the region CSV file generated by the Perl script.  This line is a header
    describing the various files.  As such, it matches the format of a cluster,
    but isn't a cluster specification.

    Here's the format of the first 4 lines of the Perl generated file:

        # Regions based on 'create_region_file.pl -seq_region_ids -tid 0 -region_file t.simpoints -weight_file t.weights t.bb':
        comment,thread-id,region-id,simulation-region-start-icount,simulation-region-end-icount,region-weight
        # Region = 1 Slice = 88 Icount = 308000214 Length = 3500002 Weight = 0.5432
        cluster 0 from slice 88,0,1,308000214,311500216,0.543210

   For a pc+count regions file, a region record looks like this
   cluster 0 from slice 5459,0,1,0x4029a5,mcf_base.linux.xSSE4-2-20111104,0x29a5,478047360,0x401e47,mcf_base.linux.xSSE4-2-20111104,0x1e47,2406261603,1516288,30000005,0.04461,463.000,simulation
   # comment,thread-id,region-id,start-pc, start-image-name, start-image-offset, start-pc-count,end-pc, end-image-name, end-image-offset, end-pc-count,end-pc-relative-count, region-length, region-weight, region-multiplier, region-type


    @return dictionary containing cluster information
    """

    # Assume the first field, which contains the cluster information, has the format:
    #   cluster 0 from slice 88
    #
    cluster_pattern = '[Cc]luster\s(\d+)\sfrom slice\s(\d+)'
    warmup_pattern = 'Warmup\sfor\sregionid\s(\d+)'
    total_instr_pattern = '.*Total instructions in.*= (\d+)'

    # If file_name is not a CSV file, then need to get the CSV
    # file corresponding to file_name.
    #
    #import pdb;  pdb.set_trace()
    if '.in.csv' not in file_name and '.out.csv' not in file_name:
        [region_file, in_file, out_file] = GetCSVFiles(file_name, param)
        file_name = region_file
        # self.region_CSV_file = region_file    # Save name of region CSV file in class attribute

    cluster_info = {}
    warmup_info = {}
    total_instr = 0
    # import pdb;  pdb.set_trace()
    if os.path.isfile(file_name):
        try:
            f = open(file_name)
        except IOError:
            msg.PrintAndExit(
                'method phases.GetClusterInfo() can\'t open file: ' + file_name)

        numfields = 0
        for string in f.readlines():

            # Look for the cluster information
            #
            line = string
            if '#' in string:
                line = string.partition('#')[2]  # Remove any comments
            # if '#' is found 
            #   patition() returns the part before '#', '#', part after '#'
            # else
            #   patition() returns string followed by two empty strings
            fields = re.split(',', line)  # Find line with CSV fields
            if 'comment' in  fields[0]:
                numfields=len(fields)
                continue
            if numfields == 0: 
                continue
            if len(fields) == numfields:
                # Get cluster number from the first field
                #
                c = re.search(cluster_pattern, fields[0])
                if c:
                    num = int(c.group(1))
                    cluster_info[num] = line
                    warmup_info[num] = ''
                    # may be modified later if warmup record exists
                    # assumes simulation records are emitted before warmup
                c = re.search(warmup_pattern, fields[0])
                if c:
                    regionid = int(c.group(1))
                    warmup_info[regionid-1] = line

            # Look for total number of instructions
            #
            c = re.search(total_instr_pattern, string)
            if c:
                total_instr = int(c.group(1))

        f.close()

    return cluster_info, warmup_info, total_instr


def ParseClusterInfo(cluster_info):
    """
    Parse the individual fields in a regions CSV files.  Input is the cluster info
    returned by GetClusterInfo().

    @return list of dictionaries, one for each cluster
    """

    # Define a pattern to parse the cluster information in one line of a CSV file:
    #
    #    Here's the format for one line of the cluster info from a file:
    #
    #      cluster 0 from slice 88,0,1,308000214,311500216,0.543210
    #      or with pc+count regions:
    #      comment,thread-id,region-id,start-pc, start-image-name, start-image-offset, 
    #           start-pc-count,end-pc, end-image-name, end-image-offset,  end-pc-count,
    #           end-pc-relative-count, region-length, region-weight,
    #           region-multiplier, region-type
    #      cluster 0 from slice 4,0,1,0x7ffff7de6351,ld-linux-x86-64.so.2,0x11974,13,430,
    #            0x7fffdea57990, ld-linux-x86-64.so.2,0x11980,14,
    #            1,2,17962,0.20000,
    #            0.615,simulation
    #
    # cluster_iregion_pattern:
    # 1) Comment with cluster number
    #   [Cc]luster\s(\d+)\sfrom slice\s[0-9]+,
    #
    # 2) TID
    #   (\d+),
    #
    # 3) Region number
    #   (\d+),
    #
    # 4) icount of first instruction in region
    #   (\d+),
    #
    # 5) icount of last instruction in region
    #   (\d+),
    #
    # 6) Cluster weight - This matches floating point numbers in either fixed point or scientific notation:
    #   -?        optionally matches a negative sign (zero or one negative signs)
    #   \ *       matches any number of spaces (to allow for formatting variations like - 2.3 or -2.3)
    #   [0-9]+    matches one or more digits
    #   \.?       optionally matches a period (zero or one periods)
    #   [0-9]*    matches any number of digits, including zero
    #   (?: ... ) groups an expression, but without forming a "capturing group" (look it up)
    #   [Ee]      matches either "e" or "E"
    #   \ *       matches any number of spaces (to allow for formats like 2.3E5 or 2.3E 5)
    #   -?        optionally matches a negative sign
    #   \ *       matches any number of spaces
    #   [0-9]+    matches one or more digits
    #   ?         makes the entire non-capturing group optional (to allow for
    #             the presence or absence of the exponent - 3000 or 3E3

    # cluster_pcregion_pattern:
    # 1) Comment with cluster number
    #   [Cc]luster\s(\d+)\sfrom slice\s[0-9]+,
    #
    # 2) TID
    #   (\d+),
    #
    # 3) Region number
    #   (\d+),
    #
    # 4) pc of the first instruction in region
    #   (0x[0-9a-f]+),
    #
    # 5) start-image-name
    #   string,
    #
    # 6) end-image-offset
    #   (0x[0-9a-f]+),
    #
    # 7) start-pc-count
    #   (\d+),
    #
    # 8) pc of the last instruction in region
    #   (0x[0-9a-f]+),
    #
    # 9) end-image-name
    #   string,
    #
    # 10) end-image-offset
    #   (0x[0-9a-f]+),
    #
    # 11) end-pc-count
    #   (\d+),
    #
    # 12) end-pc-count-relative
    #   (\d+),
    #
    # 13) region-length
    #   (\d+),
    #
    # 14) Cluster weight - This matches floating point numbers in either fixed point or scientific notation:
    #   -?        optionally matches a negative sign (zero or one negative signs)
    #   \ *       matches any number of spaces (to allow for formatting variations like - 2.3 or -2.3)
    #   [0-9]+    matches one or more digits
    #   \.?       optionally matches a period (zero or one periods)
    #   [0-9]*    matches any number of digits, including zero
    #   (?: ... ) groups an expression, but without forming a "capturing group" (look it up)
    #   [Ee]      matches either "e" or "E"
    #   \ *       matches any number of spaces (to allow for formats like 2.3E5 or 2.3E 5)
    #   -?        optionally matches a negative sign
    #   \ *       matches any number of spaces
    #   [0-9]+    matches one or more digits
    #   ?         makes the entire non-capturing group optional (to allow for
    #             the presence or absence of the exponent - 3000 or 3E3
    #
    # 15) Multiplier - Same as "Cluster weight"
    #
    # 16) Region type "warmup" of "simulation"
    #
    cluster_iregion_pattern = '[Cc]luster\s(\d+)\sfrom slice\s[0-9]+,(\d+),(\d+),(\d+),(\d+),' \
                      '(-?\ *[0-9]+\.?[0-9]*(?:[Ee]\ *-?\ *[0-9]+)?)'
    cluster_pcregion_pattern = '[Cc]luster\s(\d+)\sfrom slice\s[0-9]+,(\d+),(\d+),' \
                      '(0x[0-9a-f]+),([\S]+),(0x[0-9a-f]+),(\d+),' \
                      '(0x[0-9a-f]+),([\S]+),(0x[0-9a-f]+),(\d+),(\d+),(\d+),' \
                      '(-?\ *[0-9]+\.?[0-9]*(?:[Ee]\ *-?\ *[0-9]+)?),' \
                      '(-?\ *[0-9]+\.?[0-9]*(?:[Ee]\ *-?\ *[0-9]+)?),' \
                      '(warmup|simulation)'

    # For each cluster, parse the information and record it.
    #
    cluster_list = []
    for cluster in cluster_info.values():
        c = re.search(cluster_iregion_pattern, cluster)
        if c:
            cl_dir = {}
            cl_dir['cluster_num'] = int(c.group(1))
            cl_dir['tid'] = int(c.group(2))
            cl_dir['region'] = int(c.group(3))
            cl_dir['first_icount'] = int(c.group(4))
            cl_dir['last_icount'] = int(c.group(5))
            cl_dir['weight'] = float(c.group(6))
            cluster_list.append(cl_dir)
        else:
            c = re.search(cluster_pcregion_pattern, cluster)
            if c:
                cl_dir = {}
                cl_dir['cluster_num'] = int(c.group(1))
                cl_dir['tid'] = int(c.group(2))
                cl_dir['region'] = int(c.group(3))
                cl_dir['startPC'] = c.group(4)
                cl_dir['startImageName'] = c.group(5)
                cl_dir['startImageOffset'] = c.group(6)
                cl_dir['startPCCount'] = int(c.group(7))
                cl_dir['endPC'] = c.group(8)
                cl_dir['endImageName'] = c.group(9)
                cl_dir['endImageOffset'] = c.group(10)
                cl_dir['endPCCount'] = int(c.group(11))
                cl_dir['endPCCountRelative'] = int(c.group(12))
                cl_dir['length'] = int(c.group(13))
                cl_dir['weight'] = float(c.group(14))
                cl_dir['multiplier'] = float(c.group(15))
                cl_dir['regionType'] = c.group(16)
                cluster_list.append(cl_dir)

    return cluster_list


def CountClusters(file_name, param):
    """
    Count the clusters in a region CSV file.

    @return number of valid clusters
    """

    cluster_info, not_used1, not_used2 = GetClusterInfo(file_name, param)
    return len(cluster_info)


def IsCoopPinball(file_name):
    """
    Is this a cooperative pinball (i.e. has > 1 thread)?

    Count the number of result files for the pinball.  If there are more than
    one, then it's a cooperative pinball.

    This will probably have to be changed when PinPlay implements the
    capability to generate one 'per thread' pinball for each thread in
    cooperative whole program pinballs.  It is currently unknown how this
    enhancement will be implemented.

    @return boolean
    """

    return len(glob.glob(file_name + '*.result')) > 1


def IsInt(s):
    """
    Is a string an integer number?

    @return boolean
    """
    try:
        int(s)
        return True
    except ValueError:
        return False


def IsFloat(s):
    """
    Is a string an floating point number?

    @return boolean
    """
    try:
        float(s)
        return True
    except ValueError:
        return False


def OpenCompressFile(sim_file):
    """
    Open a simulator file and make sure it contains at least some data.

    The method will open either a file compressed with gzip, bzip2 or a non-compressed
    file.

    @return file pointer to open file
    @return None if open fails
    """

    # Dictionary of 'magic' strings which define the first several char of
    # different type of compressed files.
    #
    magic_dict = {
        "\x1f\x8b\x08": "gz",
        "\x42\x5a\x68": "bz2",
        "\x50\x4b\x03\x04": "zip"
    }
    max_len = max(len(x) for x in magic_dict)

    def file_type(filename):
        """Return the type of file compression used for a file."""

        with open(filename) as f:
            file_start = f.read(max_len)
        for magic, filetype in magic_dict.items():
            if file_start.startswith(magic):
                return filetype
        return "no match"

    # Make sure the Simultor data file exists and has at least some data.
    #
    if not os.path.isfile(sim_file):
        msg.PrintMsg('Can\'t find data file: ' + sim_file)
        return None
    if os.path.getsize(sim_file) < 4:
        msg.PrintMsg('No real data in data file: ' + sim_file)
        return None

    # See if the file is compressed with gzip or bzip2.  Otherwise, assume the file
    # is not compressed.  Does not handle files compressed with 'zip'.
    #
    # import pdb ; pdb.set_trace()
    err_msg = lambda: msg.PrintMsg('Unable to open data file: ' + sim_file)
    ftype = file_type(sim_file)
    if ftype == 'gz':
        import gzip
        try:
            f = gzip.open(sim_file, 'rb')
        except IOError:
            err_msg
            return None
    elif ftype == 'bz2':
        import bz2
        try:
            f = bz2.BZ2File(sim_file, 'rb')
        except:
            err_msg
            return None
    else:
        try:
            f = open(sim_file, 'rb')
        except IOError:
            err_msg
            return None

    return f

################################################################################
#
# Functions to generate directory and file names and modify them.
#
################################################################################


def GetKitType():
    """
    Get the type of kit and the path to it based on the location where
    the script was run. This assumes the script was called with an explicit
    path to it, or the script was in the users path.
        SDE kit will contain 'pinplay-scripts' in path
        Pin kit will contain 'extras' in path

    @return the type and the base directory for the respective kit
    """

    script_path = GetScriptDir()
    kit_type = None

    if config.pin_script_path in script_path:
        kit_type = config.PINPLAY
        base_dir = script_path.replace(config.pin_script_path, '')
    elif config.sde_script_path in script_path:
        kit_type = config.SDE
        base_dir = script_path.replace(config.sde_script_path, '')
    else:
        msg.PrintAndExit('Unable to determine type of kit used '\
            'to run the script. \nPlease run it from the script directory in '\
            'a valid kit.')

    return kit_type, base_dir


def GetDefaultWPDir(options=None):
    """
    Get the default tracing instance whole program pinball directory.

    This is the directory name based solely on tracing parameters.
    """

    dir_name = config.wp_dir_basename
    if options:
        if hasattr(options, 'add_program_wp') and options.add_program_wp:
            dir_name += '.' + options.program_name
        dir_name += '.' + options.input_name
    else:
        if config.add_program_wp:
            dir_name += '.' + config.program_name
        dir_name += '.' + config.input_name

    return dir_name


def GetBaseWPDir(options=None):
    """
    Get the base whole program pinball directory.

    The base name is either the default defined above, or the name the
    user has specified on the command line.

    @return string with WP pinball directory
    """

    if config.whole_pgm_dir:
        wp_dir = config.whole_pgm_dir
    else:
        wp_dir = GetDefaultWPDir(options)

    return wp_dir


def GetWPDir(options=None):
    """
    Get the current directory name for the whole program pinballs.

    There are 3 possible locations to get this value (in priority order):
        1) config.relog_dir     - any filtered WP pinballs
        2) config.whole_pgm_dir - user has defined WP pinball dir
        3) default tracing instance dir

    @return string with WP pinball directory
    """

    if config.relog_dir:
        wp_dir = config.relog_dir
    else:
        wp_dir = GetBaseWPDir(options)

    return wp_dir


def GetWPPinballs(options=None):
    """
    Get a list of all the pinballs in the whole program pinball directory.

    @return list of all WP pinballs
    """

    wp_dir = GetWPDir(options)
    wp_pb = glob.glob(os.path.join(wp_dir, '*.address'))
    if wp_pb:
        wp_pb = [w.replace('.address', '') for w in wp_pb]

    return wp_pb


def GetWPPinballsNoTID(options=None):
    """
    Get a list of all the pinballs in the whole program pinball directory
    with the TIDs removed.

    When filtering with a focus thread the relogged WP pinballs have a TID of
    '0' added to the file name.  However, this TID is not used when generating
    the directory names for Data/pp/lit files.

    @return list of all WP pinballs without TID
    """

    wp_pb = GetWPPinballs(options)
    wp_pb = [RemoveTID(w) for w in wp_pb]

    return wp_pb


def AddRelogStr(string):
    """
    If the string doesn't already contain 'config.relog_dir_str', then add it to the
    end of the string.
    """

    if string.find(config.relog_dir_str) == -1:
        return string + config.relog_dir_str
    else:
        return string


def GetRelogPhaseDir(old_dir, phase, options):
    """
    Generate a directory name for the relogged whole program pinballs based
    on the the current phase.

    If the current value for the relog directory in the config object does
    not already have the string 'config.relog_dir_str', then it needs to be added to the
    directory name. Then the string for the appropriate phase is added.
    """

    # Default to the old name.
    #
    new_dir = old_dir

    if phase == config.RELOG_NONE:
        # Set to the original whole program directory name because not in a
        # relogging phase.
        #
        new_dir = GetWPDir(options)

    if phase == config.RELOG_NAME:
        if options.relog_name:
            string = options.relog_name
        if options.use_relog_name:
            string = options.use_relog_name
        new_dir = AddRelogStr(old_dir) + '.' + string

    if phase == config.RELOG_FOCUS:
        # Make sure there really is a focus thread.
        #
        if config.focus_thread == -1:
            # Since we know exactly which phase we are in, use the method
            # CheckResult() to exit the app. This records the failure in
            # the *.status file.
            #
            msg.PrintMsg(
                'ERROR: Trying to relog using a focus thread,\n'
                'but a focus thread has not been defined.  Use option \'--focus_thread\' to define a focus thread.')
            CheckResult(
                -1, options,
                'Filtering WP pinballs with focus thread %s' % config.PhaseStr(4)
            )  # Force the failure
        new_dir = AddRelogStr(old_dir) + '.per_thread_' + str(config.focus_thread)

    if phase == config.RELOG_NO_INIT:
        new_dir = AddRelogStr(old_dir) + '.no_init'

    if phase == config.RELOG_NO_CLEANUP:
        new_dir = AddRelogStr(old_dir) + '.no_cleanup'

    # Add the basename of the code exclusion file to the WP dirctory
    # name.
    #
    ce_dir = 'code_ex-'
    if phase == config.RELOG_CODE_EXCLUDE:
        if options.relog_code_exclude:
            s = os.path.splitext(os.path.basename(options.relog_code_exclude))
        if options.use_relog_code_exclude:
            s = os.path.splitext(
                os.path.basename(options.use_relog_code_exclude))
        new_dir = AddRelogStr(old_dir) + '.' + ce_dir + s[0]

    if phase == config.RELOG_NO_OMP_SPIN:
        new_dir = AddRelogStr(old_dir) + '.no_omp_spin'

    if phase == config.RELOG_NO_MPI_SPIN:
        new_dir = AddRelogStr(old_dir) + '.no_mpi_spin'

    return new_dir


def GetLogFile(options=None):
    """
    Generate the base file name used for many things, including pinballs, BB vector
    files and SimPoints files.
    """

    return Config.GetInstanceName(options)


def GetCBSPLogFile():
    """
    Generate the CBSP base file name used for many things, including accessing *jason* files.
    """

    return Config.GetCBSPInstanceName()


def GetDataDir(options=None):
    """
    Get all the Data directories for this tracing instance.

    @return list of Data dirs
    """

    data_list = GetWPPinballsNoTID(options)
    data_list = [os.path.basename(d) + '.Data' for d in data_list]

    return data_list


def GetLitDir(options=None):
    """
    Get all the *.lit directories for this tracing instance.

    @return list of lit dirs
    """

    lit_list = GetWPPinballsNoTID(options)
    lit_list = [os.path.basename(l) + '.lit' for l in lit_list]

    return lit_list


def GetRegionPinballDir(options=None):
    """Get all the region pinball directories for this tracing instance.

    @return list of WP pinball dirs
    """

    pb_list = GetWPPinballsNoTID(options)
    pb_list = [os.path.basename(p) + '.pp' for p in pb_list]

    return pb_list


def GetCBSPDataDir(options):
    """
    Get the Data directory for the CBSP global files, making the directory
    if it doesn't already exist.

    @return name of directory, or '' if not found
    """

    name = ''
    if options.cbsp():
        name = '%s.Data' % options.cbsp_name
        if not os.path.isdir(name):
            try:
                os.mkdir(name)
            except OSError:
                msg.PrintAndExit(
                    'function MkCBSPDataDir(), Unable to make directory: ' + name)

    return name


def GetStatusFileName(options=None, cbsp=False):
    """
    Generate the name of the status file.

    If parameters not yet defined for instance file name, return a
    default status file name.

    @return string with status file name
    """

    fname = Config.GetInstanceFileName(config.phase_status_ext, options, cbsp)
    if fname == config.instance_ext + config.phase_status_ext:
        fname = 'CBSP_default' + config.instance_ext + config.phase_status_ext

    return fname


def NewStatusFile(options=None, cbsp=False):
    """
    If not debugging and parameter 'append_status' is not set, create a new,
    empty phase status file.

    @return no return
    """

    if not (hasattr(options, 'debug') and options.debug) and not (
       hasattr(options, 'append_status') and options.append_status):
        sfile = GetStatusFileName(options, cbsp)
        try:
            f = open(sfile, 'w')
        except IOError:
            msg.PrintHelpAndExit('Can\'t open status file: ' + sfile)
        f.close()


def GetHighestIcountRegions(file_list, options):
    """
    For each region pinball in 'file_list', determine the thread (if multi-threaded)
    which has the highest icount.

    Can't assume each region has the same thead with the highest icount, so
    need to search the threads for a given region to find the one for the one
    with the lowest TID.

    The parameter to the function GetAllIcount() is a region pinball name.
    However, the file_list passsed to this function contains all the different
    threads for a region pinball. Thus, this function needs to chose just one
    of the threads in each pinball to pass to GetAllIcount().  Use the brute
    force method of sorting by region number, then iterating through the list
    using using one of the file names found for each region to call GetAllIcount().

    Assume the format of the result file names is:
        log_t0r1_warmup100001500_prolog0_region29982106_epilog0_001_0-08685.1.result
             OR
        log_t0r1_warmupendPC0x0004385f0_warmupendPCCount3125_warmuplength1000009_endPC0x000417cc0_endPCCount1377_length1000002_multiplier1-001_001_0-00162.0 

    @return list of result files with highest count for each region
    """
    #import pdb ; pdb.set_trace()
    if config.global_regions:
      search_str = '.*_globalr(\d+)_warmup.*\.result'
    else:
      search_str = '.*_t\d+r(\d+)_warmup.*\.result'

    def GetRegion(string):
        """
        Method used to sort on region number.

        @return region_num
        """
        match = re.search(search_str, string)
        if match:
          return int(match.group(1))
        else:
          #print "Spurious result file ", string
          return -1

    def DeleteIfSpuriousResultFile(string, options):
        """
        Method used to sort on region number.

        @return 1 if deleted 0 otherwise 
        """
        match = re.search(search_str, string)
        if not match:
          msg.PrintMsgPlus('Spurious result file: ' + string)
          Delete(options, string)
          return 1
        return 0

    # Sort by region number
    #
    reduced_file_list = list(file_list)
    for result_file in file_list:
      if DeleteIfSpuriousResultFile(result_file, options):
       reduced_file_list.remove(result_file) 

    reduced_file_list.sort(key=lambda x: GetRegion(x))

    new_list = []
    cur_region = 1  # Region numbers are 1 based, so start at 1
    cur_file = None
    for fname in reduced_file_list:
        match = re.search(search_str, fname)
        if not match:
          print "Spurious result file ", fname
          continue
           
        region = int(re.search(search_str, fname).group(1))
        # If current region same as last region, skip to next file
        #
        if region == cur_region:
            if not cur_file:
                cur_file = fname
            # Same region, skip this file
            #
            continue

        # Found a file for a new region.
        #
        if cur_file:
            # Get the icounts for all the result files for this region pinball,
            # sorted max->min. Add the result file for the thread with max
            # icount to the list.
            #
            # import pdb;  pdb.set_trace()
            icounts = GetAllIcount(os.path.dirname(cur_file),
                                   os.path.basename(cur_file))
            new_list += [icounts[0][1]]

        # Use new region info as starting point for next iteration
        #
        cur_region = region
        cur_file = fname

    # Add file name for the last region pinball.
    #
    if cur_file:
        icounts = GetAllIcount(os.path.dirname(cur_file),
                               os.path.basename(cur_file))
        new_list += [icounts[0][1]]

    #import pdb ; pdb.set_trace()
    return new_list


def GetMsgFileOption(string):
    """
    If a user defined value for a msg file extension exists, return a PinPlay
    knob which generates a msg file.  Otherwise, return an empty string so the
    default msgifile will be generated.

    @return string knobs
    """

    if config.msgfile_ext:
        # If the msgfile string doesn't start with the char '.', then
        # add it.
        #
        if config.msgfile_ext.find('.') == -1:
            msg_str = '.' + config.msgfile_ext
        else:
            msg_str = config.msgfile_ext
        return ' -pinplay:msgfile ' + string + msg_str
    else:
        # No extension, don't want to add an explict msgfile knob.
        #
        return ''


def ChangeExtension(file_name, old_str, new_str):
    """
    Change the 'extension' at the end a file or directory to a new type.
    For example, change 'foo.pp' to 'foo.Data'

    Do this by substituting the last occurance of 'old_str' in the file
    name with 'new_str'. This is required because the file name may contain
    'old_str' as a legal part of the name which is not at the end of the
    directory name.

    This assumes anything after the last '.' is the file extension.  If there
    isn't a file extention, then just return the original name.

    @return string with file name & new extension
    """

    pos = file_name.rfind(old_str)
    if pos == -1:
        # old_str not found, just return the file_name
        #
        return file_name

    return file_name[:pos] + new_str


def RemoveTID(filename):
    """
    Remove the TID (i.e. the focus_thread) from the end of the file name.

    This assumes the string 'filename' is the base file name, not a file name
    with file extensions.

    For example:

        omnetpp.1.p10000-s10_78607_t0r4_warmup1001500_prolog0_region3500002_epilog0_004_0-00970.0

    not:

        omnetpp.1.p10000-s10_78607_t0r4_warmup1001500_prolog0_region3500002_epilog0_004_0-00970.0.address

    @return string of file name w/o TID
    """

    # import pdb;  pdb.set_trace()

    # Remove dot immediately followed by digit(s) at end.
    filename = re.sub('\.\d+$', '', filename)

    return filename


def PintoolHelpKit(kit, options):
    """
    Get the pintool help msg for a given kit.

    @return result of command to get msg
    """

    cmd = os.path.join(kit.path, kit.pin)
    kit.binary_type = config.ARCH_INTEL64  # Type doesn't matter, can be either one
    cmd += kit.GetPinToolKnob()
    if kit.kit_type == config.PINPLAY:
        cmd += ' -help'
    elif kit.kit_type == config.SDE:
        cmd += ' -thelp'
    cmd += ' -- ls /'  # Any generic command will work
    result = RunCmd(cmd, options, '',
                         concurrent=False,
                         print_time=False,
                         print_cmd=False)
    return result
