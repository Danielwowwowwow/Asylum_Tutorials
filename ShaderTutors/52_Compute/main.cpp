//*************************************************************************************************************
#include <Windows.h>
#include <GdiPlus.h>
#include <iostream>

#include "../common/gl4x.h"

// helper macros
#define TITLE				"Shader sample 52: Hello compute!"
#define MYERROR(x)			{ std::cout << "* Error: " << x << "!\n"; }

// external variables
extern HWND		hwnd;
extern HDC		hdc;
extern long		screenwidth;
extern long		screenheight;

// sample variables
OpenGLMesh*		mesh			= 0;
OpenGLEffect*	effect			= 0;
OpenGLEffect*	coloredtexture	= 0;	// compute shader creating a checker pattern
GLuint			texture			= 0;

bool InitScene()
{
	SetWindowText(hwnd, TITLE);
	Quadron::qGLExtensions::QueryFeatures();

	if( !Quadron::qGLExtensions::ARB_compute_shader || !Quadron::qGLExtensions::ARB_shader_image_load_store )
		return false;

	glClearColor(0.0f, 0.125f, 0.3f, 1.0f);
	glClearDepth(1.0);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glDepthFunc(GL_LEQUAL);
	glEnable(GL_DEPTH_TEST);

	bool ok = GLCreateMeshFromQM("../media/meshes/teapot.qm", &mesh);

	if( !ok )
	{
		MYERROR("Could not load mesh");
		return false;
	}

	// texture
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, 0);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	// effect
	if( !(ok = GLCreateEffectFromFile("../media/shadersGL/simplelight.vert", 0, "../media/shadersGL/simplelight.frag", &effect)) )
	{
		MYERROR("Could not load effect");
		return false;
	}

	// compute shader
	if( !(ok = GLCreateComputeProgramFromFile("../media/shadersGL/coloredtexture.comp", &coloredtexture)) )
	{
		MYERROR("Could not load compute shader");
		return false;
	}

	coloredtexture->SetInt("img", 0);

	return true;
}
//*************************************************************************************************************
void UninitScene()
{
	if( effect )
		delete effect;

	if( coloredtexture )
		delete coloredtexture;

	if( texture )
		glDeleteTextures(1, &texture);

	if( mesh )
		delete mesh;

	effect = 0;
	coloredtexture = 0;
	mesh = 0;
	texture = 0;

	GLKillAnyRogueObject();
}
//*************************************************************************************************************
void Event_KeyDown(unsigned char keycode)
{
}
//*************************************************************************************************************
void Event_KeyUp(unsigned char keycode)
{
}
//*************************************************************************************************************
void Event_MouseMove(int x, int y, short dx, short dy)
{
}
//*************************************************************************************************************
void Event_MouseDown(int x, int y, unsigned char button)
{
}
//*************************************************************************************************************
void Event_MouseUp(int x, int y, unsigned char button)
{
}
//*************************************************************************************************************
void Update(float delta)
{
}
//*************************************************************************************************************
void Render(float alpha, float elapsedtime)
{
	static float time = 0;

	float lightpos[4]	= { 6, 3, 10, 1 };
	float eye[3]		= { 0, 0, 3 };
	float look[3]		= { 0, 0, 0 };
	float up[3]			= { 0, 1, 0 };

	float view[16];
	float proj[16];
	float world[16];
	float viewproj[16];
	float tmp1[16];
	float tmp2[16];

	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	GLMatrixLookAtRH(view, eye, look, up);
	GLMatrixPerspectiveFovRH(proj, (60.0f * 3.14159f) / 180.f,  (float)screenwidth / (float)screenheight, 0.1f, 100.0f);
	
	GLMatrixMultiply(viewproj, view, proj);

	//calculate world matrix
	GLMatrixIdentity(tmp2);
	
	tmp2[12] = -0.108f;		// offset with bb center
	tmp2[13] = -0.7875f;	// offset with bb center

	GLMatrixRotationAxis(tmp1, fmodf(time * 20.0f, 360.0f) * (3.14152f / 180.0f), 1, 0, 0);
	GLMatrixMultiply(world, tmp2, tmp1);

	GLMatrixRotationAxis(tmp2, fmodf(time * 20.0f, 360.0f) * (3.14152f / 180.0f), 0, 1, 0);
	GLMatrixMultiply(world, world, tmp2);

	// render
	time += elapsedtime;

	// update texture
	glBindImageTexture(0, texture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

	coloredtexture->SetFloat("time", time);
	coloredtexture->Begin();
	{
		glDispatchCompute(8, 8, 1);
	}
	coloredtexture->End();

	effect->SetVector("eyePos", eye);
	effect->SetVector("lightPos", lightpos);
	effect->SetMatrix("matWorld", world);
	effect->SetMatrix("matViewProj", viewproj);
	effect->SetInt("sampler0", 0);

	effect->Begin();
	{
		glBindTexture(GL_TEXTURE_2D, texture);
		mesh->DrawSubset(0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	effect->End();

	// check errors
	GLenum err = glGetError();

	if( err != GL_NO_ERROR )
		std::cout << "Error\n";

	SwapBuffers(hdc);
}
//*************************************************************************************************************
