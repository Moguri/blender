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

void init_shader(Shader *sh)
{
	sh->source = NULL;
	sh->type = SHADER_TYPE_FRAGMENT;
	sh->location = SHADER_LOC_BUILTIN;

	sh->uniforms.first = NULL;
	sh->uniforms.last = NULL;

	sh->uniform_cache = BLI_ghash_str_new("Uniform Cache");
}

void BKE_shader_lib_free(Shader *sh)
{
	Uniform *uni;
	if (sh->source)
		MEM_freeN(sh->source);
	
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
	sh = BKE_libblock_alloc(&G.main->shader, ID_SH, name);
	init_shader(sh);
	return sh;
}

struct Shader *BKE_shader_copy(Shader *sh)
{
	Shader *shn;
	shn = BKE_libblock_copy(&sh->id);
	return shn;
}

struct Shader *BKE_shader_empty()
{
	Shader *sh;
	sh = MEM_mallocN(sizeof(Shader), "Empty Shader");
	init_shader(sh);
	return sh;
}

void BKE_shader_source_merge(Shader *dst, const Shader *src)
{
	DynStr *new_source;
	char fname1[40] = "main\0";
	char fname2[40] = "main\0";
	char count[33];
	char i = 0;
	char *dst_main, *src_main, *search;

	/* Handle a couple of NULL cases */
	if (src->source == NULL)
		return;

	if (dst->source == NULL) {
		dst->source = MEM_mallocN(strlen(src->source)+1, "Merged shader source");
		strcpy(dst->source, src->source);
		return;
	}

	/* Find main, and assume there is nothing after it */
	dst_main = strstr(dst->source, "void main");
	src_main = strstr(src->source, "void main");

	if (!dst_main || ! src_main)
		return;

	new_source = BLI_dynstr_new();

	/* Put the fluff into the new shader */
	BLI_dynstr_nappend(new_source, dst->source, dst_main-dst->source);
	BLI_dynstr_nappend(new_source, src->source, src_main-src->source);

	/* Find a new name for the first main */
	search = BLI_dynstr_get_cstring(new_source);
	do {
		sprintf(count, "%d", i++);
		strcpy(fname1, "main");
		strcat(fname1, count);
	}while (search = strstr(dst->source, fname1));
	search = BLI_dynstr_get_cstring(new_source);

	/* Add the first main back in with new name */
	while (*(dst_main++) != '(');
	BLI_dynstr_appendf(new_source, "void %s %s", fname1, --dst_main);

	/* Find a name for the second main */
	do {
		sprintf(count, "%d", i++);
		strcpy(fname2, "main");
		strcat(fname2, count);
	}while (search = strstr(dst->source, fname2));

	/* Add the second main back in with new name */
	while (*(src_main++) != '(');
	BLI_dynstr_appendf(new_source, "void %s %s", fname2, --src_main);

	/* Build a new main function */
	BLI_dynstr_appendf(new_source, "\nvoid main()\n{\t%s();\n\t%s();\n}", fname1, fname2);

	/* Copy the DynStr back to dst->source and clean up*/
	search = BLI_dynstr_get_cstring(new_source);
	dst->source = MEM_mallocN(strlen(search)+1, "Merged shader source");
	strcpy(dst->source, search);

	BLI_dynstr_free(new_source);
	MEM_freeN(search);
}

void gather_uniforms(Shader *sh);
void BKE_shader_source_merge_ch(Shader *dst, const char *src)
{
	Shader *sh = BKE_shader_empty();
	sh->source = MEM_mallocN(strlen(src)+1, "Shader merge temp");
	strcpy(sh->source, src);
	gather_uniforms(sh);
	BKE_shader_source_merge(dst, sh);
	BKE_shader_free(sh);
}

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
	char *src = sh->source;
	char *type, *name, *id;

	if (!src)
		return;

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
					extract_default(src+1, uni);
			}
		}
		BLI_addtail(&sh->uniforms, uni);
		MEM_freeN(type);
		MEM_freeN(name);
		MEM_freeN(id);
	}

}

void BKE_shader_read_source(Shader *sh, Main *main)
{
	Uniform *uni;

	/* Clean up an previous source text */
	if (sh->source) {
		MEM_freeN(sh->source);
		sh->source = NULL;
	}

	if (sh->location == SHADER_LOC_INTERNAL)
		sh->source = txt_to_buf(sh->sourcetext);
	else if (sh->location == SHADER_LOC_EXTERNAL)
		sh->source = file_to_buf(sh->sourcepath, main);

	/* Cache the uniform list and rebuild it */
	for (uni = sh->uniforms.first; uni; uni = uni->next)
		BLI_ghash_insert(sh->uniform_cache, uni->id, uni);

	sh->uniforms.first = sh->uniforms.last = NULL;

	if (sh->source)
		gather_uniforms(sh);
}
