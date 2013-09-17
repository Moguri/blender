/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Blender Foundation (2008).
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __RNA_SHADER_UTILS_H__
#define __RNA_SHADER_UTILS_H__

/** \file RNA_shader_utils.h
 *  \ingroup RNA
 *  Utility functions for creating shader related functions. */

#include "DNA_listBase.h"
#include "RNA_types.h"

#ifdef __cplusplus
extern "C" {
#endif

PointerRNA rna_Shader_active_get(PointerRNA *ptr, ListBase *list, int index);
void rna_Shader_active_set(PointerRNA value, ListBase *list, int index);
int rna_Shader_active_index_get(int index);
void rna_Shader_active_index_set(int value, int *index);
void rna_Shader_active_index_range(int *min, int *max, ListBase *list);
int rna_Shader_id_poll(PointerRNA *ptr, PointerRNA value, char use);

#ifdef __cplusplus
}
#endif

#endif /* __RNA_SHADER_UTILS_H__ */

