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

/** \file blender/blenkernel/intern/shader.c
 *  \ingroup bke
 */

#include <stdlib.h>
#include <string.h>

#include "DNA_shader_types.h"

#include "MEM_guardedalloc.h"

#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_shader.h"
#include "BKE_text.h"


#include "BLI_utildefines.h"

#include "BLI_dynstr.h"
#include "BLI_fileops.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

static char *light_code =
"#ifndef MAX_LIGHTS\n"
"#define MAX_LIGHTS 4\n"
"#endif\n"
"struct Light {\n"
"	int type;\n"
"	int falloff;\n"
"	float dist;\n"
"	vec3 color;\n"
"	vec3 position;\n"
"	mat3 orientation;\n"
"};\n"
"uniform int bgl_lightcount;\n"
"uniform Light bgl_lights[MAX_LIGHTS];\n";

void init_shader(Shader *sh)
{
	unsigned int i;

	sh->uniforms.first = NULL;
	sh->uniforms.last = NULL;

	sh->uniform_cache = BLI_ghash_str_new("Uniform Cache");
}

void BKE_shader_lib_free(Shader *sh)
{
	Uniform *uni;
	unsigned int i;

	for (i = 0; i < SHADER_SRC_MAX; ++i) {
		if (sh->sources[i].source)
			MEM_freeN(sh->sources[i].source);
	}
	
	for (uni = sh->uniforms.first; uni; uni = uni->next) {
		if (uni->data && uni->size > 1)
			MEM_freeN(uni->data);
	}

	BLI_freelistN(&sh->uniforms);
	BLI_ghash_free(sh->uniform_cache, NULL, (GHashValFreeFP)MEM_freeN);
}

void BKE_shader_free(Shader *sh)
{
	BKE_shader_lib_free(sh);
	MEM_freeN(sh);
}

struct Shader *BKE_shader_add(const char *name)
{
	Shader *sh;
	sh = BKE_libblock_alloc(G.main, ID_SH, name);
	init_shader(sh);
	return sh;
}

struct Shader *BKE_shader_copy(Shader *sh)
{
	Shader *shn;
	shn = BKE_libblock_copy(&sh->id);
	return shn;
}

void gather_uniforms(Shader *sh);

int starts_with(const char *str, const char *start)
{
	int len, result;
	char *tmp;
	len = strlen(start);
	tmp = MEM_callocN(sizeof(char)*(len+1), "starts_with buffer");
	strncpy(tmp, str, len);
	result = strcmp(tmp, start);
	MEM_freeN(tmp);
	return result == 0;
}

Uniform *uniform_init(char *type, char *name)
{
	Uniform *uni = MEM_mallocN(sizeof(Uniform), "Custom shader uniform");
	int data_size;
	BLI_strncpy(uni->name, name, 68);
	BLI_strncpy(uni->id, name, 68);
	strncat(uni->id, type, 80);
	uni->size = 1;

	if (strcmp(type, "float") == 0)	{
		uni->type = SHADER_UNF_FLOAT;
	}
	else if (strcmp(type, "int") == 0) {
		uni->type = SHADER_UNF_INT;
	}
	/*
	else if (strcmp(type, "bool") == 0) {
		uni->type = SHADER_UNF_BOOL;
	}
	*/
	else if (starts_with(type, "vec")) {
		uni->type = SHADER_UNF_VEC2;
		uni->size = atoi(type+3);
		uni->type += uni->size - 2;
		data_size = sizeof(float);
	}
	else if (starts_with(type, "ivec")) {
		uni->type = SHADER_UNF_IVEC2;
		uni->size = atoi(type+4);
		uni->type += uni->size -2;
		data_size = sizeof(float);
	}
	else if (strcmp(type, "sampler2D") == 0) {
		uni->type = SHADER_UNF_SAMPLER2D;
	}
	else if (starts_with(type, "sampler2D")) {
		uni->type = SHADER_UNF_SAMPLER2D;
	}
	else if (starts_with(type, "mat")) {
		uni->type = SHADER_UNF_MAT2;
		uni->size = atoi(type+3);
		uni->type += uni->size -2;
		uni->size *= uni->size;
		data_size = sizeof(float);
	}
	else if (
		strcmp(type, "Light") == 0 ||
		strcmp(type, "Material") == 0 ||
		strcmp(type, "TextureInfo") == 0
		) {
			uni->type = SHADER_UNF_BUILTIN;
	}
	else {
		printf("Data type %s is not supported\n", type);
		MEM_freeN(uni);
		return NULL;
	}


	/* Clamp sizes in case there is an error in the shader */
	//if (uni->size < 1)
	//	uni->size = 1;
	//else if (uni->size > 4)
	//	uni->size = 4;

	/* Ready data */
	if (uni->size > 1) {		
		uni->data = MEM_callocN(data_size * uni->size, "Custom shader uniform data");
	}
	else
		uni->data = 0;

	uni->prev = uni->next = NULL;

	return uni;
}

char *file_to_buf(char *filename, Main *main)
{	
	FILE *fp;
	int len;
	char *shader = NULL;
	char path[280] = {0};

	if (strcmp(filename, "") == 0)
		return shader;

	strcpy(path, filename);

	if (filename[0] == '/' && filename[1] == '/')
	{
		char base[280] = "";
		strcpy(base, main->name);
		BLI_clean(base);
		BLI_parent_dir(base);
		BLI_path_abs(path, base);
	}
	fp = BLI_fopen(path, "rb");

	if (fp) {
		// Find the size
		fseek(fp, 0, SEEK_END);
		len = ftell(fp);
		rewind(fp);

		// Initialize the string and read the files
		shader = MEM_callocN(len+1, "shader_text");
		fread(shader, 1, len, fp);

		// Close the file
		fclose(fp);
	}
	else {
		printf("Unable to open shader %s\n", path);
	}

	return shader;
}

char* extract_token(char **ptr)
{
	char *token;
	int token_len = 0;
	
	while (**ptr != '\0' && (**ptr == ' ' || **ptr == '\t' || **ptr == '\n'))
		(*ptr)++;

	while (**ptr != ' ' && **ptr != '\t' && **ptr != '\n' && **ptr != ';' && **ptr != '\0') {
		token_len++;
		(*ptr)++;
	}

	token = MEM_callocN(sizeof(char)*(token_len+1), "Shader token");
	strncpy(token, *ptr-token_len, token_len);
	return token;
}

void extract_default(const char *src,  Uniform *uni)
{
	void *var = NULL;
	int error = 0;

	/* Ignore whitespace */
	while (*src != '\0' && (*src == ' ' || *src == '\t' || *src == '\n'))
		src++;

	if (uni->type == SHADER_UNF_FLOAT) {
		if (sscanf(src, "%f", &uni->data) != 1)
			error = 1;
	}
	else if (uni->type == SHADER_UNF_VEC2) {
		if (sscanf(src, "vec2(%f, %f)", uni->data, (float*)uni->data+1) != 2)
			error = 1;
	}
	else if (uni->type == SHADER_UNF_VEC3) {
		if (sscanf(src, "vec3(%f, %f, %f)", uni->data, (float*)uni->data+1, (float*)uni->data+2) != 3)
			error = 1;
	}
	else if (uni->type == SHADER_UNF_INT) {
		if (sscanf(src, "%d", &uni->data) != 1)
			error = 1;
	}
	else if (uni->type == SHADER_UNF_IVEC2) {
		if (sscanf(src, "vec2(%d, %d)", uni->data, (int*)uni->data+1) != 2)
			error = 1;
	}
	else if (uni->type == SHADER_UNF_IVEC3) {
		if (sscanf(src, "vec3(%d, %d, %d)", uni->data, (int*)uni->data+1, (int*)uni->data+2) != 3)
			error = 1;
	}

	if (error)
	{
		printf("Unable to parse default value for uniform %s\n", uni->name);
	}
}

void gather_uniforms(Shader *sh)
{
	Uniform *uni;
	char *src;
	char *type, *name, *id;
	unsigned int i;

	for (i = 0; i < SHADER_SRC_MAX; ++i) {
		src = sh->sources[i].source;

		if (!src)
			continue;

		while (src = strstr(src, "uniform")) {
			src += 7;
			type = extract_token(&src);
			name = extract_token(&src);
			id = BLI_strdupcat(name, type);
			uni = (Uniform *)BLI_ghash_popkey(sh->uniform_cache, id, NULL);
			if (!uni) {
				uni = uniform_init(type, name);
				while (*src++ != ';') {
					if (*src == '=')
						extract_default(src + 1, uni);
				}
			}
			BLI_addtail(&sh->uniforms, uni);
			MEM_freeN(type);
			MEM_freeN(name);
			MEM_freeN(id);
		}
	}

}

static char* parse_pragmas(char *src)
{
	char *light_pragma = "#pragma bgl include lights";
	char *tmp;

	tmp = BLI_replacestrN(src, light_pragma, light_code);
	MEM_freeN(src);
	src = tmp;

	return src;
}

void BKE_shader_read_source(Shader *sh, Main *main)
{
	Uniform *uni;
	unsigned int i;

	/* Clean up any previous source text */

	for (i = 0; i < SHADER_SRC_MAX; ++i) {
		if (sh->sources[i].source) {
			MEM_freeN(sh->sources[i].source);
			sh->sources[i].source = NULL;
		}

		if (sh->sources[i].flags & SHADERSRC_EXTERNAL)
			sh->sources[i].source = file_to_buf(sh->sources[i].filepath, main);
		else
			sh->sources[i].source = txt_to_buf(sh->sources[i].textptr);

		if (sh->sources[i].source)
			sh->sources[i].source = parse_pragmas(sh->sources[i].source);
	}

	/* Cache the uniform list and rebuild it */
	for (uni = sh->uniforms.first; uni; uni = uni->next)
		BLI_ghash_insert(sh->uniform_cache, uni->id, uni);

	sh->uniforms.first = sh->uniforms.last = NULL;

	gather_uniforms(sh);
}
