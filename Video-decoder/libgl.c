#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <locale.h>
#include <sys/time.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include  <X11/Xatom.h>

#ifndef USEGLES2
#	define USEGLX
#endif

#ifdef USEGLX
#	define GL_GLEXT_PROTOTYPES
#	include <GL/glx.h>
#	include <GL/gl.h>
#	include <GL/glext.h>
static GLXContext ctx;
#else
#	include <EGL/egl.h>
#	ifdef USEGLES2
#		include <GLES2/gl2.h>
#	else
#		include <GLES3/gl3.h>
#	endif
static EGLDisplay display;
static EGLContext context;
static EGLSurface surface;
#endif

static Window win;
static Display *dpy;
static char logging = 1;
static GLuint program[3];
static GLint positionLoc[3];
static GLint texCoordLoc[3];
static GLint samplerLoc[3];
static GLint texturewidthLoc;
static int winw, winh;

enum {FMT_RGBA, FMT_YUYV, FMT_NV12};

static double seconds()
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec * 1e-6;
}

static void lprintf(const char *fmt, ...)
{
	if(!logging)
		return;
	char s[300];
	static double start;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	if(!start)
		start = seconds();
	printf("%f: %s", seconds() - start, s);
	va_end(ap);
}

static void eprintf(const char *fmt, ...)
{
	char s[300];
	static double start;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	if(!start)
		start = seconds();
	printf("%f: %s", seconds() - start, s);
	va_end(ap);
}

static void *cmalloc(size_t nbytes)
{
	void *buf = malloc(nbytes);
	if(buf)
		return buf;
	eprintf("malloc of %u bytes failed\n", nbytes);
	return 0;
}

static int InitGL(const char *title, int x, int y, int w, int h)
{
	int fullscreen = 0;

	if(!dpy)
	{
		lprintf("Cannot open display, trying :0\n");
		dpy = XOpenDisplay(":0");
		if(!dpy)
		{
			eprintf("Cannot open display\n");
			return -1;
		}
	}
	if(!w || !h)
	{
		Screen *screen = XDefaultScreenOfDisplay(dpy);
		x = y = 0;
		w = XWidthOfScreen(screen);
		h = XHeightOfScreen(screen);
		fullscreen = 1;
	}
	winw = w;
	winh = h;
	XSetWindowAttributes swa;
	swa.border_pixel = 0;
	win = XCreateWindow(dpy, DefaultRootWindow(dpy), x, y, w, h, 0, CopyFromParent, InputOutput, CopyFromParent, CWBorderPixel, &swa);
	if(!win)
	{
		eprintf("Error creating X Window\n");
		return -1;
	}

	if(fullscreen)
	{
		Atom atom = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", True);
		XChangeProperty (dpy, win, XInternAtom (dpy, "_NET_WM_STATE", True ), XA_ATOM, 32, PropModeReplace, (unsigned char*)&atom,  1);
	}
	XMapWindow(dpy, win);
	XStoreName(dpy, win, title);
	XFlush(dpy);
	return 0;
}

static int StartGL()
{
#ifdef USEGLX
	static int attributeList[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None };
	XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attributeList);
	ctx = glXCreateContext(dpy, vi, 0, GL_TRUE);
	glXMakeCurrent (dpy, win, ctx);
	lprintf("OpenGL %s initialized\n", glGetString(GL_VERSION));
#else
	const EGLint confAttr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE
	};
	EGLint contextAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	// Size here doesn't matter, we will draw to an offscreen buffer
	EGLConfig config;
	EGLint majorVersion, minorVersion, numConfigs;

	display = eglGetDisplay(dpy);
	if(display == EGL_NO_DISPLAY)
	{
		eprintf("No display: 0x%x\n", eglGetError());
		return -1;
	}
	if(!eglInitialize(display, &majorVersion, &minorVersion))
	{
		eprintf("eglInitialize failed: 0x%x\n", eglGetError());
		return -1;
	}
	lprintf("eglInitialize succeeded, version=%d.%d\n", majorVersion, minorVersion);
	if(!eglGetConfigs(display, NULL, 0, &numConfigs))
	{
		eprintf("eglGetConfigs failed: 0x%x\n", eglGetError());
		return -1;
	}
	if(!eglChooseConfig(display, confAttr, &config, 1, &numConfigs))
	{
		eprintf("eglChooseConfig failed: 0x%x\n", eglGetError());
		return -1;
	}
	if(!numConfigs)
	{
		eprintf("eglChooseConfig returned no valid configuration\n");
		return -1;
	}
	context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttr);
	if(context == EGL_NO_CONTEXT)
	{
		eprintf("eglCreateContext failed: 0x%x\n", eglGetError());
		return -1;
	}
	surface = eglCreateWindowSurface (display, config, win, 0);
	if(surface == EGL_NO_SURFACE)
	{
		eprintf("eglCreateWindowSurface failed: 0x%x\n", eglGetError());
		return -1;
	}
	if(!eglMakeCurrent(display, surface, surface, context))
	{
		eprintf("eglMakeCurrent failed: 0x%x\n", eglGetError());
		return -1;
	}
	lprintf("%s initialized\n", glGetString(GL_VERSION));
#endif
	glClearColor (0, 0, 0, 1);
	glClear (GL_COLOR_BUFFER_BIT);
	return 0;
}

void CloseWindow()
{
#ifdef USEGLX
	ctx = glXGetCurrentContext();
	glXDestroyContext(dpy, ctx);
#else
	eglMakeCurrent(display, surface, surface, EGL_NO_CONTEXT);
	eglDestroyContext(display, context);
	eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(display, surface);
	eglTerminate(display);
#endif
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
}

int checkGlError(const char *op)
{
	int rc = 0, error;
	while ((error = glGetError()) != GL_NO_ERROR) {
		eprintf("%s: Error 0x%x\n", op, error);
		rc = error;
	}
	return rc;
}

static GLuint esLoadShader (GLenum type, const char *shaderSrc)
{
	GLuint shader;
	GLint compiled;

	shader = glCreateShader(type);
	if(!shader)
		return 0;
	glShaderSource(shader, 1, &shaderSrc, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if(!compiled) 
	{
		GLint infoLen = 0;
		glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &infoLen);
		if(infoLen > 1)
		{
			char* infoLog = cmalloc(sizeof(char) * infoLen);
			glGetShaderInfoLog (	shader, infoLen, NULL, infoLog);
			eprintf("Error compiling %s shader:\n%s\n",
				type == GL_VERTEX_SHADER ? "vertex" : "fragment", infoLog);
			free (infoLog);
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint esLoadProgram(const char *vertShaderSrc, const char *fragShaderSrc)
{
	GLuint vertexShader;
	GLuint fragmentShader;
	GLuint programObject;
	GLint linked;

	// Load the vertex/fragment shaders
	vertexShader = esLoadShader(GL_VERTEX_SHADER, vertShaderSrc);
	if(!vertexShader)
		return 0;
	fragmentShader = esLoadShader (GL_FRAGMENT_SHADER, fragShaderSrc);
	if(!fragmentShader)
	{
		glDeleteShader(vertexShader);
		return 0;
	}
	programObject = glCreateProgram();
	if(!programObject)
		return 0;
	glAttachShader(programObject, vertexShader);
	glAttachShader(programObject, fragmentShader);
	glLinkProgram(programObject);
	glGetProgramiv(programObject, GL_LINK_STATUS, &linked);
	if(!linked) 
	{
		GLint infoLen = 0;
		glGetProgramiv(programObject, GL_INFO_LOG_LENGTH, &infoLen);
		if(infoLen > 1)
		{
			char *infoLog = cmalloc(sizeof(char) * infoLen);
		
			glGetProgramInfoLog(programObject, infoLen, NULL, infoLog);
			eprintf("Error linking program:\n%s\n", infoLog);
			free (infoLog);
		}
		glDeleteProgram(programObject);
		return 0;
	}
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	return programObject;
}

static int CreateShaders(int fmt)
{
	const char *VertexShaderGLSL1 =
		"attribute vec4 aPosition;\n"
		"attribute vec2 aTextureCoord;\n"
		"varying vec2 vTextureCoord;\n" 
		"void main()\n"
		"{\n" 
			"gl_Position = aPosition;\n" 
			"vTextureCoord = aTextureCoord;\n" 
		"}\n";
	const char *FragmentShaderGLSL1_RGBA =
		"precision mediump float;\n"
		"varying vec2 vTextureCoord;\n"
		"uniform sampler2D sTexture;\n"
		"void main()\n"
		"{\n"
		"    gl_FragColor = texture2D(sTexture, vTextureCoord);\n"
		"}\n";
	const char *FragmentShaderGLSL1_YUYV =
		"precision mediump float;\n"
		"varying vec2 vTextureCoord;\n"
		"uniform sampler2D sTexture;\n"
		"uniform float texturewidth;\n"
		"void main()\n"
		"{\n"
		"    vec4 c = texture2D(sTexture, vTextureCoord);\n"
		"    c = c + vec4(-0.0625, -0.5, -0.0625, -0.5);\n"
		"    float x = floor(vTextureCoord.x * texturewidth);\n"
		"    if(floor(x * 0.5) * 2.0 != floor(x))\n"
		"        gl_FragColor = vec4(1.164 * c.b + 1.326*c.a, 1.164 * c.b - 0.459*c.g - 0.674*c.a, 1.164 * c.b + 2.364*c.g, 1.0);\n" 
		"    else gl_FragColor = vec4(1.164 * c.r + 1.326*c.a, 1.164 * c.r - 0.459*c.g - 0.674*c.a, 1.164 * c.r + 2.364*c.g, 1.0);\n"
		"}\n";
	const char *FragmentShaderGLSL1_NV12 =
		"precision mediump float;\n"
		"varying vec2 vTextureCoord;\n"
		"uniform sampler2D sTextureY;\n"
		"uniform sampler2D sTextureU;\n"
		"uniform sampler2D sTextureV;\n"
		"void main()\n"
		"{\n"
		"    vec3 yuv;\n"
		"    yuv.x = texture2D(sTextureY, vTextureCoord).r;\n"
		"    yuv.y = texture2D(sTextureU, vTextureCoord).r;\n"
		"    yuv.z = texture2D(sTextureV, vTextureCoord).r;\n"
		"    yuv = yuv + vec3(0, -0.5, -0.5);\n"
		"    gl_FragColor = vec4(yuv.x + 1.14*yuv.z, yuv.x - 0.395*yuv.y - 0.581*yuv.z, yuv.x + 2.032*yuv.y, 1.0);\n"
		"}\n";

	program[fmt] = esLoadProgram(VertexShaderGLSL1, fmt == FMT_YUYV ? FragmentShaderGLSL1_YUYV : fmt == FMT_NV12 ? FragmentShaderGLSL1_NV12 : FragmentShaderGLSL1_RGBA);
	if(!program[fmt])
	{
		eprintf("esLoadProgram failed\n");
		return -1;
	}

	// Get the attribute locations
	positionLoc[fmt] = glGetAttribLocation(program[fmt], "aPosition");
	texCoordLoc[fmt] = glGetAttribLocation(program[fmt], "aTextureCoord");
	if(fmt == FMT_NV12)
	{
		samplerLoc[0] = glGetUniformLocation(program[fmt], "sTextureY");
		samplerLoc[1] = glGetUniformLocation(program[fmt], "sTextureU");
		samplerLoc[2] = glGetUniformLocation(program[fmt], "sTextureV");
	} else if(fmt == FMT_YUYV)
		texturewidthLoc = glGetUniformLocation(program[fmt], "texturewidth");
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	checkGlError("glClearColor");
	return 0;
}

static int CreateTextures(int fmt, int framew, int frameh, GLubyte *frame, GLuint *textureIds)
{
	// Use tightly packed data
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	// Generate a texture object
	if(fmt == FMT_NV12)
	{
		glGenTextures(3, textureIds);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureIds[0]);
		glTexImage2D( GL_TEXTURE_2D, 0, GL_LUMINANCE, framew, frameh, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frame);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// Convert interleaved U/V to planar UV
		frame += framew * frameh;
		framew /= 2;
		frameh /= 2;
		GLubyte *v, *u = (GLubyte *)malloc(framew * frameh * 2);
		v = u + framew * frameh;
		int i, j;
		for(i = 0; i < frameh; i++)
			for(j = 0; j < framew; j++)
			{
				u[i * framew + j] = frame[2 * (i * framew + j)];
				v[i * framew + j] = frame[2 * (i * framew + j) + 1];
			}
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, textureIds[1]);
		glTexImage2D( GL_TEXTURE_2D, 0, GL_LUMINANCE, framew, frameh, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, u);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, textureIds[2]);
		glTexImage2D( GL_TEXTURE_2D, 0, GL_LUMINANCE, framew, frameh, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, v);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		free(u);
		return 3;
	} else {
		glGenTextures(1, textureIds);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textureIds[0]);
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, framew / (1 + (fmt == FMT_YUYV)), frameh, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		return 1;
	}
}

int Draw(int fmt, int dx, int dy, int dw, int dh)
{
	const GLfloat def_vVertices[] = { -1.0f,  1.0f, 0.0f,  // Position 0
							0.0f,  1.0f,        // TexCoord 0
							-1.0f, -1.0f, 0.0f,  // Position 1
							0.0f,  0.0f,        // TexCoord 1
							1.0f, -1.0f, 0.0f,  // Position 2
							1.0f,  0.0f,        // TexCoord 2
							1.0f,  1.0f, 0.0f,  // Position 3
							1.0f,  1.0f         // TexCoord 3
							};
	GLfloat vVertices[20];

	// Set the filtering mode
	if(checkGlError("start"))
		return -1;
	glViewport(0, 0, winw, winh);
	if(checkGlError("glViewPort"))
		return -1;

	// Use the program object
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(program[fmt]);
	if(checkGlError("glUseProgram"))
		return -1;
	if(fmt == FMT_NV12)
	{
		glUniform1i(samplerLoc[0], 0);
		glUniform1i(samplerLoc[1], 1);
		glUniform1i(samplerLoc[2], 2);
	} else if(fmt == FMT_YUYV)
		glUniform1f(texturewidthLoc, winw);
	if(checkGlError("glUniform1i"))
		return -1;
	memcpy(vVertices, def_vVertices, sizeof(vVertices));
	vVertices[0] = vVertices[5] = dx * 2.0 / winw - 1.0;
	vVertices[6] = vVertices[11] = 1.0 - dy * 2.0 / winh;
	vVertices[10] = vVertices[15] = (dx+dw) * 2.0 / winw - 1.0;
	vVertices[1] = vVertices[16] = 1.0 - (dy+dh) * 2.0 / winh;
	
	// Load the vertex position
	glVertexAttribPointer(positionLoc[fmt], 3, GL_FLOAT,
		GL_FALSE, 5 * sizeof(GLfloat), vVertices);
	if(checkGlError("glVertexAttribPointer1"))
		return -1;
	glVertexAttribPointer(texCoordLoc[fmt], 2, GL_FLOAT,
		GL_FALSE, 5 * sizeof(GLfloat), &vVertices[3]);
	if(checkGlError("glVertexAttribPointer2"))
		return -1;

	glEnableVertexAttribArray(positionLoc[fmt]);
	glEnableVertexAttribArray(texCoordLoc[fmt]);
	if(checkGlError("glEnableVertexAttribArray"))
		return -1;

	GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
	if(checkGlError("glDrawElements"))
		return -1;
	glFinish();
	return 0;
}

int CreateWindow(const char *title, int x, int y, int w, int h)
{
	if(InitGL(title, x, y, w, h))
		return -1;
	return 0;
}

int StartWindow()
{
	if(StartGL())
		return -1;
	if(CreateShaders(0) || CreateShaders(1) || CreateShaders(2))
		return -1;
	return 0;
}

int GetWindowSize(int *w, int *h)
{
	*w = winw;
	*h = winh;
	return 0;
}

int Blt(const void *image, int fmt, int w, int h, int dx, int dy, int dw, int dh)
{
	GLuint textureIds[3];

	int n = CreateTextures(fmt, w, h, (GLubyte *)image, textureIds);
	if(n)
	{
		Draw(fmt, dx, dy, dw, dh);
		glDeleteTextures(n, textureIds);
	}
	return 0;
}

int Present()
{
#ifdef USEGLX
	glXSwapBuffers(dpy, win);
#else
	eglSwapBuffers(display, surface);
#endif
	return 0;
}
