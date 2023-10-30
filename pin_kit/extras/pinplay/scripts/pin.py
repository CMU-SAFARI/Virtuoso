# BEGIN_LEGAL 
# BSD License 
# 
# Copyright (c)2014 Intel Corporation. All rights reserved.
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

import gdb
import re
import sys
import traceback

    # debugger-shell expects all tokens to be space-delimited. Therefore,
    # expressions like [%rbp-8] must be generated in monitor commands as
    # '[ %rbp + -8 ]'
    # Also, gdb output from 'info scope' displays register names with a 
    # leading '$' while the monitor commands in debugger-shell expect '%'.
    # This means that $rbp must be changed to %rbp

    # Set to True to show exception tracebacks for debugging
showTraceback = True
    # Set to True to show 'monitor' commands passed to gdb
showCommand = True

    # Precompile regular expressions used in functions below 
addrExpr = "^Line \d+ .*is at address (0x[0-9a-fA-F]+).*but contains no code.*$"
addrExprMatcher = re.compile(addrExpr)
addrRangeExpr = "^Line \d+ .* starts at address (0x[0-9a-fA-F]+) .* ends at (0x[0-9a-fA-F]+) .*"
addrRangeMatcher = re.compile(addrRangeExpr)
symbolMatchExpr = "^Symbol ([_a-zA-Z][_0-9a-zA-Z]*) (.*)$"
symbolMatcher = re.compile(symbolMatchExpr)
varInfoExpr = ".*is .* \$([a-z]+) offset ([0-9]+)\+([\+\-]?[0-9]+), length ([0-9]+).*$"
varInfoMatcher = re.compile(varInfoExpr)
rangeExpr = ".*Range (0x[0-9a-fA-F]+)-(0x[0-9a-fA-F]+): (.*)$"
rangeMatcher = re.compile(rangeExpr)
varLengthExpr = "^, length ([0-9]+)\..*$"
varLengthMatcher = re.compile(varLengthExpr)
dwarfExprPrefix = "^\s+[0-9a-fA-F]+: "
dwarfExprPrefixMatcher = re.compile(dwarfExprPrefix)
functionNameExpr = "^Line .*starts at address .*<([_a-zA-Z:][_0-9a-zA-Z:]*).*$"
functionNameMatcher = re.compile(functionNameExpr, flags=re.DOTALL)
variableNameExpr = "[_a-zA-Z][_0-9a-zA-Z]*"
variableNameMatcher = re.compile(variableNameExpr)
staticVariableExpr = "is.*static storage at address (0x[0-9a-fA-F]+).*"
staticVariableMatcher = re.compile(staticVariableExpr)

def commonParse(command, args):
    """Common parse code for slice, forward-slice and backward-slice commands"""
    if len(args) < 5:
        raise gdb.GdbError("Invalid slice command syntax")
    try:
        index = args.index("at")
    except IndexError:
        raise gdb.GdbError("Invalid slice command syntax")
    if len(args) == index + 2:
            # Make sure user didn't specify an instruction address
        if args[index + 1][0] == '*':
            raise gdb.GdbError("ERROR: Cannot specify only a start address")

        # User specified a source file/line
        (startAddress, endAddress) = getLineAddressRange(args[index + 1])
    else:
        startAddress = args[index + 1][1:]
        endAddress = args[index + 2][1:]
    if index == 3:
        # User specified a variable name or register name
        if args[2].startswith("%"):
            variableAddress = args[2]
            variableLength = ""
        else:
            (variableAddress, variableLength) = resolveScopedVariable(
                              args[2], "*" + startAddress, "*" + endAddress)
            if variableAddress[0] == '[' or variableAddress[0] == '(':
                    # If the variable address starts with '[' or '(' then
                    # it is a base+displacement address which is not
                    # supported. Try to get its address using
                    # 'print &variable'. If the variable is not in scope
                    # that will fail with an error message.
                (variableAddress, variableLength) = resolveVariable(args[2])
    else:
        # User specified an address and length
        variableAddress = args[2]
        variableLength = args[3]
    try:
        label = "#" + args[2] + ":<" + args[index + 1] + ">"
        execute("monitor %s %s %s %s %s %s %s %s" % (command, args[1],
                startAddress, endAddress, args[0],
                variableAddress, variableLength, label))
    except gdb.error:
        reason = sys.exc_info()
        raise gdb.GdbError("ERROR: Error generating slice: %s" % reason[1])

def execute(command):
    """Invoke a monitor command"""

    if showCommand:
        print(command)
    gdb.execute(command)

def getLineAddressRange(line):
    """Get the code starting and ending address for a given line number"""

        # Try to get the start and end address for a line number by issuing an
        # 'info line' command and using regular expressions to parse the result
    result = []
    try:
        lineInfo = gdb.execute("info line " + line, False, True)
    except gdb.error:
        reason = sys.exc_info()
        raise gdb.GdbError("ERROR: Invalid line number %s: %s" %
                           (line, reason[1]))
    match = addrRangeMatcher.match(lineInfo)
    if not match:
        match = addrExprMatcher.match(lineInfo)
        if match:
                # Response contains only a single address, set both start and
                # end of code range to this address
            result.append(match.group(1))
            result.append(match.group(1))
        else:
                # Response did not contain an address range, possibly because of
                # an invalid line number specification.
            reason = sys.exc_info()
            raise gdb.GdbError("ERROR: Could not get address range for line %s: %s"
                               % (line, reason[1]))
    else:
            # Response contains a start and end address for the statement in
            # question
        result.append(match.group(1))
        result.append(match.group(2))
    return result

def getLineAddress(line):
    """Get the code starting address for a given line number"""

        # Try to get the start and end address for a line number by issuing an
        # 'info line' command and using regular expressions to parse the result
    try:
        lineInfo = gdb.execute("info line " + line, False, True)
    except gdb.error:
        reason = sys.exc_info()
        raise gdb.GdbError("ERROR: Invalid line number %s: %s" %
                           (line, reason[1]))
    match = addrRangeMatcher.match(lineInfo)
    if not match:
        match = addrExprMatcher.match(lineInfo)
        if match:
                # Response contains only a single address
            return match.group(1)
        else:
                # Response did not contain an address range, possibly because of
                # an invalid line number specification.
            reason = sys.exc_info()
            raise gdb.GdbError("ERROR: Could not get address for line %s: %s"
                               % (line, reason[1]))
    else:
            # Response contains a start and end address for the statement in
            # question. Only start address is needed
        return match.group(1)

def resolveVariable(variableName):
    """Get the address and length of the specified variable"""

    try:
            # Parse_and_eval returns an address/name pair so strip out the
            # name by trimming the result at the first ' '
        variableAddress = str(gdb.parse_and_eval("&%s" % variableName))
        variableLength = str(gdb.parse_and_eval("sizeof %s" % variableName))
        index = variableAddress.find(" ")
        if index >= 0:
            return (variableAddress[:index], variableLength)
        else:
            return (variableAddress, variableLength)
    except gdb.error:
        reason = sys.exc_info()
        raise gdb.GdbError("ERROR: Unable to get address and size of %s: %s" %
                           (variableName, reason[1]))
        # return a 2-element list containing address and length of variable
    return varInfo

def rewriteRegister(register, startAddress):
    """Rewrite the register specification for special case registers"""

        # If the register in the location expression is the stack pointer,
        # then the reference cannot be relied on since the stack pointer is
        # modified in the function prolog. Therefore the stack pointer 
        # register name must be prefixed with the corresponding function entry
        # address as a marker to debugger-shell so it can resolve the reference
        #  Note that this function expects a register name without '$' or '%' 
        # and it always inserts the '%' just before the register name
    if register == "rsp" or register == "esp":
            # Get function name by invoking 'info line' for starting address and
            # parsing the output to extract the function name
        try:
            lineInfo = gdb.execute("info line " + startAddress, False, True)
            match = functionNameMatcher.match(lineInfo)
            if match:
                functionAddress = str(gdb.parse_and_eval("&%s" %
                                      match.group(1)))
                index = functionAddress.find(" ")
                if index >= 0:
                    functionAddress = functionAddress[:index]
                return functionAddress + " : %" + register
            else:
                raise gdb.GdbError("ERROR: Unable to resolve function name for address %s " % startAddress)
        except gdb.error:
            reason = sys.exc_info()
            raise gdb.GdbError("ERROR: Unable to resolve function name for address %s: %s" %
                              (startAddress, reason[1]))
    else:
        return "%" + register

def parseDwarfExpression(variableName, variableLength, address, lines,
                         startLine):
    """Parse a DWARF expression to get location of a variable"""

    validTarget = False
    index = startLine
    while dwarfExprPrefixMatcher.match(lines[index]):
        tokens = lines[index].split()
        tokenLen = len(tokens[1])
        if tokens[1] == "DW_OP_addr":
                # Address of variable is as specified by the DWARF op operand
            validTarget = True
            target = tokens[2]
        elif tokenLen > 9 and tokens[1][0:10] == "DW_OP_breg":
                # Value of variable is specified register + offset
            validTarget = True
            target = "( %" + tokens[3][2:5] + " + " + tokens[2] + " )"
        elif tokens[1] == "DW_OP_bregx":
                # Value of variable is specified register + offset
                # remaining tokens are probably
                # register %s [$%s] offset %s
                # see http://www.cygwin.com/ml/gdb-patches/2010-06/msg00011.html
            validTarget = True
            target = "( %" + tokens[4][2:5] + " + " + tokens[6] + " )"
        elif tokenLen > 8 and tokens[1][0:9] == "DW_OP_reg":
                # The variable value is in the specified register
            validTarget = True
            target = "%" + tokens[2][2:5]
        elif tokens[1] == "DW_OP_regx":
                # The variable value is in the specified register
                # remaining tokens are probably
                # %s [$%s]
                # see http://www.cygwin.com/ml/gdb-patches/2010-06/msg00011.html
            validTarget = True
            target = "%" + tokens[1][2:5]
        elif tokens[1] == "DW_OP_fbreg":
            validTarget = True
            try:
                lineInfo = gdb.execute("info line " + address, False, True)
                match = functionNameMatcher.match(lineInfo)
                if match:
                    functionAddress = str(gdb.parse_and_eval("&%s" %
                                          match.group(1)))
                    wordIndex = functionAddress.find(" ")
                    if wordIndex >= 0:
                        functionAddress = functionAddress[:wordIndex]
                else:
                    raise gdb.GdbError("ERROR: Unable to resolve function name for address %s " % startAddress)
            except gdb.error:
                reason = sys.exc_info()
                raise gdb.GdbError("ERROR: Unable to resolve function name for address %s: %s" %
                              (startAddress, reason[1]))
            target = "[ " + functionAddress + " : " + "%rsp-c + " + \
                     tokens[2] + " ]"
        elif tokens[1] != "DW_OP_stack_value":
                # The DWARF opcode does not represent a 
                # specific register or memory address, so
                # it invalidates the target in the case where
                # the value of the register or memory
                # location is important (pin break ...)
            validTarget = False
        index = index + 1
        
        # If target is a register then no variable length is returned
    if validTarget:
        if target[0] == "%":
            return (target, "")
        else:
            return (target, variableLength)
    else:
        raise gdb.GdbError("ERROR: Unable to resolve variable %s, representation in DWARF debug info too complex" %
                           variableName)

def resolveScopedVariable(variableName, rangeStart, rangeEnd):
    """Get address of a variable which may be local to a function or block"""

       #
       # See http://dwarfstd.org/doc/DWARF4.pdf for an explanation of DWARF
       # expressions, particularly section 2.5
       #
    try:
            # First try to find the variable in the list returned by
            # 'info scope', which is a list of local variables for the scope
            # Note that rangeStart and rangeEnd are hex addresses prefixed with
            # '*' (since the user may have entered an address with starting '*')
        info = str(gdb.execute("info scope %s" % rangeStart, False, True))
        start = int(rangeStart[1:], 16)
        end = int(rangeEnd[1:], 16)
        lines = str.split(info, "\n")
            # Try to find the variable in the set of local variables. If it's
            # not found then try to find it as a global variable
        localVariableFound = False
        index = 0
        while index < len(lines) and not localVariableFound:
            match = symbolMatcher.match(lines[index])
            if match and match.group(1) == variableName:
                symbolType = match.group(2)
                localVariableFound = True
                break
            index = index + 1
        if not localVariableFound:
            return resolveVariable(variableName)

            # Find the end index for the set of lines describing this symbol
            # Any further processing occurs only for lines between index and 
            # endIndex - 1
        endIndex = index + 1
        while endIndex < len(lines):
            if lines[endIndex].startswith("Symbol "):
                break
            endIndex = endIndex + 1

            # Check if the variable has been optimized out of the program
        if symbolType == "is optimized out.":
            raise gdb.GdbError("ERROR: Symbol %s has been optimized out." %
                               variableName)

            # Try to match to line like 
            # 'is a variable at frame base reg $rbp offset 16+-80, length 4.'
        match = varInfoMatcher.match(symbolType)
        if match:
            offset = int(match.group(2)) + int(match.group(3))
            targetRegister = rewriteRegister(match.group(1), rangeStart)
            target = "[ " + targetRegister + " + " + str(offset) + " ]"
            return (target, match.group(4))

            # Find the variable length by searching for ', length [0-9]+'
        lengthIndex = index + 1
        variableLength = ""
        while lengthIndex < endIndex:
            match = varLengthMatcher.match(lines[lengthIndex])
            if match:
                variableLength = match.group(1)
                break
            lengthIndex = lengthIndex + 1

        if (symbolType.startswith("the constant") or 
                symbolType.startswith("is constant bytes")):
            raise gdb.GdbError(
                  "ERROR: Variable %s is a constant at specified location" %
                  variableName)

            # Check if the variable has multiple locations depending on
            # instruction address
        if symbolType.startswith("is multi-location:"):
            rangeIndex = index + 1
            while rangeIndex < endIndex:
                match = rangeMatcher.match(lines[rangeIndex])
                if match:
                    startRange = int(match.group(1), 16)
                    endRange = int(match.group(2), 16)
                    if startRange <= start and endRange > end:
                        rangeType = match.group(3)
                        break
                rangeIndex = rangeIndex + 1
            if rangeIndex >= endIndex:
                raise gdb.GdbError(
                      "ERROR: Variable %s is not active at specified location" %
                      variableName)
            if rangeType.startswith("the constant "):
                raise gdb.GdbError(
                      "ERROR: Variable %s is a constant at specified location" %
                      variableName)
            if rangeType.startswith("a variable in"):
                regIndex = rangeType.find("$")
                target = "%" + rangeType[regIndex + 1:]
                return (target, "")
            if rangeType.startswith("a complex DWARF expression:"):
                return parseDwarfExpression(variableName, variableLength,
                                            rangeStart, lines, rangeIndex + 1)
            raise gdb.GdbError("ERROR: Unable to resolve variable %s, unrecognized construct in DWARF debug informtion" %
                               variableName)
        match = staticVariableMatcher.match(symbolType)
        if match:
            return (match.group(1), variableLength)
        if symbolType == "is a complex DWARF expression:":
            return parseDwarfExpression(variableName, variableLength,
                                        rangeStart, lines, index + 1)
    except gdb.error:
        reason = sys.exc_info()
        raise gdb.GdbError("ERROR: Error getting variable address: %s" % 
                           reason[1])

def substituteLineAddress(args, index):
    """Parse command arguments to determine source line address range"""

    if (str(args[index])).startswith("*"):
            # User specified location as an address (*0x12345678) so
            # just use what the user specified
        return (args[index], args[index])
    else:
            # Otherwise location is a source line reference
        range = getLineAddressRange(args[index])
        return("*" + range[0], "*" + range[1])

def substituteVariable(args, index, scopeStart, scopeEnd):
    """Parse comand arguments to get variable address and length"""

    if args[index].startswith("%"):
        return (args[index], "")
    else:
        match = variableNameMatcher.match(args[index])
        if match:
            return resolveScopedVariable(args[index], scopeStart, scopeEnd)
        elif args[index].startswith("*"):
            return  (args[index], args[index + 1])
        else:
            raise gdb.GdbError("ERROR: %s is not a variable, register or memory address" %
                               args[index])

def handleTraceAt(args):
    """Handle 'pin trace ... at ...' command variants"""

    try:
            # Find the 'at' keyword which separates variable and address
        index = args.index("at")
        (start, end) = substituteLineAddress(args, index + 1)
        (variableAddress, variableLength) = substituteVariable(args, 0,
                                            start, end)

                # Remove leading '*' from instruction address before issuing
                # monitor command
        label = "#" + args[0] + ":<" + args[index + 1] + ">"
        execute("monitor trace %s %s at %s %s" %
                (variableAddress, variableLength, start[1:], label))
    except IndexError:
        if showTraceback:
            reason = sys.exc_info()
            print(reason[1])
            traceback.print_tb(reason[2])
        raise gdb.GdbError("ERROR: Invalid trace command syntax")
    except gdb.error:
        reason = sys.exc_info()
        raise gdb.GdbError("ERROR: Error handling trace command: %s" %
                           reason[1])

def handleBreakAt(args):
    """Handle 'pin break <pc> if ...' break command variants"""
    try:
        (start, end) = substituteLineAddress(args, 0)
        (variableAddress, variableLength) = substituteVariable(args, 2, start,
                                                               end)
        index = args.index("==")
            # Remove leading '*' from instruction address before issuing
            # command
        label = "#" + args[2] + ":<" + args[0] + ">"
        execute("monitor break at %s if %s %s == %s %s" % (start[1:],
                variableAddress, variableLength, args[index + 1], label))
    except ValueError:
        if showTraceback:
            reason = sys.exc_info()
            print(reason[1])
            traceback.print_tb(reason[2])
        raise gdb.GdbError("ERROR: Invalid break command syntax")
    except IndexError:
        if showTraceback:
            reason = sys.exc_info()
            print(reason[1])
            traceback.print_tb(reason[2])
        raise gdb.GdbError("ERROR: Invalid break command syntax")
    except gdb.error:
        reason = sys.exc_info()
        raise gdb.GdbError("ERROR: Error handling break command: %s" %
                           reason[1])

class SliceCommand(gdb.Command):
    """Create a program slice

    pin slice <instance> <thread> 
               { <variableName> | %<register> | <address> <length> } at
               { <sourceLocation> | *<startAddress> *<endAddress> }

          instance: DART log instance
          thread: Target thread id
          variableName: Name of the variable to generate slice for
          register: Name of register to generate slice for
          address: Address of the variable to generate slice for
          length: Length of variable
          sourceLocation: file:lineNumber or line number to generate slice
          startAddress: Starting code address to generate slice
          endAddress: Ending code address to generate slice"""

    def __init__(self):
        """Initialize the SliceCommand object"""
        super(SliceCommand,
              self).__init__(name="pin slice",
                             command_class = gdb.COMMAND_DATA,
                             prefix = False)

    def invoke(self, argList, from_tty):
        """Top level handler for the slice command"""

            # Don't repeat the command if the user presses enter. That just 
            # generates duplicate slice files
        super(SliceCommand, self).dont_repeat()
        args = gdb.string_to_argv(argList)
        commonParse("slice", args)

class ForwardSliceCommand(gdb.Command):
    """Create a program forward slice

    pin forward-slice <instance> <thread> 
               { <variableName> | %<register> | <address> <length> } at
               { <sourceLocation> | *<startAddress> *<endAddress> }

          instance: DART log instance
          thread: Target thread id
          variableName: Name of the variable to generate slice for
          register: Name of register to generate slice for
          address: Address of the variable to generate slice for
          length: Length of variable
          sourceLocation: file:lineNumber or line number to generate slice
          startAddress: Starting code address to generate slice
          endAddress: Ending code address to generate slice"""

    def __init__(self):
        """Initialize the ForwardSliceCommand object"""
        super(ForwardSliceCommand,
              self).__init__(name="pin forward-slice",
                             command_class = gdb.COMMAND_DATA,
                             prefix = False)

    def invoke(self, argList, from_tty):
        """Top level handler for the forward slice command"""

            # Don't repeat the command if the user presses enter. That just 
            # generates duplicate slice files
        super(ForwardSliceCommand, self).dont_repeat()
        args = gdb.string_to_argv(argList)
        commonParse("forward-slice", args)

class BackwardSliceCommand(gdb.Command):
    """Create a program backward slice

    pin backward-slice <instance> <thread> 
               { <variableName> | %<register> | <address> <length> } at
               { <sourceLocation> | *<startAddress> *<endAddress> }

          instance: DART log instance
          thread: Target thread id
          variableName: Name of the variable to generate slice for
          register: Name of register to generate slice for
          address: Address of the variable to generate slice for
          length: Length of variable
          sourceLocation: file:lineNumber or line number to generate slice
          startAddress: Starting code address to generate slice
          endAddress: Ending code address to generate slice"""

    def __init__(self):
        """Initialize the SliceCommand object"""
        super(BackwardSliceCommand,
              self).__init__(name="pin backward-slice",
                             command_class = gdb.COMMAND_DATA,
                             prefix = False)

    def invoke(self, argList, from_tty):
        """Top level handler for the slice command"""

            # Don't repeat the command if the user presses enter. That just 
            # generates duplicate slice files
        super(BackwardSliceCommand, self).dont_repeat()
        args = gdb.string_to_argv(argList)
        commonParse("backward-slice", args)

class PruneCommand(gdb.Command):
    """Prune a program slice, removing variable references

    pin prune <slice_id> { <variable> | %<register> | <address> <length> }

          slice_id: slice id number
          variable: Name of variable to prune from slice
          register: Name of register to prune from slice, %rax, %rbx, etc
          address: Address of the variable to prune from the slice
          length: Length of the variable to prune from the slice"""

    def __init__(self):
        """Initialize the PruneCommand object"""
        super(PruneCommand,
              self).__init__(name="pin prune",
                             command_class = gdb.COMMAND_DATA,
                             prefix = False)

    def invoke(self, argList, from_tty):
        """Top level handler for the prune command"""

            # Don't repeat the command if the user presses enter. There's
            # probably no harm done other than wasting a bit of time if the
            # command is repeated
        super(PruneCommand, self).dont_repeat()
        args = gdb.string_to_argv(argList)
        if len(args) == 2:
                # If command has 2 arguments then 2nd argument is variable name
                # or register.
            if str(args[1]).startswith("%"):
                try:
                    execute("monitor prune %s %s %s" % (args[0], args[1]),
                            "#" + args[1])
                except gdb.error:
                    reason = sys.exc_info()
                    raise gdb.GdbError("ERROR: Error pruning slice: %s" %
                                       reason[1])
            else:
                (variableAddress, variableLength) = resolveVariable(
                                  args[1])
                try:
                    execute("monitor prune %s %s %s" % (args[0],
                            variableAddress, variableLength)) 
                except gdb.error:
                    reason = sys.exc_info()
                    raise gdb.GdbError("ERROR: Error pruning slice: %s" %
                                       reason[1])
        elif len(args) == 3:
                # 2nd and 3rd arguments are address and length
            try:
                execute("monitor prune %s %s %s" % (args[0], args[1],
                        args[2]))
            except gdb.error:
                reason = sys.exc_info()
                raise gdb.GdbError("ERROR: Error pruning slice: %s" %
                                   reason[1])
        else:
            raise gdb.GdbError("ERROR: Invalid prune command syntax")
class RecordCommand(gdb.Command):
    """Start/stop recording a Pin program log

    pin record on | off

    on: Start recording a log
    off: Stop recording a log"""

    def __init__(self):
        """Initialize the RecordCommand object"""

        super(RecordCommand,
              self).__init__(name="pin record",
                             command_class = gdb.COMMAND_DATA,
                             prefix = False)

    def invoke(self, option, from_tty):
        """Top level handler for the record command"""

            # Don't repeat the command if the user presses enter. That just 
            # causes an error
        super(RecordCommand, self).dont_repeat()
        if option == "on" or option == "off":
            try:
                execute("monitor record %s" % option)
            except gdb.error:
                reason = sys.exc_info()
                raise gdb.GdbError("ERROR: Error handling log command: %s" %
                                   reason[1])
        else:
            raise gdb.GdbError("ERROR: Invalid log command syntax")

class TraceCommand(gdb.Command):

    """Monitor variable at a specified instruction address
    pin trace { <variable> | %<register> | <variableAddress> <length> } at
               { <sourceLine> | *<instructionAddress> }
    pin trace %<register> if load from 
               { <variable> | %<register> | <variableAddress> }
    pin trace %<register> if store to 
               { <variable> | %<register> | <variableAddress> }
    pin trace %<register> before load from 
               { <variable> | %<register> | <variableAddress> } == <value>
    pin trace %<register> after store to
               { <variable> | %<register> | <variableAddress> } == <value>
    pin trace enable <id>
    pin trace disable <id>
    pin trace print to <file>
    pin trace clear

        variable: Name of variable to be traced
        register: Name of the register to be traced, %rax, %rbx, etc
        variableAddress: Address of variable to trace
        length: length of variable to trace
        sourceLine: line number or filename:linenumber pair to trace
        instructionAddress: Address of instruction to trace at
        value: Value to match to register value
        id: tracepoint identifier number
        file: Pathname for file for trace output"""

    def __init__(self):
        """Initialize the TraceCommand object"""
        super(TraceCommand,
              self).__init__(name="pin trace",
                             command_class = gdb.COMMAND_TRACEPOINTS,
                             prefix = False)

    def invoke(self, argList, from_tty):
        """Top level handler for the trace command"""

            # Don't repeat the command if the user presses enter. 
        super(TraceCommand, self).dont_repeat()
        args = gdb.string_to_argv(argList)
        argc = len(args)
        if argc > 1 and args[1] == "at":
            handleTraceAt(args)
        elif argc > 2 and args[2] == "at":
            handleTraceAt(args)
        else:
            command = "monitor trace"
            for s in args:
                command = command + " " + s
            try:
                execute(command)
            except gdb.error:
                reason = sys.exc_info()
                raise gdb.GdbError("ERROR: Error handling trace command: %s" %
                                   reason[1])
        
class BreakCommand(gdb.Command):
    """Set a Pin conditional breakpoint

    pin break { <sourceLine> | *<instructionAddress> } if
         { <variable> | %<register> | <variableAddress> <length> } == <value>
    pin break if load from { <variable> | %<register> | <variableAddress> }
    pin break if store to { <variable> | %<register> | <variableAddress> }
    pin break before load from 
         { <variable> | %<register> | <variableAddress> } == <value>
    pin break after store to
         { <variable> | %<register> | <variableAddress> } == <value>
    pin break if icount <count>
    pin break if mcount <count>
    pin break if jump to { <sourceLine> | *<instructionAddress> }


        variable: Name of variable to be monitored
        register: Name of the register to be monitored, %rax, %rbx, etc
        variableAddress: Address of variable to monitor
        length: length of variable to monitor
        sourceLine: line number or filename:linenumber pair for breakpoint
        instructionAddress: Address of instruction to set breakpoint
        value: Value to match when testing breakpoint condition
        count: Event count to trigger breakpoint"""

    def __init__(self):
        """Initialize the BreakCommand object"""
        super(BreakCommand,
              self).__init__(name="pin break",
                             command_class = gdb.COMMAND_BREAKPOINTS,
                             prefix = False)

    def invoke(self, argList, from_tty):
        """Top level handler for the break command"""

            # Don't repeat the command if the user presses enter. 
        super(BreakCommand, self).dont_repeat()
        args = gdb.string_to_argv(argList)
        argc = len(args)
        if argc > 1 and args[1] == "if":
            handleBreakAt(args)
        else:
            command = "monitor break"
            for s in args:
                command = command + " " + s
            try:
                execute(command)
            except gdb.error:
                reason = sys.exc_info()
                raise gdb.GdbError("ERROR: Error handling break command: %s" %
                                   reason[1])

class PinCommand(gdb.Command):
    """Miscellaneous Pin commands and command prefix for all Pin subcommands

    pin list breakpoints
    pin list tracepoints
    pin delete breakpoint <id>
    pin delete tracepoint <id>
    pin <debugger-shell command>

    id: breakpoint or tracepoint identifier number
    debugger-shell command: Any command accepted by debugger-shell"""

    def __init__(self):
        """Initialize the PinCommand object"""
        super(PinCommand,
	      self).__init__(name="pin",
			     command_class = gdb.COMMAND_DATA,
			     prefix = True)

    def invoke(self, argList, from_tty):
        """Top level handler for the pin command"""
            # Don't repeat the command if the user presses enter.
        super(PinCommand, self).dont_repeat()
        args = gdb.string_to_argv(argList)
        if len(args) == 0:
            raise gdb.GdbError("ERROR: Missing command arguments")
        command = "monitor " + argList
        try:
            execute(command)
        except gdb.error:
            reason = sys.exc_info()
            raise gdb.GdbError("ERROR: Error handling pin command: %s" %
                               reason[1])

PinCommand()
TraceCommand()
BreakCommand()
PruneCommand()
SliceCommand()
ForwardSliceCommand()
BackwardSliceCommand()
