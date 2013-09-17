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
 * Contributor(s): Blender Foundation (2008), Nathan Letwory
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/makesrna/intern/rna_shader.c
 *  \ingroup RNA
 */

#include <stdlib.h>

#include "RNA_define.h"
#include "rna_internal.h"
#include "DNA_shader_types.h"

#include "BKE_shader.h"

#include "BLI_listbase.h"

#ifdef RNA_RUNTIME

#include "WM_api.h"
#include "WM_types.h"
#include "BKE_depsgraph.h"
#include "DNA_scene_types.h"
#include "DNA_material_types.h"
#include "MEM_guardedalloc.h"

static void rna_Shader_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	Shader *sh = ptr->id.data;
	Material *ma;
	CustomShader *cs;

	DAG_id_tag_update(&sh->id, 0);

	for (ma = bmain->mat.first; ma; ma = ma->id.next) {
		for (cs = ma->custom_shaders.first; cs; cs = cs->next) {
			if (strcmp(cs->shader->id.name, sh->id.name) == 0) {	
				DAG_id_tag_update(&ma->id, 0);
				if(scene->gm.matmode == GAME_MAT_GLSL)
					WM_main_add_notifier(NC_MATERIAL|ND_SHADING_DRAW, ma);
				else
					WM_main_add_notifier(NC_MATERIAL|ND_SHADING, ma);
			}
		}
	}
}

static void rna_Shader_source_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
	Shader *sh = ptr->id.data;
	BKE_shader_read_source(sh, bmain);
	rna_Shader_update(bmain, scene, ptr);
}

static void rna_ShaderLink_name_get(struct PointerRNA *ptr, char *str)
{
	ShaderLink *link = (ShaderLink *)ptr->data;
	Shader *shader = link->shader;
	if (shader)
		strcpy(str, shader->id.name+2);
	else
		str[0] = '\0';
}

static int rna_ShaderLink_name_length(struct PointerRNA *ptr)
{
	ShaderLink *link = (ShaderLink *)ptr->data;
	Shader *shader = link->shader;
	if (shader)
		return strlen(shader->id.name+2);
	return 0;
}

PointerRNA rna_Shader_active_get(PointerRNA *ptr, ListBase *list, int index)
{
	LinkData *link = (LinkData *)BLI_findlink(list, index-1);
	Shader *shader = (link) ? (Shader*)link->data : NULL;
	return rna_pointer_inherit_refine(ptr, &RNA_Shader, shader);
}

void rna_Shader_active_set(PointerRNA value, ListBase *list, int index)
{
	Shader *sh = (Shader*)value.data;

	LinkData *link = BLI_findlink(list, index-1);
	if (!link)
	{
		link = MEM_callocN(sizeof(LinkData), "New Shader");
		link->data = sh;
		BLI_addtail(list, link);
	}
	else
	{
		if (link->data)
			((Shader*)link->data)->id.us--;
		link->data = sh;
	}

	id_us_plus((ID *) sh);
}

int rna_Shader_active_index_get(int index)
{
	return MAX2(index - 1, 0);
}

void rna_Shader_active_index_set(int value, int *index)
{
	*index = value + 1;
}

void rna_Shader_active_index_range(int *min, int *max, ListBase *list)
{
	*min = 0;
	*max = MAX2(0, BLI_countlist(list) - 1);
}

int rna_Shader_id_poll(PointerRNA *ptr, PointerRNA value, char use)
{
	Shader *sh = (Shader *)value.id.data;
	
	if (sh->use == use)
		return 1;
	return 0;
}

static int rna_IntUniform_value_get(struct PointerRNA *ptr)
{
	Uniform *uni = (Uniform*)ptr->data;
	return *(int*)&uni->data;
}

static void rna_IntUniform_value_set(struct PointerRNA *ptr, int value)
{
	Uniform *uni = (Uniform*)ptr->data;
	*(int*)&uni->data = value;
}

static float rna_FloatUniform_value_get(struct PointerRNA *ptr)
{
	Uniform *uni = (Uniform*)ptr->data;
	return *(float*)(&uni->data);
}

static void rna_FloatUniform_value_set(struct PointerRNA *ptr, float value)
{
	Uniform *uni = (Uniform*)ptr->data;
	*(float*)(&uni->data) = value;
}

static void rna_VecUniform_value_get(struct PointerRNA *ptr, float *values)
{
	Uniform *uni = (Uniform*)ptr->data;
	int i;

	memset(values, 0.f, sizeof(float)*uni->size);
	if (uni->data) {
		for(i = 0; i < uni->size; i++)
			values[i] = ((float*)uni->data)[i];
	}
}

static void rna_VecUniform_value_set(struct PointerRNA *ptr, const float *values)
{
	Uniform *uni = (Uniform*)ptr->data;
	int i;

	for (i = 0; i < uni->size; i++)
		((float*)uni->data)[i] = values[i];
}

static void rna_IVecUniform_value_get(struct PointerRNA *ptr, int *values)
{
	Uniform *uni = (Uniform*)ptr->data;
	int i;

	memset(values, 0, sizeof(int) * uni->size);

	for(i = 0; i < uni->size; i++)
		values[i] = ((int*)uni->data)[i];
}

static void rna_IVecUniform_value_set(struct PointerRNA *ptr, const int *values)
{
	Uniform *uni = (Uniform*)ptr->data;
	int i;

	for (i = 0; i < uni->size; i++)
		((int*)uni->data)[i] = values[i];
}

static StructRNA* rna_Uniform_refine(struct PointerRNA *ptr)
{
	Uniform *uni = (Uniform*)ptr->data;

	switch(uni->type){
	case SHADER_UNF_FLOAT:
		return &RNA_FloatUniform;
	case SHADER_UNF_VEC2:
		return &RNA_Vec2Uniform;
	case SHADER_UNF_VEC3:
		return &RNA_Vec3Uniform;
	case SHADER_UNF_VEC4:
		return &RNA_Vec4Uniform;
	case SHADER_UNF_INT:
		return &RNA_IntUniform;	
	case SHADER_UNF_IVEC2:
		return &RNA_IVec2Uniform;
	case SHADER_UNF_IVEC3:
		return &RNA_IVec3Uniform;
	case SHADER_UNF_IVEC4:
		return &RNA_IVec4Uniform;
	case SHADER_UNF_SAMPLER2D:
		return &RNA_Sampler2DUniform;
	case SHADER_UNF_MAT2:
		return &RNA_Mat2Uniform;
	case SHADER_UNF_MAT3:
		return &RNA_Mat3Uniform;
	case SHADER_UNF_MAT4:
		return &RNA_Mat4Uniform;
	default:
		return &RNA_Uniform;
	}
}

#endif

/* Helper function for defining the different uniform data types */
static void rna_def_uniformtype(BlenderRNA *brna, const char *name, const char *doc, int type, int subtype, int size)
{
	PropertyRNA *prop;
	StructRNA *srna;
	int x2[] = {2, 2};
	int x3[] = {3, 3};
	int x4[] = {4, 4};

	srna = RNA_def_struct(brna, name, "Uniform");
	RNA_def_struct_ui_text(srna, name, doc);
	RNA_def_struct_sdna(srna, "Uniform");

	prop= RNA_def_property(srna, "value", type, subtype);
	
	if (size == 4 && subtype == PROP_MATRIX)
		RNA_def_property_multi_array(prop, 2, x2);
	else if (size == 9)
		RNA_def_property_multi_array(prop, 2, x3);
	else if (size == 16)
		RNA_def_property_multi_array(prop, 2, x4);
	else if (size > 1)
		RNA_def_property_array(prop, size);

	if (type == PROP_INT)
	{
		if (size > 1)
			RNA_def_property_int_funcs(prop, "rna_IVecUniform_value_get", "rna_IVecUniform_value_set", NULL);
		else
		{
			RNA_def_property_int_funcs(prop, "rna_IntUniform_value_get", "rna_IntUniform_value_set", NULL);
		}
	}

	else if (type == PROP_FLOAT)
	{
		if (size > 1)
			RNA_def_property_float_funcs(prop, "rna_VecUniform_value_get", "rna_VecUniform_value_set", NULL);
		else
		{
			RNA_def_property_float_funcs(prop, "rna_FloatUniform_value_get", "rna_FloatUniform_value_set", NULL);
		}
	}
	else if (type == PROP_POINTER)
	{	
		RNA_def_property_pointer_sdna(prop, NULL, "data");
		RNA_def_property_struct_type(prop, "Texture");
		RNA_def_property_flag(prop, PROP_EDITABLE);
	}

	RNA_def_property_ui_text(prop, "Value", "Uniform value");
	RNA_def_property_update(prop, 0, "rna_Shader_update");
}

static void rna_def_shader_uniform(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static EnumPropertyItem prop_type_items[] = {
		{SHADER_UNF_FLOAT, "FLOAT", 0, "float", "float"},
		{SHADER_UNF_VEC2, "VEC2", 0, "vec2", "vec2"},
		{SHADER_UNF_VEC3, "VEC3", 0, "vec3", "vec3"},
		{SHADER_UNF_VEC4, "VEC4", 0, "vec4", "vec4"},
		{SHADER_UNF_INT, "INT", 0, "int", "int"},
		{SHADER_UNF_IVEC2, "IVEC2", 0, "ivec2", "ivec2"},
		{SHADER_UNF_IVEC3, "IVEC3", 0, "ivec3", "ivec3"},
		{SHADER_UNF_IVEC4, "IVEC4", 0, "ivec4", "ivec4"},
		{SHADER_UNF_SAMPLER2D, "SAMPLER2D", 0, "sampler2D", "sampler2D"},
		{SHADER_UNF_MAT2, "MAT2", 0, "mat2", "mat2"},
		{SHADER_UNF_MAT3, "MAT3", 0, "mat3", "mat3"},
		{SHADER_UNF_MAT4, "MAT4", 0, "mat4", "mat4"},
		{0, NULL, 0, NULL, NULL}};
		
	srna = RNA_def_struct(brna, "Uniform", NULL);
	RNA_def_struct_sdna(srna, "Uniform");
	RNA_def_struct_ui_text(srna, "Uniform", "Uniform data for custom shaders");
	RNA_def_struct_refine_func(srna, "rna_Uniform_refine");

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_string_sdna(prop, NULL, "name");
	RNA_def_property_ui_text(prop, "Name", "The name of the uniform");

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, prop_type_items);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Uniform type", "The data type of the uniform");

	rna_def_uniformtype(brna, "FloatUniform", "Custom float uniform", PROP_FLOAT, PROP_NONE, 1);
	rna_def_uniformtype(brna, "IntUniform", "Custom integer uniform", PROP_INT, PROP_NONE, 1);
	rna_def_uniformtype(brna, "Vec2Uniform", "Custom float vector 2 uniform", PROP_FLOAT, PROP_NONE, 2);
	rna_def_uniformtype(brna, "Vec3Uniform", "Custom float vector 3 uniform", PROP_FLOAT, PROP_NONE, 3);
	rna_def_uniformtype(brna, "Vec4Uniform", "Custom float vector 4 uniform", PROP_FLOAT, PROP_NONE, 4);
	rna_def_uniformtype(brna, "IVec2Uniform", "Custom integer vector 2 uniform", PROP_INT, PROP_NONE, 2);
	rna_def_uniformtype(brna, "IVec3Uniform", "Custom integer vector 3 uniform", PROP_INT, PROP_NONE, 3);
	rna_def_uniformtype(brna, "IVec4Uniform", "Custom integer vector 4 uniform", PROP_INT, PROP_NONE, 4);
	rna_def_uniformtype(brna, "Mat2Uniform", "Custom 2x2 matrix uniform", PROP_FLOAT, PROP_MATRIX, 4);
	rna_def_uniformtype(brna, "Mat3Uniform", "Custom 3x3 matrix uniform", PROP_FLOAT, PROP_MATRIX, 9);
	rna_def_uniformtype(brna, "Mat4Uniform", "Custom 4x4 matrix uniform", PROP_FLOAT, PROP_MATRIX, 16);
	rna_def_uniformtype(brna, "Sampler2DUniform", "Custom sampler2D uniform", PROP_POINTER, PROP_NONE, 1);
}

static void rna_def_shader_shaderlink(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	srna = RNA_def_struct(brna, "ShaderLink", NULL);
	RNA_def_struct_ui_icon(srna, ICON_TEXT);
	RNA_def_struct_sdna(srna, "ShaderLink");

	prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
	RNA_def_struct_name_property(srna, prop);
	RNA_def_property_clear_flag(prop, PROP_EDITABLE);
	RNA_def_property_string_funcs(prop, "rna_ShaderLink_name_get", "rna_ShaderLink_name_length", NULL);
	RNA_def_property_ui_text(prop, "Name", "The name of the shader");
}

void RNA_def_shader(BlenderRNA *brna)
{
	StructRNA *srna;
	PropertyRNA *prop;

	static EnumPropertyItem prop_type_items[] = {
		{SHADER_TYPE_VERTEX, "VERTEX", 0, "Vertex", "Use as vertex shader"},
		{SHADER_TYPE_FRAGMENT, "FRAGMENT", 0, "Fragment", "Use as fragment shader"},
		{SHADER_TYPE_GEOMETRY, "GEOMETRY", 0, "Geometry", "Use as geometry shader"},
		{0, NULL, 0, NULL, NULL}
	};

	static EnumPropertyItem prop_pass_items[] = {
		{SHADER_PASS_RENDER, "RENDER", 0, "Render", "Use this shader in the main render pass"},
		{SHADER_PASS_PRE, "PREPASS", 0, "Pre-Pass", "Use this shader in pass before the main render"},
		{SHADER_PASS_LIGHT, "LIGHT", 0, "Light", "Use this shader when drawing lights"},
		{0, NULL, 0, NULL, NULL}
	};

	static EnumPropertyItem prop_shader_loc_items[] = {
		{SHADER_LOC_BUILTIN, "BUILTIN", 0, "Builtin", "Use a builtin shader"},
		{SHADER_LOC_INTERNAL, "INTERNAL", 0, "Internal", "Use a shader from a text datablock"},
		{SHADER_LOC_EXTERNAL, "EXTERNAL", 0, "External", "Use a shader from an external file"},
		{0, NULL, 0, NULL, NULL}
	};

	static EnumPropertyItem prop_filter_loc_items[] = {
		{SHADER_LOC_BUILTIN, "BUILTIN", 0, "Builtin", "Use a builtin shader"},
		{SHADER_LOC_INTERNAL, "INTERNAL", 0, "Internal", "Use a shader from a text datablock"},
		{SHADER_LOC_EXTERNAL, "EXTERNAL", 0, "External", "Use a shader from an external file"},
		{0, NULL, 0, NULL, NULL}
	};

	static EnumPropertyItem prop_geomin_items[] = {
		{SHADER_GEOM_IN_POINTS, "POINTS", 0, "Points", "Use points as input"},
		{SHADER_GEOM_IN_LINES, "LINES", 0, "Lines", "Use lines as input"},
		{SHADER_GEOM_IN_LINES_ADJ, "LINES_ADJ", 0, "Lines Adjacency", "Use adjacent lines as input"},
		{SHADER_GEOM_IN_TRIS, "TRIS", 0, "Triangles", "Use triangles as input"},
		{SHADER_GEOM_IN_TRIS_ADJ, "TRIS_ADJ", 0, "Triangles Adjacency", "Use adjacent triangles as input"},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem prop_geomout_items[] = {
		{SHADER_GEOM_OUT_POINTS, "POINTS", 0, "Points", "Output points"},
		{SHADER_GEOM_OUT_LINE_STRIP, "LINE_STRIP", 0, "Line Strip", "Output line strips"},
		{SHADER_GEOM_OUT_TRIANGLE_STRIP, "TRIANGLE_STRIP", 0, "Triangle Strip", "Output triangle strips"},
		{0, NULL, 0, NULL, NULL}};

	static EnumPropertyItem prop_filter_builtin_items[] = {
		//{SHADER_2DFILTER_MOTIONBLUR, "MOTIONBLUR", 0, "Motion Blur", ""},
		{SHADER_2DFILTER_BLUR, "BLUR", 0, "Blur", "Decreases contrast"},
		{SHADER_2DFILTER_SHARPEN, "SHARPEN", 0, "Sharpen", "Increases contrast"},
		{SHADER_2DFILTER_DILATION, "DILATION", 0, "Dilation", ""},
		{SHADER_2DFILTER_EROSION, "EROSION", 0, "Erosion", ""},
		{SHADER_2DFILTER_LAPLACIAN, "LAPLACIAN", 0, "Laplacian", ""},
		{SHADER_2DFILTER_SOBEL, "SOBEL", 0, "Sobel", ""},
		{SHADER_2DFILTER_PREWITT, "PREWITT", 0, "Prewitt", ""},
		{SHADER_2DFILTER_GRAYSCALE, "GRAYSCALE", 0, "Grascale", "Removes color"},
		{SHADER_2DFILTER_SEPIA, "SEPIA", 0, "Sepia", "Converts color to a Sepia tone"},
		{SHADER_2DFILTER_INVERT, "INVERT", 0, "Invert", "Inverts colors"},
		{0, NULL, 0, NULL, NULL}};

	srna = RNA_def_struct(brna, "Shader", "ID");
	RNA_def_struct_ui_text(srna, "Shader", "Shader datablock to define custom shading for a material");
	RNA_def_struct_ui_icon(srna, ICON_TEXT);

	prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "type");
	RNA_def_property_enum_items(prop, prop_type_items);
	RNA_def_property_ui_text(prop, "Type", "Type of shader to use");
	RNA_def_property_update(prop, 0, "rna_Shader_update");

	prop = RNA_def_property(srna, "render_pass", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "pass");
	RNA_def_property_enum_items(prop, prop_pass_items);
	RNA_def_property_ui_text(prop, "Pass", "Controls when the shader is used");
	RNA_def_property_update(prop, 0, "rna_Shader_update");

	prop = RNA_def_property(srna, "shader_location", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "location");
	RNA_def_property_enum_items(prop, prop_shader_loc_items);
	RNA_def_property_ui_text(prop, "Location", "Location of the shader to use");
	RNA_def_property_update(prop, 0, "rna_Shader_source_update");

	prop = RNA_def_property(srna, "filter_location", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "location");
	RNA_def_property_enum_items(prop, prop_filter_loc_items);
	RNA_def_property_ui_text(prop, "Location", "Location of the shader to use");
	RNA_def_property_update(prop, 0, "rna_Shader_update");

	prop = RNA_def_property(srna, "filter_builtin", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "sourceenum");
	RNA_def_property_enum_items(prop, prop_filter_builtin_items);
	RNA_def_property_ui_text(prop, "Filter", "Builtin filter to use");
	RNA_def_property_update(prop, 0, "rna_Shader_update");

	prop = RNA_def_property(srna, "source_text", PROP_POINTER, PROP_NONE);
	RNA_def_property_pointer_sdna(prop, NULL, "sourcetext");
	RNA_def_property_struct_type(prop, "Text");
	RNA_def_property_flag(prop, PROP_EDITABLE);
	RNA_def_property_ui_text(prop, "Source", "The shader text to use");
	RNA_def_property_update(prop, 0, "rna_Shader_source_update");

	prop = RNA_def_property(srna, "source_path", PROP_STRING, PROP_FILEPATH);
	RNA_def_property_string_sdna(prop, NULL, "sourcepath");
	RNA_def_property_ui_text(prop, "Source", "The path to the shader to use");
	RNA_def_property_update(prop, 0, "rna_Shader_source_update");

	prop = RNA_def_property(srna, "geometry_input", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "geom_in");
	RNA_def_property_enum_items(prop, prop_geomin_items);
	RNA_def_property_ui_text(prop, "Input", "The input type for the geometry shader");
	RNA_def_property_update(prop, 0, "rna_Shader_update");

	prop = RNA_def_property(srna, "geometry_output", PROP_ENUM, PROP_NONE);
	RNA_def_property_enum_sdna(prop, NULL, "geom_out");
	RNA_def_property_enum_items(prop, prop_geomout_items);
	RNA_def_property_ui_text(prop, "Output", "The output type for the geometry shader");
	RNA_def_property_update(prop, 0, "rna_Shader_update");

	prop = RNA_def_property(srna, "uniforms", PROP_COLLECTION, PROP_NONE);
	RNA_def_property_collection_sdna(prop, NULL, "uniforms", NULL);
	RNA_def_property_struct_type(prop, "Uniform");
	RNA_def_property_ui_text(prop, "Custom Uniforms", "Uniform values to send to custom shaders");

	rna_def_shader_uniform(brna);
	rna_def_shader_shaderlink(brna);
}