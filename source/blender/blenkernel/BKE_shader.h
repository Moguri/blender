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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BKE_SHADER_H__
#define __BKE_SHADER_H__

/** \file BKE_shader.h
 *  \ingroup bke
 *  \brief General operations, lookup, etc. for shaders.
 */
#ifdef __cplusplus
extern "C" {
#endif

#include "DNA_shader_types.h"

void init_shader(struct Shader *sh);
void BKE_shader_free(Shader *sh);
void BKE_shader_lib_free(Shader *sh);

struct Shader *BKE_shader_add(const char *name);
struct Shader *BKE_shader_copy(struct Shader *sh);
struct Shader *BKE_shader_empty();

void BKE_shader_source_merge(Shader *dst, const Shader *src);
void BKE_shader_source_merge_ch(Shader *dst, const char *src);

void BKE_shader_read_source(struct Shader *sh, struct Main *main);
#ifdef __cplusplus
}
#endif

#endif
