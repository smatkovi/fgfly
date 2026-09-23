/*
 * Minimal EGL/GLES2 declarations, for building sgxprobe where no development
 * headers are installed (the Jolla has the libraries but not the headers).
 * The device build uses the real headers from the MADDE sysroot; this file is
 * only pulled in with -DSGXPROBE_MIN_HEADERS.
 *
 * Values are from the Khronos registry; signatures are the ES 2.0 / EGL 1.4
 * ones.  Only what sgxprobe.c calls is declared.
 */
#ifndef SGXPROBE_GLES_MIN_H
#define SGXPROBE_GLES_MIN_H

#include <stdint.h>

typedef void *EGLDisplay, *EGLSurface, *EGLContext, *EGLConfig, *EGLNativeDisplayType;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean, EGLenum;

#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NO_DISPLAY      ((EGLDisplay)0)
#define EGL_NO_CONTEXT      ((EGLContext)0)
#define EGL_NO_SURFACE      ((EGLSurface)0)
#define EGL_VERSION               0x3054
#define EGL_SURFACE_TYPE          0x3033
#define EGL_PBUFFER_BIT           0x0001
#define EGL_PIXMAP_BIT            0x0002
#define EGL_BUFFER_SIZE           0x3020
#define EGL_RENDERABLE_TYPE       0x3040
#define EGL_OPENGL_ES2_BIT        0x0004
#define EGL_BLUE_SIZE             0x3022
#define EGL_GREEN_SIZE            0x3023
#define EGL_RED_SIZE              0x3024
#define EGL_DEPTH_SIZE            0x3025
#define EGL_NONE                  0x3038
#define EGL_HEIGHT                0x3056
#define EGL_WIDTH                 0x3057
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API         0x30A0
#define EGL_ALPHA_SIZE            0x3021
#define EGL_STENCIL_SIZE          0x3026

EGLDisplay eglGetDisplay(EGLNativeDisplayType);
EGLBoolean eglInitialize(EGLDisplay, EGLint *, EGLint *);
EGLBoolean eglBindAPI(EGLenum);
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
EGLBoolean eglGetConfigs(EGLDisplay, EGLConfig *, EGLint, EGLint *);
EGLBoolean eglGetConfigAttrib(EGLDisplay, EGLConfig, EGLint, EGLint *);
EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const EGLint *);
typedef void *EGLNativePixmapType;
EGLSurface eglCreatePixmapSurface(EGLDisplay, EGLConfig, EGLNativePixmapType, const EGLint *);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
EGLint     eglGetError(void);
const char *eglQueryString(EGLDisplay, EGLint);

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef char GLchar;

#define GL_FALSE 0
#define GL_TRUE  1
#define GL_VENDOR                 0x1F00
#define GL_RENDERER               0x1F01
#define GL_VERSION                0x1F02
#define GL_EXTENSIONS             0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_MAX_TEXTURE_SIZE       0x0D33
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE 0x851C
#define GL_MAX_RENDERBUFFER_SIZE  0x84E8
#define GL_MAX_VERTEX_ATTRIBS     0x8869
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_MAX_VERTEX_UNIFORM_VECTORS 0x8DFB
#define GL_MAX_VARYING_VECTORS    0x8DFC
#define GL_MAX_FRAGMENT_UNIFORM_VECTORS 0x8DFD
#define GL_SHADER_COMPILER        0x8DFA
#define GL_LOW_FLOAT              0x8DF0
#define GL_MEDIUM_FLOAT           0x8DF1
#define GL_HIGH_FLOAT             0x8DF2
#define GL_FRAGMENT_SHADER        0x8B30
#define GL_VERTEX_SHADER          0x8B31
#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_INFO_LOG_LENGTH        0x8B84

const GLubyte *glGetString(GLenum);
void glGetIntegerv(GLenum, GLint *);
void glGetBooleanv(GLenum, GLboolean *);
void glGetShaderPrecisionFormat(GLenum, GLenum, GLint *, GLint *);
GLuint glCreateShader(GLenum);
void glShaderSource(GLuint, GLsizei, const GLchar *const *, const GLint *);
void glCompileShader(GLuint);
void glGetShaderiv(GLuint, GLenum, GLint *);
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei *, GLchar *);
void glDeleteShader(GLuint);
GLuint glCreateProgram(void);
void glAttachShader(GLuint, GLuint);
void glLinkProgram(GLuint);
void glGetProgramiv(GLuint, GLenum, GLint *);
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei *, GLchar *);
void glDeleteProgram(GLuint);

#endif
