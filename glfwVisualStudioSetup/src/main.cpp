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

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>

struct Obstacle { float x, y, speed; };
std::vector<Obstacle> obstacles;
double spawnAcc = 0.0;


static std::string LoadTextFile(const char* path)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		std::cerr << "Failed to open file: " << path << "\n";
		return {};
	}

	std::stringstream ss;
	ss << file.rdbuf();
	return ss.str();
}


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
	srand((unsigned)time(nullptr));
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

	float playerVerts[] = {
		// 1st tri
		-0.08f, -0.08f,
		 0.08f, -0.08f,
		 0.08f,  0.08f,
		 // 2nd tri
		 -0.08f, -0.08f,
		  0.08f,  0.08f,
		 -0.08f,  0.08f
	};

	GLuint playerVAO = 0, playerVBO = 0;
	glGenVertexArrays(1, &playerVAO);
	glGenBuffers(1, &playerVBO);

	glBindVertexArray(playerVAO);
	glBindBuffer(GL_ARRAY_BUFFER, playerVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(playerVerts), playerVerts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);


	float spikeVerts[] = {
		 0.0f,  -0.08f,   // tip (¾Æ·¡)
		 0.07f,  0.08f,   // right top
		-0.07f,  0.08f    // left top
	};

	GLuint spikeVAO = 0, spikeVBO = 0;
	glGenVertexArrays(1, &spikeVAO);
	glGenBuffers(1, &spikeVBO);

	glBindVertexArray(spikeVAO);
	glBindBuffer(GL_ARRAY_BUFFER, spikeVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(spikeVerts), spikeVerts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);


	std::string vertSrc = LoadTextFile("resources/basic.vert");
	std::string fragSrc = LoadTextFile("resources/basic.frag");

	if (vertSrc.empty() || fragSrc.empty())
	{
		std::cerr << "Shader source empty\n";
		return EXIT_FAILURE;
	}

	GLuint program = createProgram(
		vertSrc.c_str(),
		fragSrc.c_str()
	);

	GLint locColor = glGetUniformLocation(program, "uColor");

	if (!program) {
		std::cerr << "Failed to create shader program\n";
		return EXIT_FAILURE;
	}

	GLint locOffset = glGetUniformLocation(program, "uOffset");

	float posX = 0.0f, posY = 0.0f;
	float speed = 0.8f;

	double last = glfwGetTime();

	const float playerHalf = 0.06f;
	const float playerY = -1.0f + playerHalf + 0.02f; 

	enableReportGlErrors();
	
	glClearColor(0.05f, 0.05f, 0.05f, 1.0f);

	while (!glfwWindowShouldClose(window))
	{
		// --- viewport / dt ---
		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);

		double now = glfwGetTime();
		float dt = (float)(now - last);
		last = now;

		// --- input (x only) ---
		float dx = 0.0f;
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
			glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
			dx -= 1.0f;

		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
			glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
			dx += 1.0f;

		posX += dx * speed * dt;

		// map bounds
		if (posX > 0.9f)  posX = 0.9f;
		if (posX < -0.9f) posX = -0.9f;

		// --- spawn / update obstacles ---
		const float spawnInterval = 1.2f;
		spawnAcc += dt;
		if (spawnAcc >= spawnInterval)
		{
			spawnAcc = 0.0;
			float rx = ((rand() % 2001) / 1000.0f - 1.0f) * 0.9f; // -0.9~0.9
			obstacles.push_back({ rx, 1.2f, 0.45f });
		}

		for (auto& o : obstacles) o.y -= o.speed * dt;

		obstacles.erase(
			std::remove_if(obstacles.begin(), obstacles.end(),
				[](const Obstacle& o) { return o.y < -1.2f; }),
			obstacles.end()
		);

		// --- render ---
		glClear(GL_COLOR_BUFFER_BIT);
		glUseProgram(program);

		// player (green square)
		glBindVertexArray(playerVAO);
		glUniform3f(locColor, 0.0f, 1.0f, 0.0f);
		glUniform2f(locOffset, posX, playerY);
		glDrawArrays(GL_TRIANGLES, 0, 6); 

		// spikes (red inverted triangles)
		glBindVertexArray(spikeVAO);
		glUniform3f(locColor, 1.0f, 0.0f, 0.0f);
		for (const auto& o : obstacles) {
			glUniform2f(locOffset, o.x, o.y);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}


		glfwSwapBuffers(window);
		glfwPollEvents();
	}


	glfwDestroyWindow(window);

	glfwTerminate();

	return 0;
}