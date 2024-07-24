#include <fmt/core.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <sndfile.h>

const float WINDOW_WIDTH = 1080.0f;
const float WINDOW_HEIGHT = 1920.0f;
const float WALL_MARGIN = 100.0f;
const float SIMULATION_SPEED = 0.5f;
const float WALL_RADIUS = 0.9f;

struct Ball {
    float x, y;  // World space coordinates
    float sx, sy;  // Screen space coordinates
    float dx, dy;
    float radius;
    float r, g, b;  // Color
    float addedMomentum;
};

class SoundPlayer {
private:
    ALCdevice* device;
    ALCcontext* context;
    ALuint source;
    ALuint buffer;

public:
    SoundPlayer(const char* filename) : device(nullptr), context(nullptr), source(0), buffer(0) {
        try {
            device = alcOpenDevice(nullptr);
            if (!device) {
                throw std::runtime_error("Failed to open OpenAL device");
            }

            context = alcCreateContext(device, nullptr);
            if (!context) {
                throw std::runtime_error("Failed to create OpenAL context");
            }

            if (!alcMakeContextCurrent(context)) {
                throw std::runtime_error("Failed to make OpenAL context current");
            }

            SF_INFO sfinfo;
            SNDFILE* sndfile = sf_open(filename, SFM_READ, &sfinfo);
            if (!sndfile) {
                throw std::runtime_error(fmt::format("Failed to open sound file: {}", filename));
            }

            std::vector<short> samples(sfinfo.frames * sfinfo.channels);
            sf_count_t count = sf_read_short(sndfile, samples.data(), samples.size());
            if (count < 1) {
                sf_close(sndfile);
                throw std::runtime_error("Failed to read sound file data");
            }
            sf_close(sndfile);

            alGenBuffers(1, &buffer);
            if (alGetError() != AL_NO_ERROR) {
                throw std::runtime_error("Failed to generate OpenAL buffer");
            }

            alBufferData(buffer, sfinfo.channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
                samples.data(), samples.size() * sizeof(short), sfinfo.samplerate);
            if (alGetError() != AL_NO_ERROR) {
                throw std::runtime_error("Failed to fill OpenAL buffer");
            }

            alGenSources(1, &source);
            if (alGetError() != AL_NO_ERROR) {
                throw std::runtime_error("Failed to generate OpenAL source");
            }

            alSourcei(source, AL_BUFFER, buffer);
            if (alGetError() != AL_NO_ERROR) {
                throw std::runtime_error("Failed to attach buffer to source");
            }
        }
        catch (const std::exception& e) {
            cleanup();
            throw;
        }
    }

    void play() {
        alSourcePlay(source);
        if (alGetError() != AL_NO_ERROR) {
            std::cerr << "Failed to play sound" << std::endl;
        }
    }

    ~SoundPlayer() {
        cleanup();
    }

private:
    void cleanup() {
        if (source) {
            alDeleteSources(1, &source);
        }
        if (buffer) {
            alDeleteBuffers(1, &buffer);
        }
        if (context) {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context);
        }
        if (device) {
            alcCloseDevice(device);
        }
    }
};

void worldToScreen(float wx, float wy, float& sx, float& sy, float aspectRatio) {
    sx = wx / aspectRatio;
    sy = wy;
}

void screenToWorld(float sx, float sy, float& wx, float& wy, float aspectRatio) {
    wx = sx * aspectRatio;
    wy = sy;
}

Ball createRandomBall(float aspectRatio) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> pos(-WALL_RADIUS + 0.1f, WALL_RADIUS - 0.1f);
    std::uniform_real_distribution<float> vel(-0.25f, 0.25f);

    Ball ball;
    ball.radius = 0.05f;

    do {
        ball.x = pos(gen);
        ball.y = pos(gen);
        worldToScreen(ball.x, ball.y, ball.sx, ball.sy, aspectRatio);
    } while (std::sqrt(ball.sx * ball.sx + ball.sy * ball.sy) > WALL_RADIUS - ball.radius);

    ball.dx = vel(gen);
    ball.dy = vel(gen);
    ball.addedMomentum = 1.05f;

    return ball;
}

void updateBalls(std::vector<Ball>& balls, float deltaTime, SoundPlayer& soundPlayer, float aspectRatio) {
    float adjustedDeltaTime = deltaTime * SIMULATION_SPEED;
    std::vector<Ball> newBalls;
    const size_t MAX_BALLS = 1000;
    const float MOMENTUM_INCREMENT = 0.05f;
    const float MAX_ADDED_MOMENTUM = 5.0f;
    const float CENTER_BIAS = 0.5f;
    const float RANDOM_FACTOR = 0.4f;

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(-1.0f, 1.0f);

    for (size_t i = 0; i < balls.size(); ++i) {
        Ball& ball = balls[i];

        ball.dy -= 1.4f * adjustedDeltaTime;

        ball.x += ball.dx * adjustedDeltaTime;
        ball.y += ball.dy * adjustedDeltaTime;
        worldToScreen(ball.x, ball.y, ball.sx, ball.sy, aspectRatio);

        float distanceFromCenter = std::sqrt(ball.sx * ball.sx + ball.sy * ball.sy);
        if (distanceFromCenter + ball.radius > WALL_RADIUS) {
            soundPlayer.play();

            float angle = std::atan2(ball.sy, ball.sx);
            ball.sx = (WALL_RADIUS - ball.radius) * std::cos(angle);
            ball.sy = (WALL_RADIUS - ball.radius) * std::sin(angle);
            screenToWorld(ball.sx, ball.sy, ball.x, ball.y, aspectRatio);

            float nx = ball.sx / distanceFromCenter;
            float ny = ball.sy / distanceFromCenter;

            float dotProduct = ball.dx * nx + ball.dy * ny;

            float rx = ball.dx - 2 * dotProduct * nx;
            float ry = ball.dy - 2 * dotProduct * ny;

            float centerX = -ball.sx / distanceFromCenter;
            float centerY = -ball.sy / distanceFromCenter;

            float randX = dis(gen) * RANDOM_FACTOR;
            float randY = dis(gen) * RANDOM_FACTOR;

            ball.dx = rx * (1 - CENTER_BIAS) + centerX * CENTER_BIAS + randX;
            ball.dy = ry * (1 - CENTER_BIAS) + centerY * CENTER_BIAS + randY;

            float speed = std::sqrt(ball.dx * ball.dx + ball.dy * ball.dy);
            ball.dx /= speed;
            ball.dy /= speed;

            ball.addedMomentum = std::min(ball.addedMomentum + MOMENTUM_INCREMENT, MAX_ADDED_MOMENTUM);

            float totalMomentum = 1.05f + ball.addedMomentum;
            ball.dx *= totalMomentum;
            ball.dy *= totalMomentum;

            if (balls.size() + newBalls.size() < MAX_BALLS) {
                Ball newBall = ball;
                newBall.dx *= 0.95f;
                newBall.dy *= 0.95f;
                newBall.addedMomentum = 1.05f;
                newBalls.push_back(newBall);
            }
        }

        float normalizedDistance = distanceFromCenter / WALL_RADIUS;
        if (normalizedDistance < 0.33f) {
            ball.r = 1.0f;
            ball.g = normalizedDistance * 3.0f;
            ball.b = 0.0f;
        }
        else if (normalizedDistance < 0.66f) {
            ball.r = 1.0f - (normalizedDistance - 0.33f) * 3.0f;
            ball.g = 1.0f;
            ball.b = (normalizedDistance - 0.33f) * 3.0f;
        }
        else {
            ball.r = (normalizedDistance - 0.66f) * 3.0f;
            ball.g = 1.0f - (normalizedDistance - 0.66f) * 3.0f;
            ball.b = 1.0f;
        }

        float maxSpeed = 2.5f;
        float currentSpeed = std::sqrt(ball.dx * ball.dx + ball.dy * ball.dy);
        if (currentSpeed > maxSpeed) {
            ball.dx = (ball.dx / currentSpeed) * maxSpeed;
            ball.dy = (ball.dy / currentSpeed) * maxSpeed;
        }
    }

    balls.insert(balls.end(), newBalls.begin(), newBalls.end());
}

std::vector<float> createCircleVertices(float radius, int segments) {
    std::vector<float> vertices;
    for (int i = 0; i <= segments; i++) {
        float theta = 2.0f * 3.1415926f * float(i) / float(segments);
        float x = radius * cosf(theta);
        float y = radius * sinf(theta);
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(0.0f);
    }
    return vertices;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window, std::vector<Ball>& balls, float aspectRatio) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    static bool spacePressed = false;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        if (!spacePressed) {
            balls.push_back(createRandomBall(aspectRatio));
            spacePressed = true;
        }
    }
    else {
        spacePressed = false;
    }
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(static_cast<int>(WINDOW_WIDTH), static_cast<int>(WINDOW_HEIGHT), "Balls in Circular Wall", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glViewport(0, 0, static_cast<int>(WINDOW_WIDTH), static_cast<int>(WINDOW_HEIGHT));
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    float aspectRatio = WINDOW_WIDTH / WINDOW_HEIGHT;
    std::vector<Ball> balls;
    balls.push_back(createRandomBall(aspectRatio));

    std::vector<float> ballVertices = createCircleVertices(0.05f, 32);
    std::vector<float> wallVertices = createCircleVertices(WALL_RADIUS, 100);

    unsigned int VBO[3], VAO[3];
    glGenVertexArrays(3, VAO);
    glGenBuffers(3, VBO);

    glBindVertexArray(VAO[0]);
    glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
    glBufferData(GL_ARRAY_BUFFER, ballVertices.size() * sizeof(float), ballVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(VAO[1]);
    glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
    glBufferData(GL_ARRAY_BUFFER, wallVertices.size() * sizeof(float), wallVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(VAO[2]);
    glBindBuffer(GL_ARRAY_BUFFER, VBO[2]);
    glBufferData(GL_ARRAY_BUFFER, wallVertices.size() * sizeof(float), wallVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    const char* vertexShaderSource = R"glsl(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        uniform vec2 offset;
        void main()
        {
            gl_Position = vec4(aPos.x + offset.x, aPos.y + offset.y, aPos.z, 1.0);
        }
    )glsl";

    const char* fragmentShaderSource = R"glsl(
        #version 330 core
        out vec4 FragColor;
        uniform vec3 color;
        void main()
        {
            FragColor = vec4(color, 1.0);
        }
    )glsl";

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    int offsetLoc = glGetUniformLocation(shaderProgram, "offset");
    int colorLoc = glGetUniformLocation(shaderProgram, "color");

    float lastFrame = 0.0f;

    SoundPlayer soundPlayer("ballsound.wav");

    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window, balls, aspectRatio);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // Draw black background (filled circle)
        glUniform2f(offsetLoc, 0.0f, 0.0f);
        glUniform3f(colorLoc, 0.0f, 0.0f, 0.0f);
        glBindVertexArray(VAO[2]);
        glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(wallVertices.size()) / 3);

        // Draw wall (white outline)
        glUniform2f(offsetLoc, 0.0f, 0.0f);
        glUniform3f(colorLoc, 1.0f, 1.0f, 1.0f);
        glBindVertexArray(VAO[1]);
        glDrawArrays(GL_LINE_LOOP, 0, static_cast<GLsizei>(wallVertices.size()) / 3);

        // Update and draw balls
        updateBalls(balls, deltaTime, soundPlayer, aspectRatio);
        for (const auto& ball : balls) {
            glUniform2f(offsetLoc, ball.sx, ball.sy);
            glUniform3f(colorLoc, ball.r, ball.g, ball.b);
            glBindVertexArray(VAO[0]);
            glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(ballVertices.size()) / 3);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(3, VAO);
    glDeleteBuffers(3, VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}