#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <gl2d/gl2d.h>
#include <openglErrorReporting.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imguiThemes.h"

static void error_callback(int error, const char *description)
{
	std::cout << "Error: " << description << "\n";
}

static GLuint compileShader(GLenum type, const char* src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);

	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		GLint len = 0;
		glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
		std::string log(len, '\0');
		glGetShaderInfoLog(s, len, nullptr, log.data());
		std::cerr << "[Shader compile error]\n" << log << "\n";
		glDeleteShader(s);
		return 0;
	}
	return s;
}

static GLuint createProgram(const char* vsSrc, const char* fsSrc)
{
	GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
	GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
	if (!vs || !fs) return 0;

	GLuint p = glCreateProgram();
	glAttachShader(p, vs);
	glAttachShader(p, fs);
	glLinkProgram(p);

	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		GLint len = 0;
		glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
		std::string log(len, '\0');
		glGetProgramInfoLog(p, len, nullptr, log.data());
		std::cerr << "[Program link error]\n" << log << "\n";
		glDeleteProgram(p);
		return 0;
	}

	return p;
}

int main(void)
{

	glfwSetErrorCallback(error_callback);

	if (!glfwInit())
		exit(EXIT_FAILURE);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow *window = glfwCreateWindow(640, 480, "Simple example", NULL, NULL);
	if (!window)
	{
		glfwTerminate();
		exit(EXIT_FAILURE);
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		glfwTerminate();
		exit(EXIT_FAILURE);
	}

	float vertices[] = {
		0.0f,  0.5f,  // Vertex 1 (X, Y)
		0.5f, -0.5f,  // Vertex 2 (X, Y)
	   -0.5f, -0.5f   // Vertex 3 (X, Y)
	};

	GLuint vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);

	const char* vsSrc = R"(
#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2 uOffset;
void main() {
    gl_Position = vec4(aPos + uOffset, 0.0, 1.0);
}
)";

	const char* fsSrc = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1,0,0,1);
}
)";

	GLuint program = createProgram(vsSrc, fsSrc);
	if (!program) {
		std::cerr << "Failed to create shader program\n";
		return EXIT_FAILURE;
	}

	GLint locOffset = glGetUniformLocation(program, "uOffset");

	float posX = 0.0f, posY = 0.0f;
	float speed = 0.8f;

	double last = glfwGetTime();

	enableReportGlErrors();
	
	while (!glfwWindowShouldClose(window))
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);

		double now = glfwGetTime();
		float dt = (float)(now - last);
		last = now;

		float dx = 0.0f;

		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
			glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		{
			dx -= 1.0f;
		}

		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
			glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		{
			dx += 1.0f;
		}

		posX += dx * speed * dt;
		
		// map bounds
		if (posX > 0.9f)  posX = 0.9f;
		if (posX < -0.9f) posX = -0.9f;


		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(program);
		glUniform2f(locOffset, posX, posY);

		glBindVertexArray(vao);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}


	glfwDestroyWindow(window);

	glfwTerminate();

	return 0;
}

