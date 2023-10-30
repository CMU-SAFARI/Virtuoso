#!/usr/bin/env python
#
# $Id: pb_weight.py,v 1.5 2015/08/15 20:02:37 tmstall Exp tmstall $

import os
import sys
import optparse
import cmd_options
import msg
import util
"""
@package pb_weight

Parse the different fields in the region pinballs generated using the default
file naming scheme.  Print either just the file name and weight or a selected
subset of all the fields.
"""

err_msg = lambda string: msg.PrintAndExit('Unable to get weight for this file' + string + \
            '\nUse -h for help.')


def GetOptions():
    """
    Get users command line options/args and check to make sure they are correct.

    @return options, args
    """
    version = '$Revision: 1.5 $'
    version = version.replace('$Revision: ', '')
    ver = version.replace(' $', '')
    us = '%prog [options] pb_file_name [pb_file_name(s)]'
    desc = 'Print the weight defined in a region pinball file name. Can also ' \
           'print out a select subset of all the fields in the name.'

    util.CheckNonPrintChar(sys.argv)
    parser = optparse.OptionParser(usage=us, version=ver, description=desc)

    parser.add_option("-a", "--all",
                      dest="all",
                      action="store_true",
                      default=False,
                      help="Print out additional fields in the file name")

    (options, args) = parser.parse_args()

    # Added method cbsp() to 'options' to check if running CBSP.
    #
    util.AddMethodcbsp(options)


    return options, args

############################################################################

options, args = GetOptions()

# Use format strings to make 'pretty' output.  Need different format strings
# for the weights because it requires a 'f' to print the actual float, while
# in the header it is just another string.

# Format strings to print weight header and the weight itself.
# 'weight_position' is the postion of the weight field in the format sttmt.  In
# this case it's the 2nd field to be printed.
#
weight_position = '1:'
weight_head_fmt = '{%s>8.6}' % weight_position
weight_fmt = '{%s>8.6f}' % weight_position

# Format strings for just the file name and weight.
#
file_head_fmt = '{0:<100} ' + weight_head_fmt
file_fmt = '{0:<100} ' + weight_fmt

# Format a subset of all the strings defined in the file name.  Order of
# fields:
#   file, TID, region_num, warmup, prolog, region, epilog, weight
#
fields_fmt = ' \
{0:<100} \
{1:>6} \
{2:>6} \
{3:>10} \
{4:>10} \
{5:>12} \
{6:>12} \
'

# 'weight_position' is the next field after the last one defined in
# 'fields_fmt'.  Currently it's the 8th field.
#
weight_position = '7:'
weight_head_fmt = '{%s>8.6}' % weight_position
weight_fmt = '{%s>8.6f}' % weight_position
all_head_fmt = fields_fmt + weight_head_fmt
all_fmt = fields_fmt + weight_fmt

# Print the header.
#
# import pdb ; pdb.set_trace()
if options.all:
    msg.PrintMsg(all_head_fmt.format('File', 'TID', 'Region', 'Warmup', 'Prolog', 'Region', \
        'Epilog', 'Weight'))
else:
    msg.PrintMsg(file_head_fmt.format('File', 'Weight'))

# Process each file name on the command line.
#
for name in args:

    # Get the fields from the file name.
    #
    name = os.path.basename(name)
    fields = util.ParseFileName(name)

    # Get the values from the dictionary fields
    #
    try:
        pid = fields['pid']
    except:
        pid = None
    try:
        region_num = fields['region_num']
    except:
        region_num = None
    try:
        tid = fields['tid']
    except:
        tid = None
    try:
        trace_num = fields['trace_num']
    except:
        trace_num = None
    try:
        warmup = fields['warmup']
    except:
        warmup = None
    try:
        prolog = fields['prolog']
    except:
        prolog = None
    try:
        region = fields['region']
    except:
        region = None
    try:
        epilog = fields['epilog']
    except:
        epilog = None
    try:
        weight = float(fields['weight'])
    except:
        err_msg('weight', name)

    if options.all:
        # Field order:
        #   file, TID, region_num, warmup, prolog, region, epilog, weight
        #
        msg.PrintMsg(all_fmt.format(name, tid, region_num, warmup, prolog,
                                    region, epilog, weight))
    else:
        msg.PrintMsg(file_fmt.format(name, weight))

sys.exit(0)
