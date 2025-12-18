#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cmath>

#include <openglErrorReporting.h>

#include <gl2d/gl2d.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imguiThemes.h"

#pragma region File Utils
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
#pragma endregion

#pragma region GLFW Error Callback
static void error_callback(int error, const char* description)
{
	std::cout << "GLFW Error(" << error << "): " << description << "\n";
}
#pragma endregion

#pragma region Shader Compile / Link
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
#pragma endregion

#pragma region Mesh Helper (2D positions only)
struct Mesh
{
	GLuint vao = 0;
	GLuint vbo = 0;
	GLsizei vertexCount = 0; // glDrawArrays count
};

static Mesh CreateMesh2D(const float* verts, size_t bytes, GLsizei vertexCount)
{
	Mesh m;
	m.vertexCount = vertexCount;

	glGenVertexArrays(1, &m.vao);
	glGenBuffers(1, &m.vbo);

	glBindVertexArray(m.vao);
	glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
	glBufferData(GL_ARRAY_BUFFER, bytes, verts, GL_STATIC_DRAW);

	// layout(location=0) in vec2 aPos
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBindVertexArray(0);
	return m;
}

static void DestroyMesh(Mesh& m)
{
	if (m.vbo) glDeleteBuffers(1, &m.vbo);
	if (m.vao) glDeleteVertexArrays(1, &m.vao);
	m.vbo = 0;
	m.vao = 0;
	m.vertexCount = 0;
}
#pragma endregion

#pragma region Game Types
struct Obstacle
{
	float x;
	float y;
	float speed;
};
#pragma endregion

int main(void)
{
#pragma region Init (GLFW/GLAD)
	srand((unsigned)time(nullptr));
	glfwSetErrorCallback(error_callback);

	if (!glfwInit())
		return EXIT_FAILURE;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(640, 480, "Block Dodger", nullptr, nullptr);
	if (!window)
	{
		glfwTerminate();
		return EXIT_FAILURE;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // vsync

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		glfwDestroyWindow(window);
		glfwTerminate();
		return EXIT_FAILURE;
	}

	enableReportGlErrors();
	glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
#pragma endregion

// -------------------------------------------------
// ImGui init
// -------------------------------------------------
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");


#pragma region Constants
	// Player is a square in NDC coordinates.
	// If PLAYER_HALF=0.08 => size is 0.16 x 0.16
	constexpr float PLAYER_HALF = 0.08f;
	constexpr float PLAYER_SPEED = 0.8f;

	// Keep player near the bottom (center y)
	constexpr float PLAYER_Y = -1.0f + PLAYER_HALF + 0.02f;

	// Clamp X so the square stays fully inside the screen
	constexpr float PLAYER_X_LIMIT = 1.0f - PLAYER_HALF;

	// Obstacles spawn settings
	constexpr float SPAWN_INTERVAL = 1.2f;  // slower spawn
	constexpr float SPAWN_Y = 1.2f;
	constexpr float SPIKE_SPEED = 0.45f;
	constexpr float DESPAWN_Y = -1.2f;

	// Spawn X range (keep inside)
	constexpr float SPAWN_X_LIMIT = 0.9f;

	constexpr float SPIKE_HALF_X = 0.07f;
	constexpr float SPIKE_HALF_Y = 0.08f;

#pragma endregion

#pragma region Create Meshes
	// Player square (2 triangles = 6 vertices)
	float playerVerts[] = {
		// tri 1
		-PLAYER_HALF, -PLAYER_HALF,
		 PLAYER_HALF, -PLAYER_HALF,
		 PLAYER_HALF,  PLAYER_HALF,
		 // tri 2
		 -PLAYER_HALF, -PLAYER_HALF,
		  PLAYER_HALF,  PLAYER_HALF,
		 -PLAYER_HALF,  PLAYER_HALF
	};

	// Spike (inverted triangle ▼) (3 vertices)
	// tip points downward
	float spikeVerts[] = {
		 0.0f,  -0.08f,
		 0.07f,  0.08f,
		-0.07f,  0.08f
	};

	Mesh playerMesh = CreateMesh2D(playerVerts, sizeof(playerVerts), 6);
	Mesh spikeMesh = CreateMesh2D(spikeVerts, sizeof(spikeVerts), 3);
#pragma endregion

#pragma region Load Shaders
	std::string vertSrc = LoadTextFile("resources/basic.vert");
	std::string fragSrc = LoadTextFile("resources/basic.frag");
	if (vertSrc.empty() || fragSrc.empty())
	{
		std::cerr << "Shader source empty (check working directory/path)\n";
		DestroyMesh(playerMesh);
		DestroyMesh(spikeMesh);
		glfwDestroyWindow(window);
		glfwTerminate();
		return EXIT_FAILURE;
	}

	GLuint program = createProgram(vertSrc.c_str(), fragSrc.c_str());
	if (!program)
	{
		std::cerr << "Failed to create shader program\n";
		DestroyMesh(playerMesh);
		DestroyMesh(spikeMesh);
		glfwDestroyWindow(window);
		glfwTerminate();
		return EXIT_FAILURE;
	}

	// uniforms
	GLint locOffset = glGetUniformLocation(program, "uOffset");
	GLint locColor = glGetUniformLocation(program, "uColor");
#pragma endregion

#pragma region Game State
	float playerX = 0.0f;
	double last = glfwGetTime();

	std::vector<Obstacle> obstacles;
	double spawnAcc = 0.0;

	bool gameOver = false;

	float score = 0.0f;
	float bestScore = 0.0f;

	float spawnInterval = SPAWN_INTERVAL;
	float spikeSpeed = SPIKE_SPEED;
	float difficultyT = 0.0f;

#pragma endregion

#pragma region Main Loop
	while (!glfwWindowShouldClose(window))
	{
		// -------------------------------------------------
		// viewport
		// -------------------------------------------------
		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);

		// -------------------------------------------------
		// delta time
		// -------------------------------------------------
		double now = glfwGetTime();
		float dt = (float)(now - last);
		last = now;

		// -------------------------------------------------
		// restart (game over)
		// -------------------------------------------------
		if (gameOver)
		{
			if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
			{
				gameOver = false;
				playerX = 0.0f;
				obstacles.clear();
				spawnAcc = 0.0;
				last = glfwGetTime();

				score = 0.0f;
				spawnInterval = SPAWN_INTERVAL;
				spikeSpeed = SPIKE_SPEED;
				difficultyT = 0.0f;
			}
		}

		// -------------------------------------------------
		// game logic (only if not game over)
		// -------------------------------------------------
		if (!gameOver)
		{
			// ---------- input (x only) ----------
			float dx = 0.0f;

			if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
				glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
				dx -= 1.0f;

			if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
				glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
				dx += 1.0f;

			playerX += dx * PLAYER_SPEED * dt;

			// clamp player inside screen
			if (playerX > PLAYER_X_LIMIT) playerX = PLAYER_X_LIMIT;
			if (playerX < -PLAYER_X_LIMIT) playerX = -PLAYER_X_LIMIT;

			// ---------- spawn obstacles ----------
			spawnAcc += dt;
			if (spawnAcc >= spawnInterval)
			{
				spawnAcc = 0.0;
				float rx = ((rand() % 2001) / 1000.0f - 1.0f) * SPAWN_X_LIMIT;
				obstacles.push_back({ rx, SPAWN_Y, spikeSpeed });
			}

			// ---------- update obstacles ----------
			for (auto& o : obstacles)
				o.y -= o.speed * dt;

			// ---------- collision (AABB) ----------
			const float px = playerX;
			const float py = PLAYER_Y;

			for (const auto& o : obstacles)
			{
				bool overlapX = std::fabs(px - o.x) < (PLAYER_HALF + SPIKE_HALF_X);
				bool overlapY = std::fabs(py - o.y) < (PLAYER_HALF + SPIKE_HALF_Y);

				if (overlapX && overlapY)
				{
					gameOver = true;
					break;
				}
			}

			score += dt;
			if (score > bestScore) bestScore = score;

			difficultyT += dt;
			if (difficultyT >= 5.0f)
			{
				difficultyT = 0.0f;

				spawnInterval = std::max(0.35f, spawnInterval - 0.08f);

				spikeSpeed += 0.05f;
			}


			// ---------- remove off-screen obstacles ----------
			obstacles.erase(
				std::remove_if(obstacles.begin(), obstacles.end(),
					[](const Obstacle& o) { return o.y < DESPAWN_Y; }),
				obstacles.end()
			);
		}

		// -------------------------------------------------
		// render
		// -------------------------------------------------
		glClear(GL_COLOR_BUFFER_BIT);
		glUseProgram(program);

		// player (green / yellow if game over)
		glBindVertexArray(playerMesh.vao);
		if (!gameOver) glUniform3f(locColor, 0.0f, 1.0f, 0.0f);
		else           glUniform3f(locColor, 1.0f, 1.0f, 0.0f);
		glUniform2f(locOffset, playerX, PLAYER_Y);
		glDrawArrays(GL_TRIANGLES, 0, playerMesh.vertexCount);

		// spikes (red)
		glBindVertexArray(spikeMesh.vao);
		glUniform3f(locColor, 1.0f, 0.0f, 0.0f);
		for (const auto& o : obstacles)
		{
			glUniform2f(locOffset, o.x, o.y);
			glDrawArrays(GL_TRIANGLES, 0, spikeMesh.vertexCount);
		}

		// -------------------------------------------------
// ImGui frame
// -------------------------------------------------
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// HUD (좌상단)
		{
			ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.35f);
			ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoSavedSettings;
			ImGui::Begin("HUD", nullptr, flags);
			ImGui::Text("Score: %.1f", score);
			ImGui::Text("Best : %.1f", bestScore);
			ImGui::Text("Spawn: %.2fs", spawnInterval);
			ImGui::Text("Speed: %.2f", spikeSpeed);
			ImGui::End();
		}

		// GAME OVER (가운데 크게)
		if (gameOver)
		{
			ImGuiViewport* vp = ImGui::GetMainViewport();
			ImVec2 center = vp->GetCenter();

			ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowBgAlpha(0.0f);

			ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoSavedSettings;

			ImGui::Begin("GAMEOVER", nullptr, flags);
			ImGui::SetWindowFontScale(3.0f);
			ImGui::Text("GAME OVER");
			ImGui::SetWindowFontScale(1.2f);
			ImGui::Text("Score: %.1f   Best: %.1f", score, bestScore);
			ImGui::Text("Press R to Restart");
			ImGui::End();
		}

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());


		glfwSwapBuffers(window);
		glfwPollEvents();
	}
#pragma endregion


#pragma region Shutdown
	glDeleteProgram(program);

	DestroyMesh(playerMesh);
	DestroyMesh(spikeMesh);

	glfwDestroyWindow(window);
	glfwTerminate();

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

#pragma endregion

	return 0;
}
