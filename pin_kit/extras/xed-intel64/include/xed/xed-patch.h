/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2018 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
#ifndef XED_PATCH_H
# define XED_PATCH_H
#include "xed-encoder-hl.h"

/// @name Patching decoded instructions
//@{


/// Replace a memory displacement.
/// The widths of original displacement and replacement must match.
/// @param xedd A decoded instruction.
/// @param itext The corresponding encoder output, byte array.
/// @param disp  A xed_enc_displacement_t object describing the new displacement.
/// @returns xed_bool_t  1=success, 0=failure
/// @ingroup ENCHLPATCH
XED_DLL_EXPORT xed_bool_t
xed_patch_disp(xed_decoded_inst_t* xedd,
               xed_uint8_t* itext,
               xed_enc_displacement_t disp);

/// Replace a branch displacement.
/// The widths of original displacement and replacement must match.
/// @param xedd A decoded instruction.
/// @param itext The corresponding encoder output, byte array.
/// @param disp  A xed_encoder_operand_t object describing the new displacement.
/// @returns xed_bool_t  1=success, 0=failure
/// @ingroup ENCHLPATCH
XED_DLL_EXPORT xed_bool_t
xed_patch_relbr(xed_decoded_inst_t* xedd,
                xed_uint8_t* itext,
                xed_encoder_operand_t disp);

/// Replace an imm0 immediate value.
/// The widths of original immediate and replacement must match.
/// @param xedd A decoded instruction.
/// @param itext The corresponding encoder output, byte array.
/// @param imm0  A xed_encoder_operand_t object describing the new immediate.
/// @returns xed_bool_t  1=success, 0=failure
/// @ingroup ENCHLPATCH
XED_DLL_EXPORT xed_bool_t
xed_patch_imm0(xed_decoded_inst_t* xedd,
               xed_uint8_t* itext,
               xed_encoder_operand_t imm0);

//@}
#endif
