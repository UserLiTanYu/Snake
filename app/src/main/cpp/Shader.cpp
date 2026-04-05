#include "Shader.h"
#include "AndroidOut.h"
#include "Model.h"
#include "Utility.h"

Shader *Shader::loadShader(
        const std::string &vertexSource,
        const std::string &fragmentSource,
        const std::string &positionAttributeName,
        const std::string &uvAttributeName,
        const std::string &projectionMatrixUniformName) {
    Shader *shader = nullptr;

    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader) return nullptr;

    GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return nullptr;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            if (logLength) {
                GLchar *log = new GLchar[logLength];
                glGetProgramInfoLog(program, logLength, nullptr, log);
                aout << "Failed to link program: " << log << std::endl;
                delete[] log;
            }
            glDeleteProgram(program);
        } else {
            GLint positionAttribute = glGetAttribLocation(program, positionAttributeName.c_str());
            GLint uvAttribute = glGetAttribLocation(program, uvAttributeName.c_str());
            GLint projectionMatrixUniform = glGetUniformLocation(program, projectionMatrixUniformName.c_str());

            // 修复：即便找不到 UV 属性（返回 -1），只要位置和投影矩阵在，就允许创建
            if (positionAttribute != -1 && projectionMatrixUniform != -1) {
                shader = new Shader(program, positionAttribute, uvAttribute, projectionMatrixUniform);
            } else {
                aout << "Required attributes not found in shader" << std::endl;
                glDeleteProgram(program);
            }
        }
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return shader;
}

GLuint Shader::loadShader(GLenum shaderType, const std::string &shaderSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        auto *shaderRawString = (GLchar *) shaderSource.c_str();
        GLint shaderLength = shaderSource.length();
        glShaderSource(shader, 1, &shaderRawString, &shaderLength);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLength);
            if (infoLength) {
                auto *infoLog = new GLchar[infoLength];
                glGetShaderInfoLog(shader, infoLength, nullptr, infoLog);
                aout << "Shader compile error: " << infoLog << std::endl;
                delete[] infoLog;
            }
            glDeleteShader(shader);
            return 0;
        }
    }
    return shader;
}

void Shader::activate() const { glUseProgram(program_); }
void Shader::deactivate() const { glUseProgram(0); }

void Shader::drawModel(const Model &model) const {
    glVertexAttribPointer(position_, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), model.getVertexData());
    glEnableVertexAttribArray(position_);

    if (uv_ != -1) {
        glVertexAttribPointer(uv_, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
            ((uint8_t *) model.getVertexData()) + sizeof(Vector3));
        glEnableVertexAttribArray(uv_);
    }

    // 只有在 uv 存在时才绑定纹理
    // if (uv_ != -1) { ... }

    glDrawElements(GL_TRIANGLES, model.getIndexCount(), GL_UNSIGNED_SHORT, model.getIndexData());
    if (uv_ != -1) glDisableVertexAttribArray(uv_);
    glDisableVertexAttribArray(position_);
}

void Shader::setProjectionMatrix(float *projectionMatrix) const {
    glUniformMatrix4fv(projectionMatrix_, 1, false, projectionMatrix);
}
