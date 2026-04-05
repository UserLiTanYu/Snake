#ifndef ANDROIDGLINVESTIGATIONS_SHADER_H
#define ANDROIDGLINVESTIGATIONS_SHADER_H

#include <string>
#include <GLES3/gl3.h>

class Model;

class Shader {
public:
    static Shader *loadShader(
            const std::string &vertexSource,
            const std::string &fragmentSource,
            const std::string &positionAttributeName,
            const std::string &uvAttributeName,
            const std::string &projectionMatrixUniformName);

    inline ~Shader() {
        if (program_) {
            glDeleteProgram(program_);
            program_ = 0;
        }
    }

    void activate() const;
    void deactivate() const;
    void drawModel(const Model &model) const;
    void setProjectionMatrix(float *projectionMatrix) const;

    inline GLuint getProgram() const { return program_; }
    inline GLint getPosition() const { return position_; }
    inline GLint getUV() const { return uv_; }

private:
    static GLuint loadShader(GLenum shaderType, const std::string &shaderSource);

    constexpr Shader(
            GLuint program,
            GLint position,
            GLint uv,
            GLint projectionMatrix)
            : program_(program),
              position_(position),
              uv_(uv),
              projectionMatrix_(projectionMatrix) {}

    GLuint program_;
    GLint position_;
    GLint uv_;
    GLint projectionMatrix_;
};

#endif //ANDROIDGLINVESTIGATIONS_SHADER_H
