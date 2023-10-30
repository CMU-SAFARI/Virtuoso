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
# Read in a file of frequency vectors (BBV or LDV) and execute one of several
# actions on it.  Default is to generate a regions CSV file from a BBV file.
# Other actions include:
#   normalizing and projecting FV file to a lower dimension
#
# $Id: regions.py,v 1.30 2015/10/28 22:24:13 tmstall Exp tmstall $

import datetime
import glob
import math
import optparse
import os
import random
import re
import sys

import cmd_options
import msg
import util

err_msg = lambda string: msg.PrintAndExit('This is not a valid ' + string + \
            '\nUse -h for help.')


def OpenFile(fl, type_str):
    """
    Check to make sure a file exists and open it.

    @return file pointer
    """

    # import pdb;  pdb.set_trace()
    if not os.path.isfile(fl):
        msg.PrintAndExit('File does not exist: %s' % fl)
    fp = util.OpenCompressFile(fl)
    if fp == None:
        err_msg(type_str + fl)

    return fp


def OpenNormalFVFile(fv_file, type_str):
    """
    Open a normalized frequency vector file and check to make sure it's valid.

    The first line of a valid normalized FV file contains the number of vectors
    in the file followed by the char ':' and an optional char 'w'.

    Subsequent lines contain an optional weight (if 'w' is present on first line) followed
    by the number of fields in the vector, the char ':' and the values for the vector:  For example:

    80:w
    0.01250000000000000069 3:  0.00528070484084160793 0.00575272877173275011 0.00262986399034366479
    0.01250000000000000069 2:  0.00528070484084160793 0.00575272877173275011

    @return file pointer
    """

    # Read in the 1st line of the file and check for errors.
    #
    # import pdb;  pdb.set_trace()
    type_str = 'normalized frequency vector file: '
    fp = OpenFile(fv_file, type_str)
    line = fp.readline()
    field = line.split(':')
    num_vect = field[0]
    if not util.IsInt(num_vect):
        err_msg(type_str + fv_file)
    if len(field) == 2:
        if not 'w' in field[1]:
            err_msg(type_str + fv_file)
        else:
            weights = True

    # Read the 2nd line: an optional weight, the number of values in the vector and the vector itself.
    #
    line = fp.readline()
    if line == '':
        err_msg(type_str + fv_file)
    field = line.split()
    if weights:
        if not util.IsFloat(field[0]):
            err_msg(type_str + fv_file)
        field = field[1:]
    if len(field) < 2:
        err_msg(type_str + fv_file)
    num_field = int(field[0].split(':')[0])
    if not util.IsInt(num_field):
        err_msg(type_str + fv_file)
    field = field[1:]
    if len(field) != num_field:
        err_msg(type_str + fv_file)
    for f in field:
        if not util.IsFloat(f):
            err_msg(type_str + fv_file)
    fp.seek(0, 0)

    return fp


def OpenFVFile(fv_file, type_str):
    """
    Open a frequency vector file and check to make sure it's valid.  A valid
    FV file must contain at least one line which starts with the string 'T:'.

    @return file pointer
    """

    # import pdb;  pdb.set_trace()
    fp = OpenFile(fv_file, type_str)
    line = fp.readline()
    while not line.startswith('T:') and line:
        line = fp.readline()
    if not line.startswith('T:'):
        err_msg(type_str + fv_file)
    fp.seek(0, 0)

    return fp


def OpenSimpointFile(sp_file, type_str):
    """
    Open a simpoint file and check to make sure it's valid.  A valid
    simpoint file must start with an integer.

    @return file pointer
    """

    fp = OpenFile(sp_file, type_str)
    line = fp.readline()
    field = line.split()
    if not util.IsInt(field[0]):
        err_msg(type_str + sp_file)
    fp.seek(0, 0)

    return fp


def OpenWeightsFile(wt_file, type_str):
    """
    Open a weight file and check to make sure it's valid.  A valid
    wieght file must start with an floating point number.

    @return file pointer
    """

    fp = OpenFile(wt_file, type_str)
    line = fp.readline()
    field = line.split()
    if not util.IsFloat(field[0]):
        err_msg(weight_str + wt_file)
    fp.seek(0, 0)

    return fp


def GetOptions():
    """
    Get users command line options/args and check to make sure they are correct.

    @return List of options and 3 file pointers for bbv, simpoint and weights files
    """

    version = '$Revision: 1.30 $'
    version = version.replace(' ', '')
    ver = version.replace(' $', '')
    us = '%prog [options] action file_name [file_name]'
    desc = 'Implements several actions used to process FV (Frequency Vector) files.  ' \
           'One action, and only one, must be defined in order for the script to run.  '\
           'All actions require at least one file name be given using an option. \n\n'\
           '' \
           'There are two types of frequency vector files:  '\
           '                                                            '\
           'BBV = Basic Block Vector, '\
           '                                                            '\
           'LDV = LRU stack Distance Vector'

    def combine(parser, group):
        """
        IMPORTANT NOTE:
        This is a local definition for the option which has more help
        information than the default defined in cmd_options.py.  This info is
        specific to this script and is not applicable to the other locations
        where the option is used.

        Default value for combine to 'none' instead of setting it to a value
        (as it is in cmd_options.py).  This allows the option to be used to
        determine what to do.

        @return  No return value
        """

        method = cmd_options.GetMethod(parser, group)
        method(
            "--combine",
            dest="combine",
            default=None,
            help="Combine the vectors for BBV and LDV files into a single FV file, use scaling "
            "factor COMBINE (0.0 >= COMBINE <= 1.0).  The BB vectors "
            "are scaled by COMBINE, while the LD vectors are scaled by 1-COMBINE.  Default: 0.5  "
            "Assumes both files have already been transformed by the appropriate process "
            "(project/normal for BBV, weight/normal for LDV). "
            "Must use --normal_bbv and --normal_ldv to define files to process.")

    util.CheckNonPrintChar(sys.argv)
    parser = optparse.OptionParser(
        usage=us,
        version=ver,
        description=desc,
        formatter=cmd_options.BlankLinesIndentedHelpFormatter())

    cmd_options.dimensions(parser, '')
    cmd_options.focus_thread(parser, '')

    # Options which define the actions the script to execute
    #
    action_group = cmd_options.ActionGroup(parser)

    combine(parser, action_group)
    cmd_options.csv_region(parser, action_group)
    cmd_options.project_bbv(parser, action_group)
    cmd_options.weight_ldv(parser, action_group)

    parser.add_option_group(action_group)

    # Options which list the files the script can process
    #
    # import pdb;  pdb.set_trace()
    file_group = cmd_options.FileGroup(parser)

    cmd_options.bbv_file(parser, file_group)
    cmd_options.ldv_file(parser, file_group)
    cmd_options.normal_bbv(parser, file_group)
    cmd_options.normal_ldv(parser, file_group)
    cmd_options.region_file(parser, file_group)
    cmd_options.vector_file(parser, file_group)
    cmd_options.weight_file(parser, file_group)

    parser.add_option_group(file_group)

    # Parse command line options and get any arguments.
    #
    (options, args) = parser.parse_args()

    # Added method cbsp() to 'options' to check if running CBSP.
    #
    util.AddMethodcbsp(options)

    def TrueXor(*args):
        """Return xor of some booleans."""
        return sum(args) == 1

    # Must have one, and only one, action on command line.
    #
    # import pdb;  pdb.set_trace()
    if not TrueXor(options.csv_region, options.project_bbv, options.weight_ldv, \
       options.combine != None, options.vector_file != None):
        msg.PrintAndExit(
            'Must give one, and only one, action for script to execute.\n'
            'Use -h to get help.')

    # Check to see if options required for the various actions are given.
    #
    file_error = lambda file, action: msg.PrintAndExit("Must use option '" + file + \
        "' to define the file to use with '" + action + "'.   \nUse -h for help.")

    # import pdb;  pdb.set_trace()
    fp_bbv = fp_ldv = fp_simp = fp_weight = None

    if options.combine:
        # Check to make sure the option 'combine' is an acceptable value.  If so, then turn it into a float.
        #
        util.CheckCombine(options)
        options.combine = float(options.combine)

        # Then check to make sure required files are given.
        #
        if not options.normal_bbv:
            file_error('--normal_bbv', '--combine')
        if not options.normal_ldv:
            file_error('--normal_ldv', '--combine')
        fp_bbv = OpenNormalFVFile(options.normal_bbv,
                                  'projected, normalized BBV file: ')
        fp_ldv = OpenNormalFVFile(options.normal_ldv,
                                  'projected, normalized BBV file: ')

    if options.csv_region:
        if not options.bbv_file:
            file_error('--bbv_file', '--csv_region')
        if not options.region_file:
            file_error('--region_file', '--csv_region')
        if not options.weight_file:
            file_error('--weight_file', '--csv_region')
        fp_bbv = OpenFVFile(options.bbv_file, 'Basic Block Vector (bbv) file: ')
        fp_simp = OpenSimpointFile(options.region_file, 'simpoints file: ')
        fp_weight = OpenWeightsFile(options.weight_file, 'weights file: ')

    if options.project_bbv:
        if not options.bbv_file:
            file_error('--bbv_file', '--project_bbv')
        fp_bbv = OpenFVFile(options.bbv_file, 'Basic Block Vector (bbv) file: ')

    if options.weight_ldv:
        if not options.ldv_file:
            file_error('--ldv_file', '--weight_ldv')
        fp_ldv = util.OpenCompressFile(options.ldv_file)

    return (options, fp_bbv, fp_ldv, fp_simp, fp_weight)


def GetSlice(fp):
    """
    Get the frequency vector for one slice (i.e. line in the FV file).

    All the frequency vector data for a slice is contained in one line.  It
    starts with the char 'T'.  After the 'T', there should be a sequence
    containing one, or more, of the following sets of tokens:
       ':'  integer  ':' integer
    where the first integer is the dimension index and the second integer is
    the count for that dimension. Ignore any whitespace.

    @return list of the frequency vectors for the slice; element = (dimension, count)
    """

    fv = []
    line = fp.readline()
    while not line.startswith('T') and line:
        # print 'Skipping line: ' + line

        # Don't want to skip the part of BBV files at the end which give
        # information on the basic blocks in the file.  If 'Block id:' is
        # found, then back up the file pointer to before this string.
        #
        if line.startswith('Block id:'):
            fp.seek(0 - len(line), os.SEEK_CUR)
            return []
        line = fp.readline()
    if line == '': return []

    # If vector only contains the char 'T', then assume it's a slice which
    # contains no data.
    #
    if line == 'T\n':
        fv.append((0, 0))
    else:
        blocks = re.findall(':\s*(\d+)\s*:\s*(\d+)\s*', line)
        # print 'Slice:'
        for block in blocks:
            # print block
            bb = int(block[0])
            count = int(block[1])
            fv.append((bb, count))

    # import pdb;  pdb.set_trace()
    return fv


def GetBlockIDs(fp):
    """
    Get the information about each basic block which is stored at the end
    of BBV frequency files.

    Extract the values for fields 'block id' and 'static instructions' from
    each block.  Here's an example block id entry:

      Block id: 2233 0x69297ff1:0x69297ff5 static instructions: 2 block count: 1 block size: 5

    static instructions is number of instructions in the basic block
    block count is the number of times the block is executed

    @return list of the basic block info, elements are (block_id, icount of block)
    """

    block_id = {}
    line = fp.readline()
    while not line.startswith('Block id:') and line:
        line = fp.readline()
    if line:
        while line.startswith('Block id:'):
            bb = int(line.split('Block id:')[1].split()[0])
            count = int(line.split('static instructions:')[1].split()[0])
            block_id[bb] = count
            line = fp.readline()

    # import pdb;  pdb.set_trace()
    return block_id


def PrintVectorFile(matrix):
    """
    Print a matrix composed of a list of list of floating point values in the
    format required by simpoint.

    Format of 1st line:
        num_rows: w
            num_rows = number of rows in matrix
            'w' indicates there are weights for each vector

    Format of subsequent lines with vector information:
        weight num_dim: value, value ... value
            weight  = 1/num_rows (i.e. all vectors have an equal weight)
            num_dim = number of values per row
            value  = the matrix values

    Example output:
        162:w
        0.00617 15:  -0.07 0.00 0.33 -0.22 -0.30 0.32 -0.05 0.27 0.15 0.32 -0.24 0.30 0.12 0.25 0.17
        0.00617 15:  -0.00 0.30 0.63 -0.30 -0.22 0.83 -0.13 0.08 0.13 0.62 -0.34 0.67 0.10 0.31 0.36
        0.00617 15:  -0.00 0.30 0.63 -0.30 -0.22 0.83 -0.13 0.08 0.13 0.62 -0.34 0.67 0.10 0.31 0.36

    @return no return value.
    """

    # Print 1st line
    #
    # import pdb;  pdb.set_trace()
    num_row = len(matrix)
    print '%d:w' % num_row

    # Print vectors
    #
    weight = 1 / float(num_row)
    dim = len(matrix[0])
    index = 0
    while index < num_row:
        dim = len(matrix[index])
        print '%.23f %d:' % (weight, dim),
        vector = matrix[index]
        # for block in vector:    print '%.19f' % block,
        for block in vector:
            print '%.20f' % block,
        print
        index += 1
        # import pdb;  pdb.set_trace()


def ReadVectorFile(v_file):
    """
    Read in a matrix composed of a list of list of floating point values in the
    format required by simpoint.

    Format of 1st line:
        num_rows: w
            num_rows = number of rows in matrix
            'w' indicates there are weights for each vector

    Format of subsequent lines with vector information:
        weight num_dim: value, value ... value
            weight  = 1/num_rows (i.e. all vectors have an equal weight)
            num_dim = number of values per row
            value  = the matrix values

    Example input:
        162:w
        0.00617 15:  -0.07 0.00 0.33 -0.22 -0.30 0.32 -0.05 0.27 0.15 0.32 -0.24 0.30 0.12 0.25 0.17
        0.00617 15:  -0.00 0.30 0.63 -0.30 -0.22 0.83 -0.13 0.08 0.13 0.62 -0.34 0.67 0.10 0.31 0.36
        0.00617 15:  -0.00 0.30 0.63 -0.30 -0.22 0.83 -0.13 0.08 0.13 0.62 -0.34 0.67 0.10 0.31 0.36

    @return list of lists which is the matrix
    """

    matrix = []
    weights = False

    # Read in the header of the file and do some error checking.
    #
    fp = OpenFile(v_file, 'normalized frequency vector file: ')
    line = fp.readline()
    field = line.split(':')
    num_vect = field[0]
    if len(field) == 2:
        if not 'w' in field[1]:
            msg.PrintAndExit('Illegal char given as weight: ' + field[1])
        else:
            weights = True

    count = 0
    line = fp.readline()
    while True:
        if line == '':
            return matrix

        # Read in an optional weight, the number of values in the vector and the vector itself.
        #
        vector = []
        field = line.split()
        if weights:
            field = field[1:]
        if len(field) < 2:
            msg.PrintAndExit('Corrupted vector format:\n' + line)
        num_float = int(field[0].split(':')[0])
        if len(field) - 1 != num_float:
            msg.PrintAndExit('Incorrect number of values in vector:\n' + line)
        field = field[1:]
        for value in field:
            vector.append(float(value))
        matrix.append(vector)
        count += 1

        line = fp.readline()

    # Make sure 'num_vect' vectors were read from the file.
    #
    if count != num_vect:
        msg.PrintAndExit('Incorrect number of values in vector:\n' + line)

    return matrix

############################################################################
#
#  Functions for generating regions CSV files
#
############################################################################


def GetWeights(fp):
    """
    Get the regions and weights from a weights file.

    @return lists of regions and weights
    """

    # This defines the pattern used to parse one line of the weights file.
    # There are three components to each line:
    #
    #   1) a floating point number  (group number 1 in the match object)
    #   2) white space
    #   3) a decimal number         (group number 2 in the match object)
    #
    # 1) This matches floating point numbers in either fixed point or scientific notation:
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
    # 2) This matches white space:
    #   \s
    #
    # 3) This matches a decimal number with one, or more digits:
    #   \d+
    #
    pattern = '(-?\ *[0-9]+\.?[0-9]*(?:[Ee]\ *-?\ *[0-9]+)?)\s(\d+)'

    weight_dict = {}
    for line in fp.readlines():
        field = re.match(pattern, line)

        # Look for the special case where the first field is a single digit
        # without the decimal char '.'.  This should be the weight of '1'.
        #
        if not field:
            field = re.match('(\d)\s(\d)', line)
        if field:
            weight = float(field.group(1))
            region = int(field.group(2))
            weight_dict[region] = weight

    return weight_dict


def GetSimpoints(fp):
    """
    Get the regions and slices from the Simpoint file.

    @return list of regions and slices from a Simpoint file
    """

    simp_dict = {}
    for line in fp.readlines():
        field = re.match('(\d+)\s(\d+)', line)
        if field:
            slice_num = int(field.group(1))
            region = int(field.group(2))
            simp_dict[region] = slice_num

    return simp_dict


def GetRegionBBV(fp, slice_set):
    """
    Read all the frequency vector slices and the basic block id info from a
    basic block vector file.  Put the data into a set of lists which are used
    in generating CSV regions.

    @return cumulative_icount, all_bb, bb_freq, bb_num_instr, region_bbv
    """
    # Dictionary which contains the number of instructions in each BB.
    # Key is basic block number.
    #
    bb_num_instr = {}

    # Dictionary which contains the number of times a BB was executed
    # Key is basic block number.
    #
    bb_freq = {}

    # List of lists of basic block vectors, each inner list contains the blocks for one of the
    # representative regions. 
    #
    region_bbv = []

    # List of the cumulative sum of instructions in the slices.  There is one
    # entry for each slice in the BBV file which contains the total icount up
    # to the end of the slice.
    #
    cumulative_icount = []

    # Cumulative sum of instructions so far
    #
    run_sum = 0

    # Get each slice & generate some data on it.
    #
    slice_num = 0
    while True:
        fv = GetSlice(fp)
        if fv == []:
            break
        # print fv

        # Get total icount for the basic blocks in this slice
        #
        sum = 0
        for bb in fv:
            count = bb[1]
            sum += count

            # Add the number instructions for the current BB to total icount for
            # this specific BB (bb_num_instr).  
            #
            bb_num_instr[bb] = bb_num_instr.get(bb, 0) + count

            # Increment the number of times this BB number has been encountered
            #
            bb_freq[bb] = bb_freq.get(bb, 0) + 1

        # Record the cumulative icount for this slice.
        #
        if sum != 0:
            run_sum += sum
            cumulative_icount += [run_sum]

        # If slice is a representative region, record the basic blocks of the slice.
        #
        if slice_num in slice_set:
            region_bbv.append(sorted(b[0] for b in fv))
        slice_num += 1

    # import pdb;  pdb.set_trace()

    # Read the basic block information at the end of the file if it exists. 
    #
    # import pdb;  pdb.set_trace()
    all_bb = GetBlockIDs(fp)
    # if all_bb != {}
    # print 'Block ids'
    # print all_bb

    # import pdb;  pdb.set_trace()
    return cumulative_icount, all_bb, bb_freq, bb_num_instr, region_bbv


def CheckRegions(simp_dict, weight_dict):
    """
    Check to make sure the simpoint and weight files contain the same regions.

    @return no return value
    """
    
	# if weight regions dictionary is smaller then simpoints regions 
    # then add missing values with weight 0
    if len(simp_dict) > len(weight_dict):
        for simp_key in simp_dict.keys():
            if simp_key not in weight_dict.keys():
                weight_dict[simp_key] = 0

    if len(simp_dict) != len(weight_dict) or \
       simp_dict.keys() != weight_dict.keys():
        msg.PrintMsg('ERROR: ICount Regions in these two files are not identical')
        msg.PrintMsg('   Simpoint regions: ' + str(simp_dict.keys()))
        msg.PrintMsg('   Weight regions:   ' + str(weight_dict.keys()))
        cleanup()
        sys.exit(-1)


def GenRegionCSV(options, fp_bbv, fp_simp, fp_weight):
    """
    Read in three files (BBV, weights, simpoints) and print to stdout
    a regions CSV file which defines the representative regions.

    @return no return value
    """

    # Read data from weights, simpoints and BBV files.
    # Error check the regions.
    #
    weight_dict = GetWeights(fp_weight)
    simp_dict = GetSimpoints(fp_simp)
    cumulative_icount, all_bb, bb_freq, bb_num_instr, region_bbv = GetRegionBBV(
        fp_bbv, set(simp_dict.values()))
    CheckRegions(simp_dict, weight_dict)

    total_num_slices = len(cumulative_icount)
    total_instr = cumulative_icount[len(cumulative_icount) - 1]

    # Print header information
    #
    msg.PrintMsgNoCR('# Regions based on: ')
    for string in sys.argv:
        msg.PrintMsgNoCR(string + ' '),
    msg.PrintMsg('')
    msg.PrintMsg(
        '# comment,thread-id,region-id,simulation-region-start-icount,simulation-region-end-icount,region-weight')
    # msg.PrintMsg('')

    # Print region information
    #
    # import pdb;  pdb.set_trace()
    tid = 0
    if options.focus_thread != -1:
      if options.focus_thread == 'global':
        tid = 'global'
      else:
        tid = int(options.focus_thread)
    total_icount = 0
    for region in sorted(simp_dict.keys()):
        # Calculate the info for the regions and print it.
        #
        slice_num = simp_dict[region]
        weight = weight_dict[region]
        # import pdb;  pdb.set_trace()
        if slice_num > 0:
            start_icount = cumulative_icount[slice_num - 1] + 1
        else:
            # If this is the first slice, set the initial icount to 0
            #
            start_icount = 0
        end_icount = cumulative_icount[slice_num]
        length = end_icount - start_icount + 1
        total_icount += length
        msg.PrintMsg('# Region = %d Slice = %d Icount = %d Length = %d Weight = %.5f' % \
            (region + 1, slice_num, start_icount, length, weight))
        if tid == 'global':
          msg.PrintMsg('cluster %d from slice %d,%s,%d,%d,%d,%.5f\n' % \
            (region, slice_num, tid, region + 1, start_icount, end_icount, weight))
        else:
          msg.PrintMsg('cluster %d from slice %d,%d,%d,%d,%d,%.5f\n' % \
            (region, slice_num, tid, region + 1, start_icount, end_icount, weight))

    # This code is a failed attempt to calculate the coverage of the traces.  It does NOT
    # work.  It's kept because this may be fixed at some future time.
    #
    all_bb = False
    if all_bb:
        # Get the total number of instructions in all the basic blocks
        #
        total_bb_icount = 0
        for block in all_bb.keys():
            total_bb_icount += all_bb.get(block)
        # print 'Num instructions in all BB:          %6d' % total_bb_icount

        # Get the number of instructions in the basic blocks for each region.
        #
        # import pdb;  pdb.set_trace()
        region_num = 0
        for bbv in region_bbv:
            region_icount = 0
            for block in bbv:
                if all_bb.get(block) != None:
                    region_icount += all_bb.get(block)
            region_icount += 1
            # print 'Num instructions in BB of region %2d: %6d' % (region_num, region_icount)
            # print 'Static trace coverage: %.4f' % (float(region_icount)/total_bb_icount)
            # import pdb;  pdb.set_trace()
            region_num += 1

        # Get the set of basic blocks in the regions and the number of instructions
        # in the BBs of this set.
        #
        all_region_bb = set()
        for bbv in region_bbv:
            all_region_bb.update(bbv)
        all_region_icount = sum([all_bb.get(k) for k in all_region_bb])
        # print 'Coverage of all instructions of basic blocks in regions: %.4f' % (float(all_region_icount)/total_bb_icount)

        # Print summary statistics
        #
        # import pdb;  pdb.set_trace()
    msg.PrintMsg('# Total instructions in %d regions = %d' %
                 (len(simp_dict), total_icount))
    msg.PrintMsg('# Total instructions in workload = %d' %
                 cumulative_icount[total_num_slices - 1])
    msg.PrintMsg('# Total slices in workload = %d' % total_num_slices)

############################################################################
#
#  Functions for normalization and projection
#
############################################################################


def GetDimRandomVector(proj_matrix, proj_dim, dim):
    """
    Get the random vector for dimension 'dim'.  If it's already in 'proj_matrix',
    then just return it.  Otherwise, generate a new random vector of length
    'proj_dim' with values between -1 and 1.

    @return list of length 'dim' which contains vector of random values
    """

    # import pdb;  pdb.set_trace()
    if proj_matrix.has_key(dim):
        # print 'Using random vector: %4d' % dim
        vector = proj_matrix.get(dim)
    else:
        # print 'Generating random vector: %4d' % dim
        random.seed()  # Use default source for seed
        vector = []
        index = 0
        while index < proj_dim:
            vector.append(random.uniform(-1, 1))
            index += 1
        proj_matrix[dim] = vector

    return vector


def PrintProjMatrix(matrix):
    """
    Print a projection matrix composed of a directory of vectors.

    @return no return value.
    """

    for key in sorted(matrix.keys()):
        for block in matrix.get(key):
            print '%6.3f' % block,
        print


def ProjectFVFile(fp, proj_dim=15):
    """
    Read all the slices in a frequency vector file, normalize them and use a
    random projection matrix to project them onto a result matrix with dimensions:
        num_slices x proj_dim.

    @return list of lists which contains the result matrix
    """

    # Test code to read in a pre-existing random projection matrix.
    # This allows the same matrix to be used for both Simpoin and this script.
    #
    # matrix = ReadVectorFile('simpoint_random_proj_matrix.txt')
    # # PrintVectorFile(matrix)
    # sim_proj_matrix = {}
    # index = 1
    # for vector in matrix:
    #     sim_proj_matrix[index] = vector
    #     index += 1

    # Dictionary which contains the random projection matrix.  The keys are the
    # FV dimension (NOT the slice number) and the value is a list of random
    # values with length 'proj_dim'.
    #
    proj_matrix = {}

    # List of lists which contains the result matrix. One element for each slice. 
    #
    result_matrix = []

    while True:
        fv = GetSlice(fp)
        if fv == []:
            break

        # Get the sum of all counts for this slice for use in normalizing the
        # dimension counts.
        #
        # import pdb;  pdb.set_trace()
        # print fv
        vector_sum = 0
        for block in fv:
            vector_sum += block[1]
        # for block in fv:    vector_sum += math.fabs(block[1])

        # Initilize this slice/vector of the result matrix to zero
        #
        result_vector = [0.0] * proj_dim

        # For each element in the slice, project using the "dimension of the
        # element", not the element index itself!
        #
        sum = 0
        # import pdb;  pdb.set_trace()
        for block in fv:
            dim = block[0]
            # print 'Dim: %4d' % dim
            count = float(block[1]) / vector_sum  # Normalize freq count
            # print 'Count: %d Normalized count: %f' % (block[1], count)

            # For testing only, use the random project matrix read from a file.
            #
            # proj_vector = sim_proj_matrix.get(dim)

            # Get the random vector for the dimension 'dim' and project the values for
            # 'dim' into the result
            #
            proj_vector = GetDimRandomVector(proj_matrix, proj_dim, dim)
            index = 0
            while index < proj_dim:
                result_vector[index] += count * proj_vector[index]
                index += 1

        result_matrix.append(result_vector)

    # Debugging code
    #
    # PrintProjMatrix(proj_matrix)
    # sys.exit(0)

    # import pdb;  pdb.set_trace()
    return result_matrix


def cleanup():
    """
    Close all open files and any other cleanup required.

    @return no return value
    """

    if fp_bbv:
        fp_bbv.close()
    if fp_ldv:
        fp_ldv.close()
    if fp_simp:
        fp_simp.close()
    if fp_weight:
        fp_weight.close()

############################################################################
#
#  Functions for processing LDV files
#
############################################################################

# Maximum dimension in the frequency vector.
#
max_dim = 31


def GetLDVWeights():
    """
    Generate a list of weights to be applied to LDV frequency vectors.

    The weight is equivalent to the number of cycles required to access memory
    at a given 'distance'.  For a given distance 'x' in the LDV vector, the number
    of cycles to access it is:
        2^(x/exp_scale)

    A maximum weight is set to approximate the number of cycles required to access DRAM.

    @return list of weights
    """

    # Minimum weight which should be applied.
    #
    min_weight = 4.0

    # Maximum weight which should be applied.
    #
    max_weight = 200.0

    # 1/scale of the exponent
    #
    exp_scale = 4.0

    # List of weights.
    #
    wt_list = []

    for val in range(0, max_dim):
        weight = math.pow(2.0, val / exp_scale)
        if weight < min_weight: weight = min_weight
        if weight > max_weight: weight = max_weight
        wt_list.append(weight if val > 10 else 0.1)

    return wt_list


def PadLDVBlanks(fv, num_dim):
    """
    Pad LDV vector with blanks to ensure it's of the proper length.

    The LDV vector may not contain a entry for each distance, thus the vector
    length will be less than the max length.  Need to pad all missing distance
    dimensions with a value of 0. Thus making all vectors of the same length of
    'max_dim'.

    @return FV padded with blanks
    """

    # Generate a vector of the desired length with a count of 0 for each distance.
    #
    new_vec = [[i, 0] for i in range(0, num_dim)]

    # Fill in this vector with the values from the frequency vector.
    #
    for block in fv:
        distance = block[0]
        count = block[1]
        if distance < num_dim:
            new_vec[distance] = [distance, count]

    return new_vec


def GetWeightedLDV(fp, num_dim=32):
    """
    Read the frequency vectors for all the slices in a LRU stack Distance Vector
    (ldv) file, apply weights based in the distances for each element in the vector
    and normalize the resulting vector.

    @return list of normalized, weighted LDV frequency vectors
    """

    # List of lists which contains the result matrix. One element for each slice. 
    #
    result_matrix = []

    # Get the weights to apply to the LDV frequency vectors.
    #
    weight = GetLDVWeights()

    fp = util.OpenCompressFile(options.ldv_file)

    # For each frequency vector, apply the weight and normalize the result.
    #
    slice_num = 0
    while True:
        ldv = GetSlice(fp)
        if ldv == []:
            break
        # print ldv

        if num_dim == 0:
            result_matrix.append([])
            continue

        # Apply the appropriate weight for each distance in the frequency vector.
        #
        vector_sum = 0  # Sum of all counts in this vector
        fv = [ (0,0) for i in range(0,num_dim) ]
        for block in ldv:
            distance = block[0]
            count = block[1]
            # The distances are 0 based, so maximum allowed distance value is 'max-dim-1'.
            #
            if distance > max_dim - 1:
                print 'ERROR: Distance read from LDV file (%d) was greater than max value of: %d' % (
                    distance, max_dim - 1)
                sys.exit(-1)
            count = count * weight[distance]
            index = distance - 10               # Interesting LDV range is from 10 ...
            index = index * num_dim / (25 - 10) # ... to 25, rescale this range onto 0..num_dim
            if index >= num_dim:
                index = num_dim - 1
            fv[index] = (distance, count)
            vector_sum += count
        # print fv
        fv = PadLDVBlanks(fv, num_dim)
        # print "Padding"
        # print fv

        # Normalize the weighted counts for the frequency vector.
        #
        # import pdb;  pdb.set_trace()
        result_vector = []
        for block in fv:
            count = block[1]
            if count > 0:
                result_vector.append(float(count) / vector_sum)
            else:
                result_vector.append(0.0)  # Vector contains no data
        # print result_vector

        result_matrix.append(result_vector)

    return result_matrix

############################################################################
#
#  Functions for scaling and combining BBV and LDV files
#
############################################################################


def ScaleCombine(options):
    """
    Scale each vector in the BBV and LDV normalized matrices and concatenate
    them into a new vector.

    @return none
    """

    result_matrix = []

    # Get the two normalized input matrices and make sure they
    # have the same number of rows (i.e. slices).
    #
    bbv_matrix = ReadVectorFile(options.normal_bbv)
    ldv_matrix = ReadVectorFile(options.normal_ldv)
    num_rows = len(bbv_matrix)
    if num_rows != len(ldv_matrix):
        msg.PrintAndExit(
            'Normalized BBV and LDV matrices have a different number of rows.')

    bbv_scale = options.combine
    ldv_scale = 1.0 - bbv_scale
    index = 0
    while index < num_rows:
        vector_sum = 0.0
        tmp_vector = []
        new_vector = []
        bbv_vector = bbv_matrix[index]
        ldv_vector = ldv_matrix[index]

        # Scale each of input vectors.
        #
        for value in bbv_vector:
            value = value * bbv_scale
            tmp_vector.append(value)
        #    vector_sum += value
        for value in ldv_vector:
            value = value * ldv_scale
            tmp_vector.append(value)

        # This is for normalization after the summation, which is not needed.
        # Code kept just in case.
        #
        #    vector_sum += value
        # for value in tmp_vector:
        #     new_vector.append(value/vector_sum)
        # result_matrix.append(new_vector)

        # import pdb;  pdb.set_trace()
        result_matrix.append(tmp_vector)
        index += 1

    PrintVectorFile(result_matrix)

############################################################################

options, fp_bbv, fp_ldv, fp_simp, fp_weight = GetOptions()

if options.combine >= 0.0:
    ScaleCombine(options)
elif options.csv_region:
    GenRegionCSV(options, fp_bbv, fp_simp, fp_weight)
elif options.project_bbv:
    result_matrix = ProjectFVFile(fp_bbv, proj_dim=int(options.dimensions))
    PrintVectorFile(result_matrix)
elif options.weight_ldv:
    result_matrix = GetWeightedLDV(fp_ldv, num_dim=int(options.dimensions))
    PrintVectorFile(result_matrix)
elif options.vector_file:
    # This is an undocumented action of the script.  If user gives a vector
    # file, then just print it.  Useful for things like changing the number of
    # significant digits in a FV file to a standard value.  The output from
    # this script will always have 20 significant digits in the frequency
    # vectors.
    #
    matrix = ReadVectorFile(options.vector_file)
    PrintVectorFile(matrix)

cleanup()
sys.exit(0)
