#pragma once

// The GL entry points the backend's thread calls (gb_gl_device.cpp), loaded from the platform's context
// (gb_gl_host.h): the backend links against no GL library. glBufferStorage is loaded apart, only when the context has
// it.

#include <GL/glcorearb.h>

#define GB_GL_FUNCTIONS(X)                                                                                            \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                                                        \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                                                          \
    X(PFNGLBEGINQUERYPROC, glBeginQuery)                                                                              \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                                              \
    X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange)                                                                    \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                                                    \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                                                            \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                                                                            \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                                    \
    X(PFNGLBINDVERTEXBUFFERPROC, glBindVertexBuffer)                                                                  \
    X(PFNGLBLENDCOLORPROC, glBlendColor)                                                                              \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)                                                        \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                                                                \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                                                    \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                                              \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                                                        \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                                                      \
    X(PFNGLCLEARBUFFERFIPROC, glClearBufferfi)                                                                        \
    X(PFNGLCLEARBUFFERFVPROC, glClearBufferfv)                                                                        \
    X(PFNGLCLEARBUFFERIVPROC, glClearBufferiv)                                                                        \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync)                                                                      \
    X(PFNGLCLIPCONTROLPROC, glClipControl)                                                                            \
    X(PFNGLCOLORMASKPROC, glColorMask)                                                                                \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                                        \
    X(PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC, glCompressedTexSubImage2D)                                                    \
    X(PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC, glCompressedTexSubImage3D)                                                    \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                                        \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                                                          \
    X(PFNGLCULLFACEPROC, glCullFace)                                                                                  \
    X(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback)                                                          \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                                                        \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                                                              \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                                                        \
    X(PFNGLDELETEQUERIESPROC, glDeleteQueries)                                                                        \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                                                                      \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                                                          \
    X(PFNGLDELETESYNCPROC, glDeleteSync)                                                                              \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                                                                      \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                                                              \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                                                                                \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                                                                                \
    X(PFNGLDEPTHRANGEFPROC, glDepthRangef)                                                                            \
    X(PFNGLDETACHSHADERPROC, glDetachShader)                                                                          \
    X(PFNGLDISABLEPROC, glDisable)                                                                                    \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                                                              \
    X(PFNGLDRAWBUFFERPROC, glDrawBuffer)                                                                              \
    X(PFNGLDRAWBUFFERSPROC, glDrawBuffers)                                                                            \
    X(PFNGLDRAWELEMENTSBASEVERTEXPROC, glDrawElementsBaseVertex)                                                      \
    X(PFNGLENABLEPROC, glEnable)                                                                                      \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                                    \
    X(PFNGLENDQUERYPROC, glEndQuery)                                                                                  \
    X(PFNGLFENCESYNCPROC, glFenceSync)                                                                                \
    X(PFNGLFINISHPROC, glFinish)                                                                                      \
    X(PFNGLFLUSHPROC, glFlush)                                                                                        \
    X(PFNGLFRAMEBUFFERTEXTUREPROC, glFramebufferTexture)                                                              \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                                          \
    X(PFNGLFRAMEBUFFERTEXTURELAYERPROC, glFramebufferTextureLayer)                                                    \
    X(PFNGLFRONTFACEPROC, glFrontFace)                                                                                \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                                              \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                                                    \
    X(PFNGLGENQUERIESPROC, glGenQueries)                                                                              \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                                                            \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                                                                            \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                                    \
    X(PFNGLGETERRORPROC, glGetError)                                                                                  \
    X(PFNGLGETFLOATVPROC, glGetFloatv)                                                                                \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv)                                                                            \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                                                \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                                          \
    X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v)                                                            \
    X(PFNGLGETQUERYOBJECTUIVPROC, glGetQueryObjectuiv)                                                                \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                                                  \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                                            \
    X(PFNGLGETSTRINGPROC, glGetString)                                                                                \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                                                                              \
    X(PFNGLINVALIDATEFRAMEBUFFERPROC, glInvalidateFramebuffer)                                                        \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                                            \
    X(PFNGLMAPBUFFERRANGEPROC, glMapBufferRange)                                                                      \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)                                                                            \
    X(PFNGLPOLYGONMODEPROC, glPolygonMode)                                                                            \
    X(PFNGLPOLYGONOFFSETPROC, glPolygonOffset)                                                                        \
    X(PFNGLQUERYCOUNTERPROC, glQueryCounter)                                                                          \
    X(PFNGLREADBUFFERPROC, glReadBuffer)                                                                              \
    X(PFNGLREADPIXELSPROC, glReadPixels)                                                                              \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf)                                                                \
    X(PFNGLSAMPLERPARAMETERFVPROC, glSamplerParameterfv)                                                              \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                                                                \
    X(PFNGLSCISSORPROC, glScissor)                                                                                    \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                                          \
    X(PFNGLSTENCILFUNCSEPARATEPROC, glStencilFuncSeparate)                                                            \
    X(PFNGLSTENCILMASKPROC, glStencilMask)                                                                            \
    X(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate)                                                                \
    X(PFNGLTEXPARAMETERIVPROC, glTexParameteriv)                                                                      \
    X(PFNGLTEXSTORAGE2DPROC, glTexStorage2D)                                                                          \
    X(PFNGLTEXSTORAGE2DMULTISAMPLEPROC, glTexStorage2DMultisample)                                                    \
    X(PFNGLTEXSTORAGE3DPROC, glTexStorage3D)                                                                          \
    X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D)                                                                        \
    X(PFNGLTEXSUBIMAGE3DPROC, glTexSubImage3D)                                                                        \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                                                                                \
    X(PFNGLUNMAPBUFFERPROC, glUnmapBuffer)                                                                            \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                                              \
    X(PFNGLVERTEXATTRIBBINDINGPROC, glVertexAttribBinding)                                                            \
    X(PFNGLVERTEXATTRIBFORMATPROC, glVertexAttribFormat)                                                              \
    X(PFNGLVIEWPORTINDEXEDFPROC, glViewportIndexedf)

namespace gb::gl
{
#define GB_GL_DECLARE(type, name) extern type name;
GB_GL_FUNCTIONS(GB_GL_DECLARE)
#undef GB_GL_DECLARE
extern PFNGLBUFFERSTORAGEPROC glBufferStorage;

// False, naming the first entry point the context lacks in `missing`, when one is missing.
bool LoadFunctions(const char **missing);
} // namespace gb::gl
