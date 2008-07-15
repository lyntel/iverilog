/*
 *  VHDL code generation for LPM devices.
 *
 *  Copyright (C) 2008  Nick Gasson (nick@nickg.me.uk)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "vhdl_target.h"

#include <iostream>
#include <cassert>

/*
 * Return the base of a part select.
 */
static vhdl_expr *part_select_base(vhdl_scope *scope, ivl_lpm_t lpm)
{
   vhdl_expr *off;
   ivl_nexus_t base = ivl_lpm_data(lpm, 1);
   if (base != NULL)
      off = nexus_to_var_ref(scope, base);
   else
      off = new vhdl_const_int(ivl_lpm_base(lpm));

   // Array indexes must be integers
   vhdl_type integer(VHDL_TYPE_INTEGER);
   return off->cast(&integer);
}

vhdl_var_ref *lpm_output(vhdl_scope *scope, ivl_lpm_t lpm)
{
   vhdl_var_ref *out = nexus_to_var_ref(scope, ivl_lpm_q(lpm, 0));
   if (NULL == out) {
      vhdl_type *type =
         vhdl_type::type_for(ivl_lpm_width(lpm),
                             ivl_lpm_signed(lpm) != 0);
      string name("LPM");
      name += ivl_lpm_basename(lpm);
      name += "_Out";

      if (!scope->have_declared(name)) {
         scope->add_decl
            (new vhdl_signal_decl(name.c_str(), new vhdl_type(*type)));
      }
      
      out = new vhdl_var_ref(name.c_str(), type);
   }

   if (ivl_lpm_type(lpm) == IVL_LPM_PART_PV) {
      vhdl_expr *off = part_select_base(scope, lpm);
      assert(off);

      out->set_slice(off, ivl_lpm_width(lpm) - 1);
   }
   
   return out;
}


static vhdl_expr *concat_lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm)
{
   vhdl_type *result_type =
      vhdl_type::type_for(ivl_lpm_width(lpm), ivl_lpm_signed(lpm) != 0);
   vhdl_binop_expr *expr =
      new vhdl_binop_expr(VHDL_BINOP_CONCAT, result_type);
 
   for (int i = ivl_lpm_selects(lpm) - 1; i >= 0; i--) {
      vhdl_expr *e = nexus_to_var_ref(scope, ivl_lpm_data(lpm, i));
      if (NULL == e)
         return NULL;

      expr->add_expr(e);
   }

   return expr;
}

static vhdl_expr *binop_lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm, vhdl_binop_t op)
{
   vhdl_type *result_type =
      vhdl_type::type_for(ivl_lpm_width(lpm), ivl_lpm_signed(lpm) != 0);
   vhdl_binop_expr *expr = new vhdl_binop_expr(op, result_type);
 
   for (int i = 0; i < 2; i++) {
      vhdl_expr *e = nexus_to_var_ref(scope, ivl_lpm_data(lpm, i));
      if (NULL == e)
         return NULL;

      expr->add_expr(e);
   }
   
   if (op == VHDL_BINOP_MULT) {
      // Need to resize the output to the desired size,
      // as this does not happen automatically in VHDL
      
      unsigned out_width = ivl_lpm_width(lpm);
      vhdl_fcall *resize =
         new vhdl_fcall("Resize", vhdl_type::nsigned(out_width));
      resize->add_expr(expr);
      resize->add_expr(new vhdl_const_int(out_width));
      
      return resize;
   }
   else
      return expr;
}

static vhdl_expr *part_select_vp_lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm)
{
   vhdl_var_ref *selfrom = nexus_to_var_ref(scope, ivl_lpm_data(lpm, 0));
   if (NULL == selfrom)
      return NULL;

   vhdl_expr *off = part_select_base(scope, lpm);;
   if (NULL == off)
      return NULL;

   selfrom->set_slice(off, ivl_lpm_width(lpm) - 1);
   return selfrom;
}


static vhdl_expr *part_select_pv_lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm)
{   
   return nexus_to_var_ref(scope, ivl_lpm_data(lpm, 0));
}

static vhdl_expr *ufunc_lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm)
{
   vhdl_fcall *fcall = new vhdl_fcall(ivl_lpm_basename(lpm), NULL);

   for (unsigned i = 0; i < ivl_lpm_size(lpm); i++) {
      vhdl_var_ref *ref = nexus_to_var_ref(scope, ivl_lpm_data(lpm, i));
      if (NULL == ref)
         return NULL;

      fcall->add_expr(ref);
   }

   return fcall;
}

static vhdl_expr *reduction_lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm,
                                     const char *rfunc, bool invert)
{
   vhdl_fcall *fcall = new vhdl_fcall(rfunc, vhdl_type::std_logic());

   vhdl_var_ref *ref = nexus_to_var_ref(scope, ivl_lpm_data(lpm, 0));
   if (NULL == ref)
      return NULL;
   
   fcall->add_expr(ref);

   if (invert)
      return new vhdl_unaryop_expr
         (VHDL_UNARYOP_NOT, fcall, vhdl_type::std_logic());
   else
      return fcall;
}

static vhdl_expr *sign_extend_lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm)
{
   vhdl_expr *ref = nexus_to_var_ref(scope, ivl_lpm_data(lpm, 0));
   if (ref)
      return ref->resize(ivl_lpm_width(lpm));
   else
      return NULL;
}

static vhdl_expr *array_lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm)
{
   ivl_signal_t array = ivl_lpm_array(lpm);
   if (!seen_signal_before(array))
      return NULL;
   
   const char *renamed = get_renamed_signal(array).c_str();

   vhdl_decl *adecl = scope->get_decl(renamed);
   assert(adecl);
   
   vhdl_type *atype = new vhdl_type(*adecl->get_type());

   vhdl_expr *select = nexus_to_var_ref(scope, ivl_lpm_select(lpm));
   if (NULL == select)
      return NULL;
   
   vhdl_var_ref *ref = new vhdl_var_ref(renamed, atype);
   ref->set_slice(select);
   
   return ref;
}

static vhdl_expr *lpm_to_expr(vhdl_scope *scope, ivl_lpm_t lpm)
{
   switch (ivl_lpm_type(lpm)) {
   case IVL_LPM_ADD:
      return binop_lpm_to_expr(scope, lpm, VHDL_BINOP_ADD);
   case IVL_LPM_SUB:
      return binop_lpm_to_expr(scope, lpm, VHDL_BINOP_SUB);
   case IVL_LPM_MULT:
      return binop_lpm_to_expr(scope, lpm, VHDL_BINOP_MULT);
   case IVL_LPM_CONCAT:
      return concat_lpm_to_expr(scope, lpm);
   case IVL_LPM_PART_VP:
      return part_select_vp_lpm_to_expr(scope, lpm);
   case IVL_LPM_PART_PV:
      return part_select_pv_lpm_to_expr(scope, lpm);
   case IVL_LPM_UFUNC:
      return ufunc_lpm_to_expr(scope, lpm);
   case IVL_LPM_RE_AND:
      return reduction_lpm_to_expr(scope, lpm, "Reduce_AND", false);
   case IVL_LPM_RE_NAND:
      return reduction_lpm_to_expr(scope, lpm, "Reduce_AND", true);
   case IVL_LPM_RE_NOR:
      return reduction_lpm_to_expr(scope, lpm, "Reduce_OR", true);
   case IVL_LPM_RE_OR:
      return reduction_lpm_to_expr(scope, lpm, "Reduce_OR", false);
   case IVL_LPM_RE_XOR:
      return reduction_lpm_to_expr(scope, lpm, "Reduce_XOR", false);
   case IVL_LPM_RE_XNOR:
      return reduction_lpm_to_expr(scope, lpm, "Reduce_XNOR", false);
   case IVL_LPM_SIGN_EXT:
      return sign_extend_lpm_to_expr(scope, lpm);
   case IVL_LPM_ARRAY:
      return array_lpm_to_expr(scope, lpm);
   default:
      error("Unsupported LPM type: %d", ivl_lpm_type(lpm));
      return NULL;
   }
}

int draw_lpm(vhdl_arch *arch, ivl_lpm_t lpm)
{
   vhdl_expr *f = lpm_to_expr(arch->get_scope(), lpm);
   if (NULL == f)
      return 0;
   
   vhdl_var_ref *out = lpm_output(arch->get_scope(), lpm);
   arch->add_stmt(new vhdl_cassign_stmt(out, f));
   
   return 0;
}

