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
 * Contributor(s): Daniel Stokes
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file DNA_shader_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_SHADER_TYPES_H__
#define __DNA_SHADER_TYPES_H__

#include "DNA_ID.h"
#include "DNA_listBase.h"

typedef struct Uniform {
	struct Uniform *next, *prev;
	char id[80];
	/* name array is 68 bytes for padding, if this changes update shader_init in shader.c */
	char name[68];

	short type;
	short size;
	void *data;
} Uniform;

typedef struct Shader {
	ID id;
	char type;
	char use;
	char location;
	char sourceenum;
	char sourcepath[240];
	int pad;
	struct Text *sourcetext;
	char *source;
	ListBase uniforms;
	struct GHash *uniform_cache;
} Shader;

typedef struct ShaderLink {
	struct ShaderLink *next, *prev;
	struct Shader *shader;
} ShaderLink;


/* type */
#define SHADER_TYPE_VERTEX		0
#define SHADER_TYPE_FRAGMENT	1

/* use */
#define SHADER_USE_MATERIAL		0
#define SHADER_USE_FILTER		1
#define	SHADER_USE_GLOBAL		3
#define SHADER_USE_GENERATED	4

/* location */
#define SHADER_LOC_BUILTIN	0
#define SHADER_LOC_INTERNAL	1
#define SHADER_LOC_EXTERNAL	2

/* uniform types */
#define SHADER_UNF_FLOAT	1
#define SHADER_UNF_VEC2		2
#define SHADER_UNF_VEC3		3
#define SHADER_UNF_VEC4		4
#define SHADER_UNF_INT		5
#define SHADER_UNF_IVEC2	6
#define SHADER_UNF_IVEC3	7
#define SHADER_UNF_IVEC4	8
#define SHADER_UNF_SAMPLER2D 10
#define SHADER_UNF_MAT2		11
#define SHADER_UNF_MAT3		12
#define SHADER_UNF_MAT4		13
#define SHADER_UNF_BUILTIN	14


#endif //__DNA_SHADER_TYPES_H__

