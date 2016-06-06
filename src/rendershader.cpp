// rendershader.cpp: core opengl shader rendering stuff

#include "cube.h"

GL_ShaderData gl_data[MAX_SHADER] = {
	{
		0, 0, 0,
		// vertex shader
		"varying vec4 v_color;"
		"varying vec2 v_texCoord;"
		"void main() {"
		"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;"
		"	v_color = gl_Color;"
		"	v_texCoord = vec2(gl_MultiTexCoord0);"
		"}",
		// fragment shader
		"varying vec4 v_color;"
		"varying vec2 v_texCoord;"
		"uniform sampler2D tex0;"
		"void main() {"
		"	gl_FragColor = texture2D(tex0, v_texCoord) * v_color;"
		"}"
	},
	{
		// kuwahara filter
		// https://code.google.com/archive/p/gpuakf/
		0, 0, 0,
		// vertex shader
		"varying vec2 v_texCoord;"
		"void main() {"
		"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;"
		"	v_texCoord = vec2(gl_MultiTexCoord0);"
		"}",
		// fragment shader
		"varying vec2 v_texCoord;"
		"uniform sampler2D tex0;"
		"void main() {"
		"	float radius = 3.0;"
		"	vec2 src_size = vec2(textureSize2D(tex0, 0));"
		"	float n = float((radius + 1) * (radius + 1));"
		"	vec3 m[4];"
		"	vec3 s[4];"
		"	for (int k = 0; k < 4; ++k) {"
		"		m[k] = vec3(0.0);"
		"		s[k] = vec3(0.0);"
		"	}"
		"	for (int j = -radius; j <= 0; ++j)  {"
		"		for (int i = -radius; i <= 0; ++i)  {"
		"			vec3 c = texture2D(tex0, v_texCoord + vec2(i, j) / src_size).rgb;"
		"			m[0] += c;"
		"			s[0] += c * c;"
		"		}"
		"	}"
		"	for (int j = -radius; j <= 0; ++j)  {"
		"		for (int i = 0; i <= radius; ++i)  {"
		"			vec3 c = texture2D(tex0, v_texCoord + vec2(i, j) / src_size).rgb;"
		"			m[1] += c;"
		"			s[1] += c * c;"
		"		}"
		"	}"
		"	for (int j = 0; j <= radius; ++j)  {"
		"		for (int i = 0; i <= radius; ++i)  {"
		"			vec3 c = texture2D(tex0, v_texCoord + vec2(i, j) / src_size).rgb;"
		"			m[2] += c;"
		"			s[2] += c * c;"
		"		}"
		"	}"
		"	for (int j = 0; j <= radius; ++j)  {"
		"		for (int i = -radius; i <= 0; ++i)  {"
		"			vec3 c = texture2D(tex0, v_texCoord + vec2(i, j) / src_size).rgb;"
		"			m[3] += c;"
		"			s[3] += c * c;"
		"		}"
		"	}"
		"	float min_sigma2 = 1e+2;"
		"	for (int k = 0; k < 4; ++k) {"
		"		m[k] /= n;"
		"		s[k] = abs(s[k] / n - m[k] * m[k]);"
		"		float sigma2 = s[k].r + s[k].g + s[k].b;"
		"		if (sigma2 < min_sigma2) {"
		"			min_sigma2 = sigma2;"
		"			gl_FragColor = vec4(m[k], 1.0);"
		"		}"
		"	}"
		"}"
	}
};

#define SDL_arraysize(array)			(sizeof(array)/sizeof(array[0]))
#define SDL_stack_alloc(type, count)    (type*)malloc(sizeof(type)*(count))
#define SDL_stack_free(data)            free(data)

struct GL_ShaderContext
{
	GLenum(*glGetError)(void);

	PFNGLATTACHOBJECTARBPROC			glAttachObjectARB;
	PFNGLCOMPILESHADERARBPROC			glCompileShaderARB;
	PFNGLCREATEPROGRAMOBJECTARBPROC		glCreateProgramObjectARB;
	PFNGLCREATESHADEROBJECTARBPROC		glCreateShaderObjectARB;
	PFNGLDELETEOBJECTARBPROC			glDeleteObjectARB;
	PFNGLGETINFOLOGARBPROC				glGetInfoLogARB;
	PFNGLGETOBJECTPARAMETERIVARBPROC	glGetObjectParameterivARB;
	PFNGLGETUNIFORMLOCATIONARBPROC		glGetUniformLocationARB;
	PFNGLLINKPROGRAMARBPROC				glLinkProgramARB;
	PFNGLSHADERSOURCEARBPROC			glShaderSourceARB;
	PFNGLUNIFORM1IARBPROC				glUniform1iARB;
	PFNGLUNIFORM1FARBPROC				glUniform1fARB;
	PFNGLUSEPROGRAMOBJECTARBPROC		glUseProgramObjectARB;
};

GL_ShaderContext *gl_shaderctx = 0;

inline bool isAtLeastGL3(const char *verstr)
{
	return (verstr && (ATOI(verstr) >= 3));
}

bool gl_extsupported(const char *extension)
{
	char *exts = (char *)glGetString(GL_EXTENSIONS);
	return (strstr(exts, extension));
}

bool gl_compileshader(unsigned int shader, const char *defines, const char *source)
{
	GLint status;
	const char *sources[2];

	sources[0] = defines;
	sources[1] = source;

	gl_shaderctx->glShaderSourceARB(shader, SDL_arraysize(sources), sources, NULL);
	gl_shaderctx->glCompileShaderARB(shader);
	gl_shaderctx->glGetObjectParameterivARB(shader, GL_OBJECT_COMPILE_STATUS_ARB, &status);
	if (status == 0) {
		GLint length;
		char *info;

		gl_shaderctx->glGetObjectParameterivARB(shader, GL_OBJECT_INFO_LOG_LENGTH_ARB, &length);
		info = SDL_stack_alloc(char, length + 1);
		gl_shaderctx->glGetInfoLogARB(shader, length, NULL, info);

		conoutf("Failed to compile shader:\n%s%s\n%s", defines, source, info);
		SDL_stack_free(info);

		return false;
	}
	else {
		return true;
	}
}

bool gl_compileshaderprogram(GL_ShaderData *data)
{
	const int num_tmus_bound = 4;
	const char *vert_defines = "";
	const char *frag_defines = "";
	int i, location;

	gl_shaderctx->glGetError();

	data->program = gl_shaderctx->glCreateProgramObjectARB();

	data->vert_shader = gl_shaderctx->glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
	if ( ! gl_compileshader(data->vert_shader, vert_defines, data->vert_source)) {
		return false;
	}

	data->frag_shader = gl_shaderctx->glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
	if ( ! gl_compileshader(data->frag_shader, frag_defines, data->frag_source)) {
		return false;
	}

	gl_shaderctx->glAttachObjectARB(data->program, data->vert_shader);
	gl_shaderctx->glAttachObjectARB(data->program, data->frag_shader);
	gl_shaderctx->glLinkProgramARB(data->program);

	gl_shaderctx->glUseProgramObjectARB(data->program);
	for (i = 0; i < num_tmus_bound; ++i) {
		char tex_name[10];
		sprintf(tex_name, "tex%d", i);
		location = gl_shaderctx->glGetUniformLocationARB(data->program, tex_name);
		if (location >= 0) {
			gl_shaderctx->glUniform1iARB(location, i);
		}
	}
	gl_shaderctx->glUseProgramObjectARB(0);

	return (gl_shaderctx->glGetError() == GL_NO_ERROR);
}

void gl_destroyshaderprogram(GL_ShaderData *data)
{
	gl_shaderctx->glDeleteObjectARB(data->vert_shader);
	gl_shaderctx->glDeleteObjectARB(data->frag_shader);
	gl_shaderctx->glDeleteObjectARB(data->program);
}

GL_ShaderContext *gl_createshadercontext()
{
	GL_ShaderContext *ctx;
	bool shaders_supported;

	ctx = (GL_ShaderContext *)calloc(1, sizeof(*ctx));
	if (!ctx) {
		return NULL;
	}

	/* Check for shader support */
	shaders_supported = false;
	if (gl_extsupported("GL_ARB_shader_objects") &&
		gl_extsupported("GL_ARB_shading_language_100") &&
		gl_extsupported("GL_ARB_vertex_shader") &&
		gl_extsupported("GL_ARB_fragment_shader")) {
		ctx->glGetError = (GLenum(*)(void)) SDL_GL_GetProcAddress("glGetError");
		ctx->glAttachObjectARB = (PFNGLATTACHOBJECTARBPROC)SDL_GL_GetProcAddress("glAttachObjectARB");
		ctx->glCompileShaderARB = (PFNGLCOMPILESHADERARBPROC)SDL_GL_GetProcAddress("glCompileShaderARB");
		ctx->glCreateProgramObjectARB = (PFNGLCREATEPROGRAMOBJECTARBPROC)SDL_GL_GetProcAddress("glCreateProgramObjectARB");
		ctx->glCreateShaderObjectARB = (PFNGLCREATESHADEROBJECTARBPROC)SDL_GL_GetProcAddress("glCreateShaderObjectARB");
		ctx->glDeleteObjectARB = (PFNGLDELETEOBJECTARBPROC)SDL_GL_GetProcAddress("glDeleteObjectARB");
		ctx->glGetInfoLogARB = (PFNGLGETINFOLOGARBPROC)SDL_GL_GetProcAddress("glGetInfoLogARB");
		ctx->glGetObjectParameterivARB = (PFNGLGETOBJECTPARAMETERIVARBPROC)SDL_GL_GetProcAddress("glGetObjectParameterivARB");
		ctx->glGetUniformLocationARB = (PFNGLGETUNIFORMLOCATIONARBPROC)SDL_GL_GetProcAddress("glGetUniformLocationARB");
		ctx->glLinkProgramARB = (PFNGLLINKPROGRAMARBPROC)SDL_GL_GetProcAddress("glLinkProgramARB");
		ctx->glShaderSourceARB = (PFNGLSHADERSOURCEARBPROC)SDL_GL_GetProcAddress("glShaderSourceARB");
		ctx->glUniform1iARB = (PFNGLUNIFORM1IARBPROC)SDL_GL_GetProcAddress("glUniform1iARB");
		ctx->glUniform1fARB = (PFNGLUNIFORM1FARBPROC)SDL_GL_GetProcAddress("glUniform1fARB");
		ctx->glUseProgramObjectARB = (PFNGLUSEPROGRAMOBJECTARBPROC)SDL_GL_GetProcAddress("glUseProgramObjectARB");
		if (ctx->glGetError &&
			ctx->glAttachObjectARB &&
			ctx->glCompileShaderARB &&
			ctx->glCreateProgramObjectARB &&
			ctx->glCreateShaderObjectARB &&
			ctx->glDeleteObjectARB &&
			ctx->glGetInfoLogARB &&
			ctx->glGetObjectParameterivARB &&
			ctx->glGetUniformLocationARB &&
			ctx->glLinkProgramARB &&
			ctx->glShaderSourceARB &&
			ctx->glUniform1iARB &&
			ctx->glUniform1fARB &&
			ctx->glUseProgramObjectARB) {
			shaders_supported = true;

			conoutf("GLSL enabled\n");
		}
	}

	if ( ! shaders_supported) {
		conoutf("GLSL NOT enabled\n");
		free(ctx);
		return NULL;
	}

	return ctx;
}

void gl_selectshader(int num)
{
	GL_ShaderData *p = &gl_data[(num < MAX_SHADER ? num : MAX_SHADER - 1)];
	gl_shaderctx->glUseProgramObjectARB(p->program);
}

int gl_getuniformshader(GL_ShaderData *data, char *name)
{
	return gl_shaderctx->glGetUniformLocationARB(data->program, name);
}

void gl_setuniformshader(int location, float value)
{
	gl_shaderctx->glUniform1fARB(location, value);
}

void gl_destroyshadercontext()
{
	free(gl_shaderctx);
}

void gl_initshader()
{
	gl_shaderctx = gl_createshadercontext();
	gl_compileshaderprogram(&gl_data[SHADER_RGB]);
	gl_compileshaderprogram(&gl_data[SHADER_CEL]);
}
