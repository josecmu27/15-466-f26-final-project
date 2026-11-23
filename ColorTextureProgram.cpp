#include "ColorTextureProgram.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

Load< ColorTextureProgram > color_texture_program(LoadTagEarly);

ColorTextureProgram::ColorTextureProgram() {
	//Compile vertex and fragment shaders using the convenient 'gl_compile_program' helper function:
	program = gl_compile_program(
		//vertex shader:
		"#version 330\n"
		"uniform mat4 OBJECT_TO_CLIP;\n"
		"in vec4 Position;\n"
		"in vec4 Color;\n"
		"in vec2 TexCoord;\n"
		"out vec4 color;\n"
		"out vec2 texCoord;\n"
		"void main() {\n"
		"	gl_Position = OBJECT_TO_CLIP * Position;\n"
		"	color = Color;\n"
		"	texCoord = TexCoord;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"uniform sampler2D TEX;\n"
		"uniform float SHARPEN;\n"
		"in vec4 color;\n"
		"in vec2 texCoord;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	vec4 texel = texture(TEX, texCoord);\n"
		"	if (SHARPEN > 0.001) {\n"
		"		vec2 px = 1.0 / vec2(textureSize(TEX, 0));\n"
		"		vec4 n = texture(TEX, texCoord + vec2( px.x, 0.0));\n"
		"		vec4 s = texture(TEX, texCoord + vec2(-px.x, 0.0));\n"
		"		vec4 e = texture(TEX, texCoord + vec2(0.0,  px.y));\n"
		"		vec4 w = texture(TEX, texCoord + vec2(0.0, -px.y));\n"
		"		vec3 rgb = texel.rgb * texel.a;\n"
		"		vec3 blur = (n.rgb * n.a + s.rgb * s.a + e.rgb * e.a + w.rgb * w.a) * 0.25;\n"
		"		float a = texel.a;\n"
		"		float ab = (n.a + s.a + e.a + w.a) * 0.25;\n"
		"		rgb += (rgb - blur) * SHARPEN;\n"
		"		a = clamp(a + (a - ab) * SHARPEN, 0.0, 1.0);\n"
		"		texel.rgb = a > 0.001 ? clamp(rgb / a, 0.0, 1.0) : texel.rgb;\n"
		"		texel.a = a;\n"
		"	}\n"
		"	fragColor = texel * color;\n"
		"}\n"
	);
	//As you can see above, adjacent strings in C/C++ are concatenated.
	// this is very useful for writing long shader programs inline.

	//look up the locations of vertex attributes:
	Position_vec4 = glGetAttribLocation(program, "Position");
	Color_vec4 = glGetAttribLocation(program, "Color");
	TexCoord_vec2 = glGetAttribLocation(program, "TexCoord");

	//look up the locations of uniforms:
	OBJECT_TO_CLIP_mat4 = glGetUniformLocation(program, "OBJECT_TO_CLIP");
	SHARPEN_float = glGetUniformLocation(program, "SHARPEN");
	GLuint TEX_sampler2D = glGetUniformLocation(program, "TEX");

	//set TEX to always refer to texture binding zero:
	glUseProgram(program); //bind program -- glUniform* calls refer to this program now

	glUniform1i(TEX_sampler2D, 0); //set TEX to sample from GL_TEXTURE0
	glUniform1f(SHARPEN_float, 0.0f);

	glUseProgram(0); //unbind program -- glUniform* calls refer to ??? now
}

ColorTextureProgram::~ColorTextureProgram() {
	glDeleteProgram(program);
	program = 0;
}

