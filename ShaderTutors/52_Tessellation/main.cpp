
#include <Windows.h>
#include <GdiPlus.h>
#include <iostream>
#include <sstream>

#include "../common/gl4x.h"

// TODO:
// - MSAA

// helper macros
#define TITLE				"Shader sample 52: Tessellating NURBS surfaces"
#define MYERROR(x)			{ std::cout << "* Error: " << x << "!\n"; }
#define SAFE_DELETE(x)		if( (x) ) { delete (x); (x) = 0; }

#define MAX_NUM_SEGMENTS	100
#define M_PI				3.141592f

// external variables
extern HWND		hwnd;
extern HDC		hdc;
extern long		screenwidth;
extern long		screenheight;

// sample structures
struct SurfaceVertex
{
	float pos[4];
	float norm[4];
};

struct CurveData
{
	int degree;
	float controlpoints[7][4];
	float weights[7];
	float knots[11];
};

CurveData curves[] =
{
	// degree 3, "good"
	{
		3,
		{ { 1, 1, 0, 1 }, { 1, 5, 0, 1 }, { 3, 6, 0, 1 }, { 6, 3, 0, 1 }, { 9, 4, 0, 1 }, { 9, 9, 0, 1 }, { 5, 6, 0, 1 } },
		{ 1, 1, 1, 1, 1, 1, 1 },
		{ 0, 0, 0, 0, 0.4f, 0.4f, 0.4f, 1, 1, 1, 1 }
	},

	// degree 3, "bad"
	{
		3,
		{ { 1, 1, 0, 1 }, { 1, 5, 0, 1 }, { 3, 6, 0, 1 }, { 6, 3, 0, 1 }, { 9, 4, 0, 1 }, { 9, 9, 0, 1 }, { 5, 6, 0, 1 } },
		{ 1, 1, 1, 1, 1, 1, 1 },
		{ 0, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1 }
	},

	// degree 2, "good"
	{
		2,
		{ { 1, 1, 0, 1 }, { 1, 5, 0, 1 }, { 3, 6, 0, 1 }, { 6, 3, 0, 1 }, { 9, 4, 0, 1 }, { 9, 9, 0, 1 }, { 5, 6, 0, 1 } },
		{ 1, 1, 1, 1, 1, 1, 1 },
		{ 0, 0, 0, 0.2f, 0.4f, 0.6f, 0.8f, 1, 1, 1 }
	},

	// degree 1
	{
		1,
		{ { 1, 1, 0, 1 }, { 1, 5, 0, 1 }, { 3, 6, 0, 1 }, { 6, 3, 0, 1 }, { 9, 4, 0, 1 }, { 9, 9, 0, 1 }, { 5, 6, 0, 1 } },
		{ 1, 1, 1, 1, 1, 1, 1 },
		{ 0, 0, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 1, 1 }
	},

	// circle
	{
		2,
		{ { 5, 1, 0, 1 }, { 1, 1, 0, 1 }, { 3, 4.46f, 0, 1 }, { 5, 7.92f, 0, 1 }, { 7, 4.46f, 0, 1 }, { 9, 1, 0, 1 }, { 5, 1, 0, 1 }, },
		{ 1, 0.5f, 1, 0.5f, 1, 0.5f, 1 },
		{ 0, 0, 0, 0.33f, 0.33f, 0.67f, 0.67f, 1, 1, 1 }
	},
};

// important variables for tessellation
const GLuint numcontrolvertices				= 7;
const GLuint numcontrolindices				= (numcontrolvertices - 1) * 2;
const GLuint maxsplinevertices				= MAX_NUM_SEGMENTS + 1;
const GLuint maxsplineindices				= (maxsplinevertices - 1) * 2;
const GLuint maxsurfacevertices				= maxsplinevertices * maxsplinevertices;
const GLuint maxsurfaceindices				= (maxsplinevertices - 1) * (maxsplinevertices - 1) * 6;

// sample variables
OpenGLScreenQuad*	screenquad				= 0;
OpenGLEffect*		tessellatecurve			= 0;
OpenGLEffect*		tessellatesurface		= 0;
OpenGLEffect*		renderpoints			= 0;
OpenGLEffect*		renderlines				= 0;
OpenGLEffect*		rendersurface			= 0;
OpenGLEffect*		basic2D					= 0;
OpenGLMesh*			supportlines			= 0;
OpenGLMesh*			curve					= 0;
OpenGLMesh*			surface					= 0;

GLuint				currentcurve			= 0;
GLuint				numsegments				= 50;	// tessellation level
GLuint				text1					= 0;
GLsizei				selectedcontrolpoint	= -1;
float				selectiondx, selectiondy;
bool				wireframe				= false;
bool				fullscreen				= false;
bool				hascompute				= false;

int					mousex			= 0;
int					mousey			= 0;
short				mousedx			= 0;
short				mousedy			= 0;
short				mousedown		= 0;

long				splinevpsize	= 0;
long				surfvpwidth		= 0;
long				surfvpheight	= 0;

array_state<float, 2> cameraangle;

// sample functions
bool UpdateControlPoints(float mx, float my);
void ChangeCurve(GLuint newcurve);
void Tessellate();

static void ConvertToSplineViewport(float& x, float& y)
{
	// transform to [0, 10] x [0, 10]
	float unitscale = 10.0f / screenwidth;

	// scale it down and offset with 10 pixels
	float vpscale = (float)splinevpsize / (float)screenwidth;
	float vpoffx = 10.0f;
	float vpoffy = (float)screenheight - (float)splinevpsize - 10.0f;

	x = (x - vpoffx) * unitscale * (1.0f / vpscale);
	y = (y - vpoffy) * unitscale * (1.0f / vpscale);
}

static void UpdateText()
{
	std::stringstream ss;
	const CurveData& current = curves[currentcurve];
	int numcurves = sizeof(curves) / sizeof(curves[0]);

	ss.precision(1);
	ss << "Knot vector is:  { " << current.knots[0];

	for( GLuint i = 1; i < numcontrolvertices + current.degree + 1; ++i )
		ss << ", " << current.knots[i];

	ss << " }\nWeights are:     { " << current.weights[0];

	for( GLuint i = 1; i < numcontrolvertices; ++i )
		ss << ", " << current.weights[i];

	ss << " }\n\n1 - " << numcurves;
	ss << " - presets  W - wireframe  F - full window  +/- tessellation level";
	
	GLRenderTextEx(ss.str(), text1, 800, 130, L"Calibri", false, Gdiplus::FontStyleBold, 25);
}

static void APIENTRY ReportGLError(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userdata)
{
	if( type >= GL_DEBUG_TYPE_ERROR && type <= GL_DEBUG_TYPE_PERFORMANCE )
	{
		if( source == GL_DEBUG_SOURCE_API )
			std::cout << "GL(" << severity << "): ";
		else if( source == GL_DEBUG_SOURCE_SHADER_COMPILER )
			std::cout << "GLSL(" << severity << "): ";
		else
			std::cout << "OTHER(" << severity << "): ";

		std::cout << id << ": " << message << "\n";
	}
}

bool InitScene()
{
	SurfaceVertex*	svdata = 0;
	float			(*vdata)[4] = 0;
	GLushort*		idata = 0;
	GLuint*			idata32 = 0;

	OpenGLVertexElement decl[] =
	{
		{ 0, 0, GLDECLTYPE_FLOAT4, GLDECLUSAGE_POSITION, 0 },
		{ 0xff, 0, 0, 0, 0 }
	};

	OpenGLVertexElement decl2[] =
	{
		{ 0, 0, GLDECLTYPE_FLOAT4, GLDECLUSAGE_POSITION, 0 },
		{ 0, 16, GLDECLTYPE_FLOAT4, GLDECLUSAGE_NORMAL, 0 },
		{ 0xff, 0, 0, 0, 0 }
	};

	SetWindowText(hwnd, TITLE);
	Quadron::qGLExtensions::QueryFeatures(hdc);

	// calculate viewport sizes
	long maxw = screenwidth - 350; // assume 320x240 first
	long maxh = screenheight - 130 - 10; // text is fix
	
	splinevpsize = GLMin<long>(maxw, maxh);
	splinevpsize -= (splinevpsize % 10);

	surfvpwidth = screenwidth - splinevpsize - 30;
	surfvpheight = (surfvpwidth * 3) / 4;

	if( !Quadron::qGLExtensions::ARB_geometry_shader4 )
		return false;

	hascompute = Quadron::qGLExtensions::ARB_compute_shader;

#ifdef _DEBUG
	if( Quadron::qGLExtensions::ARB_debug_output )
	{
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);
		glDebugMessageCallback(ReportGLError, 0);
	}
#endif

	glClearColor(0.0f, 0.125f, 0.3f, 1.0f);
	glClearDepth(1.0);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glDepthFunc(GL_LEQUAL);
	glEnable(GL_DEPTH_TEST);

	// create grid & control poly
	if( !GLCreateMesh(44 + numcontrolvertices, numcontrolindices, GLMESH_DYNAMIC, decl, &supportlines) )
	{
		MYERROR("Could not create mesh");
		return false;
	}

	supportlines->LockVertexBuffer(0, 0, GLLOCK_DISCARD, (void**)&vdata);
	supportlines->LockIndexBuffer(0, 0, GLLOCK_DISCARD, (void**)&idata);

	// grid points
	for( GLuint i = 0; i < 22; i += 2 )
	{
		vdata[i][0] = vdata[i + 1][0] = (float)(i / 2);
		vdata[i][2] = vdata[i + 1][2] = 0;
		vdata[i][3] = vdata[i + 1][3] = 1;

		vdata[i][1] = 0;
		vdata[i + 1][1] = 10;

		vdata[i + 22][1] = vdata[i + 23][1] = (float)(i / 2);
		vdata[i + 22][2] = vdata[i + 23][2] = 0;
		vdata[i + 22][3] = vdata[i + 23][3] = 1;

		vdata[i + 22][0] = 0;
		vdata[i + 23][0] = 10;
	}

	// curve indices
	for( GLuint i = 0; i < numcontrolindices; i += 2 )
	{
		idata[i] = 44 + i / 2;
		idata[i + 1] = 44 + i / 2 + 1;
	}

	supportlines->UnlockIndexBuffer();
	supportlines->UnlockVertexBuffer();

	OpenGLAttributeRange table[] =
	{
		{ GLPT_LINELIST, 0, 0, 0, 0, 44, true },
		{ GLPT_LINELIST, 1, 0, numcontrolindices, 44, numcontrolvertices, true }
	};

	supportlines->SetAttributeTable(table, 2);

	// create spline mesh
	if( !GLCreateMesh(maxsplinevertices, maxsplineindices, GLMESH_32BIT, decl, &curve) )
	{
		MYERROR("Could not create curve");
		return false;
	}

	OpenGLAttributeRange* subset0 = curve->GetAttributeTable();

	subset0->PrimitiveType = GLPT_LINELIST;
	subset0->IndexCount = 0;

	// create surface
	if( !GLCreateMesh(maxsurfacevertices, maxsurfaceindices, GLMESH_32BIT, decl2, &surface) )
	{
		MYERROR("Could not create surface");
		return false;
	}

	// load effects
	if( !GLCreateEffectFromFile("../media/shadersGL/color.vert", "../media/shadersGL/renderpoints.geom", "../media/shadersGL/color.frag", &renderpoints) )
	{
		MYERROR("Could not load point renderer shader");
		return false;
	}

	if( !GLCreateEffectFromFile("../media/shadersGL/color.vert", "../media/shadersGL/renderlines.geom", "../media/shadersGL/color.frag", &renderlines) )
	{
		MYERROR("Could not load line renderer shader");
		return false;
	}

	if( !GLCreateEffectFromFile("../media/shadersGL/rendersurface.vert", 0, "../media/shadersGL/rendersurface.frag", &rendersurface) )
	{
		MYERROR("Could not load surface renderer shader");
		return false;
	}

	if( !GLCreateEffectFromFile("../media/shadersGL/basic2D.vert", 0, "../media/shadersGL/basic2D.frag", &basic2D) )
	{
		MYERROR("Could not load basic 2D shader");
		return false;
	}

	if( hascompute )
	{
		if( !GLCreateComputeProgramFromFile("../media/shadersGL/tessellatecurve.comp", &tessellatecurve) )
		{
			MYERROR("Could not load compute shader");
			return false;
		}

		if( !GLCreateComputeProgramFromFile("../media/shadersGL/tessellatesurface.comp", &tessellatesurface) )
		{
			MYERROR("Could not load compute shader");
			return false;
		}
	}

	screenquad = new OpenGLScreenQuad();

	// tessellate for the first time
	ChangeCurve(0);
	Tessellate();

	// text
	GLCreateTexture(800, 130, 1, GLFMT_A8B8G8R8, &text1);
	UpdateText();

	float angles[2] = { -M_PI / 2, -0.5f };
	cameraangle = angles;

	return true;
}

void UninitScene()
{
	SAFE_DELETE(tessellatesurface);
	SAFE_DELETE(tessellatecurve);
	SAFE_DELETE(renderpoints);
	SAFE_DELETE(renderlines);
	SAFE_DELETE(rendersurface);
	SAFE_DELETE(basic2D);
	SAFE_DELETE(supportlines);
	SAFE_DELETE(surface);
	SAFE_DELETE(curve);
	SAFE_DELETE(screenquad);

	if( text1 )
		glDeleteTextures(1, &text1);

	text1 = 0;

	GLKillAnyRogueObject();
}

bool UpdateControlPoints(float mx, float my)
{
	CurveData& current = curves[currentcurve];

	float	sspx = mx;
	float	sspy = screenheight - my - 1;
	float	dist;
	float	radius = 15.0f / (screenheight / 10);
	bool	isselected = false;

	ConvertToSplineViewport(sspx, sspy);

	if( selectedcontrolpoint == -1 )
	{
		for( GLsizei i = 0; i < numcontrolvertices; ++i )
		{
			selectiondx = current.controlpoints[i][0] - sspx;
			selectiondy = current.controlpoints[i][1] - sspy;
			dist = selectiondx * selectiondx + selectiondy * selectiondy;

			if( dist < radius * radius )
			{
				selectedcontrolpoint = i;
				break;
			}
		}
	}

	isselected = (selectedcontrolpoint > -1 && selectedcontrolpoint < numcontrolvertices);

	if( isselected )
	{
		current.controlpoints[selectedcontrolpoint][0] = GLMin<float>(GLMax<float>(selectiondx + sspx, 0), 10);
		current.controlpoints[selectedcontrolpoint][1] = GLMin<float>(GLMax<float>(selectiondy + sspy, 0), 10);

		float* data = 0;

		supportlines->LockVertexBuffer(
			(44 + selectedcontrolpoint) * supportlines->GetNumBytesPerVertex(),
			supportlines->GetNumBytesPerVertex(), GLLOCK_DISCARD, (void**)&data);

		data[0] = current.controlpoints[selectedcontrolpoint][0];
		data[1] = current.controlpoints[selectedcontrolpoint][1];
		data[2] = current.controlpoints[selectedcontrolpoint][2];

		supportlines->UnlockVertexBuffer();
	}

	return isselected;
}

void ChangeCurve(GLuint newcurve)
{
	CurveData&	next = curves[newcurve];
	float		(*vdata)[4] = 0;

	supportlines->LockVertexBuffer(44 * 16, 0, GLLOCK_DISCARD, (void**)&vdata);

	for( GLuint i = 0; i < numcontrolvertices; ++i )
	{
		vdata[i][0] = next.controlpoints[i][0];
		vdata[i][1] = next.controlpoints[i][1];
		vdata[i][2] = next.controlpoints[i][2];
		vdata[i][3] = 1;
	}

	supportlines->UnlockVertexBuffer();
	currentcurve = newcurve;

	UpdateText();
}

void Tessellate()
{
	if( !hascompute )
		return;

	CurveData& current = curves[currentcurve];

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, curve->GetVertexBuffer());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, curve->GetIndexBuffer());

	if( current.degree > 1 )
		tessellatecurve->SetInt("numCurveVertices", numsegments + 1);
	else
		tessellatecurve->SetInt("numCurveVertices", numcontrolvertices);

	tessellatecurve->SetInt("numControlPoints", numcontrolvertices);
	tessellatecurve->SetInt("degree", current.degree);
	tessellatecurve->SetFloatArray("knots", current.knots, numcontrolvertices + current.degree + 1);
	tessellatecurve->SetFloatArray("weights", current.weights, numcontrolvertices);
	tessellatecurve->SetVectorArray("controlPoints", &current.controlpoints[0][0], numcontrolvertices);

	tessellatecurve->Begin();
	{
		glDispatchCompute(1, 1, 1);
	}
	tessellatecurve->End();

	// update surface cvs
	typedef float vec4[4];

	vec4* surfacecvs = new vec4[numcontrolvertices * numcontrolvertices];
	GLuint index;

	for( GLuint i = 0; i < numcontrolvertices; ++i )
	{
		for( GLuint j = 0; j < numcontrolvertices; ++j )
		{
			index = i * numcontrolvertices + j;

			surfacecvs[index][0] = current.controlpoints[i][0];
			surfacecvs[index][2] = current.controlpoints[j][0];
			surfacecvs[index][1] = (current.controlpoints[i][1] + current.controlpoints[j][1]) * 0.5f;
		}
	}

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, surface->GetVertexBuffer());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, surface->GetIndexBuffer());

	if( current.degree > 1 )
	{
		tessellatesurface->SetInt("numVerticesU", numsegments + 1);
		tessellatesurface->SetInt("numVerticesV", numsegments + 1);
	}
	else
	{
		tessellatesurface->SetInt("numVerticesU", numcontrolvertices);
		tessellatesurface->SetInt("numVerticesV", numcontrolvertices);
	}

	tessellatesurface->SetInt("numControlPointsU", numcontrolvertices);
	tessellatesurface->SetInt("numControlPointsV", numcontrolvertices);
	tessellatesurface->SetInt("degreeU", current.degree);
	tessellatesurface->SetInt("degreeV", current.degree);
	tessellatesurface->SetFloatArray("knotsU", current.knots, numcontrolvertices + current.degree + 1);
	tessellatesurface->SetFloatArray("knotsV", current.knots, numcontrolvertices + current.degree + 1);
	tessellatesurface->SetFloatArray("weightsU", current.weights, numcontrolvertices);
	tessellatesurface->SetFloatArray("weightsV", current.weights, numcontrolvertices);
	tessellatesurface->SetVectorArray("controlPoints", &surfacecvs[0][0], numcontrolvertices * numcontrolvertices);

	tessellatesurface->Begin();
	{
		glDispatchCompute(1, 1, 1);
	}
	tessellatesurface->End();

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);

	glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT|GL_ELEMENT_ARRAY_BARRIER_BIT);

	if( current.degree > 1 )
	{
		curve->GetAttributeTable()->IndexCount = numsegments * 2;
		surface->GetAttributeTable()->IndexCount = numsegments * numsegments * 6;
	}
	else
	{
		curve->GetAttributeTable()->IndexCount = (numcontrolvertices - 1) * 2;
		surface->GetAttributeTable()->IndexCount = (numcontrolvertices - 1) * (numcontrolvertices - 1) * 6;
	}

	delete[] surfacecvs;
}

void Event_KeyDown(unsigned char keycode)
{
}

void Event_KeyUp(unsigned char keycode)
{
	int numcurves = sizeof(curves) / sizeof(curves[0]);

	for( int i = 0; i < numcurves; ++i )
	{
		if( keycode == 0x31 + i )
		{
			ChangeCurve(i);
			Tessellate();
		}
	}

	switch( keycode )
	{
	case 0x46:
		fullscreen = !fullscreen;
		break;

	case 0x55:
		break;

	case 0x57:
		wireframe = !wireframe;
		break;

	case VK_ADD:
		numsegments = GLMin<GLuint>(numsegments + 10, MAX_NUM_SEGMENTS);
		Tessellate();
		break;

	case VK_SUBTRACT:
		numsegments = GLMax<GLuint>(numsegments - 10, 10);
		Tessellate();
		break;

	default:
		break;
	}
}

void Event_MouseMove(int x, int y, short dx, short dy)
{
	mousex = x;
	mousey = y;

	mousedx += dx;
	mousedy += dy;

	if( mousedown == 1 )
	{
		if( !fullscreen )
		{
			if( UpdateControlPoints((float)mousex, (float)mousey) )
				Tessellate();
		}
	}
	else
		selectedcontrolpoint = -1;
}

void Event_MouseScroll(int x, int y, short dz)
{
}

void Event_MouseDown(int x, int y, unsigned char button)
{
	mousedown = 1;
	Event_MouseMove(x, y, 0, 0);
}

void Event_MouseUp(int x, int y, unsigned char button)
{
	mousedown = 0;
}

void Update(float delta)
{
	cameraangle.prev[0] = cameraangle.curr[0];
	cameraangle.prev[1] = cameraangle.curr[1];

	if( mousedown == 1 )
	{
		int left = screenwidth - surfvpwidth - 10;
		int right = screenwidth - 10;
		int top = 10;
		int bottom = surfvpheight + 10;

		if( fullscreen ||
			((mousex >= left && mousex <= right) &&
			(mousey >= top && mousey <= bottom)) )
		{
			cameraangle.curr[0] -= mousedx * 0.004f;
			cameraangle.curr[1] -= mousedy * 0.004f;
		}
	}

	// clamp to [-pi, pi]
	if( cameraangle.curr[1] >= 1.5f )
		cameraangle.curr[1] = 1.5f;

	if( cameraangle.curr[1] <= -1.5f )
		cameraangle.curr[1] = -1.5f;
}

void Render(float alpha, float elapsedtime)
{
	OpenGLColor	grcolor(0xffdddddd);
	OpenGLColor	cvcolor(0xff7470ff);
	OpenGLColor	splinecolor(0xff000000);
	OpenGLColor	outsidecolor(0.75f, 0.75f, 0.8f, 1);
	OpenGLColor	insidecolor(1, 0.66f, 0.066f, 1);

	float		world[16];
	float		view[16];
	float		proj[16];
	float		viewproj[16];
	float		tmp[16];

	float		pointsize[2]	= { 10.0f / screenwidth, 10.0f / screenheight };
	float		grthickness[2]	= { 1.5f / screenwidth, 1.5f / screenheight };
	float		cvthickness[2]	= { 2.0f / screenwidth, 2.0f / screenheight };
	float		spthickness[2]	= { 3.0f / screenwidth, 3.0f / screenheight };
	float		spviewport[]	= { 0, 0, (float)screenwidth, (float)screenheight };

	float		eye[3]			= { 5, 4, 15 };
	float		look[3]			= { 5, 4, 5 };
	float		up[3]			= { 0, 1, 0 };
	float		lightdir[4]		= { 0, 1, 0, 0 };
	float		fwd[3];
	float		orient[2];

	// play with ortho matrix instead of viewport (line thickness remains constant)
	ConvertToSplineViewport(spviewport[0], spviewport[1]);
	ConvertToSplineViewport(spviewport[2], spviewport[3]);

	GLMatrixIdentity(world);
	GLMatrixOrthoRH(proj, spviewport[0], spviewport[2], spviewport[1], spviewport[3], -1, 1);

	glViewport(0, 0, screenwidth, screenheight);
	glClearColor(1, 1, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	renderlines->SetMatrix("matViewProj", proj);
	renderlines->SetMatrix("matWorld", world);
	renderlines->SetVector("color", &grcolor.r);
	renderlines->SetVector("lineThickness", grthickness);

	renderlines->Begin();
	{
		supportlines->DrawSubset(0);

		renderlines->SetVector("color", &cvcolor.r);
		renderlines->SetVector("lineThickness", cvthickness);
		renderlines->CommitChanges();

		supportlines->DrawSubset(1);

		if( hascompute )
		{
			renderlines->SetVector("lineThickness", spthickness);
			renderlines->SetVector("color", &splinecolor.r);
			renderlines->CommitChanges();

			curve->DrawSubset(0);
		}
	}
	renderlines->End();

	renderpoints->SetMatrix("matViewProj", proj);
	renderpoints->SetMatrix("matWorld", world);
	renderpoints->SetVector("color", &cvcolor.r);
	renderpoints->SetVector("pointSize", pointsize);

	renderpoints->Begin();
	{
		glBindVertexArray(supportlines->GetVertexLayout());
		glDrawArrays(GL_POINTS, 44, numcontrolvertices);
	}
	renderpoints->End();

	// render surface in a smaller viewport
	if( !fullscreen )
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(screenwidth - surfvpwidth - 10, screenheight - surfvpheight - 10, surfvpwidth, surfvpheight);
	}

	glClearColor(0.0f, 0.125f, 0.3f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);

	glEnable(GL_DEPTH_TEST);

	if( fullscreen )
		glViewport(0, 0, screenwidth, screenheight);
	else
		glViewport(screenwidth - surfvpwidth - 10, screenheight - surfvpheight - 10, surfvpwidth, surfvpheight);

	cameraangle.smooth(orient, alpha);

	GLVec3Subtract(fwd, look, eye);

	GLMatrixRotationAxis(view, orient[1], 1, 0, 0);
	GLMatrixRotationAxis(tmp, orient[0], 0, 1, 0);
	GLMatrixMultiply(view, view, tmp);

	GLVec3Transform(fwd, fwd, view);
	GLVec3Subtract(eye, look, fwd);

	if( fullscreen )
		GLMatrixPerspectiveFovRH(proj, M_PI / 3, (float)screenwidth / screenheight, 0.1f, 50.0f);
	else
		GLMatrixPerspectiveFovRH(proj, M_PI / 3, (float)surfvpwidth / surfvpheight, 0.1f, 50.0f);

	GLMatrixLookAtRH(view, eye, look, up);
	GLMatrixMultiply(viewproj, view, proj);

	if( wireframe )
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glDisable(GL_CULL_FACE);

	if( hascompute )
	{
		rendersurface->SetMatrix("matViewProj", viewproj);
		rendersurface->SetMatrix("matWorld", world);
		rendersurface->SetMatrix("matWorldInv", world); // its id
		rendersurface->SetVector("lightDir", lightdir);
		rendersurface->SetVector("eyePos", eye);
		rendersurface->SetVector("outsideColor", &outsidecolor.r);
		rendersurface->SetVector("insideColor", &insidecolor.r);
		rendersurface->SetInt("isWireMode", wireframe);

		rendersurface->Begin();
		{
			surface->DrawSubset(0);
		}
		rendersurface->End();
	}

	glEnable(GL_CULL_FACE);

	if( wireframe )
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// render text
	if( !fullscreen )
	{
		glViewport(3, 0, 800, 130);

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		float xzplane[4] = { 0, 1, 0, -0.5f };
		GLMatrixReflect(world, xzplane);

		basic2D->SetMatrix("matTexture", world);
		basic2D->SetInt("sampler0", 0);
		basic2D->Begin();
		{
			glBindTexture(GL_TEXTURE_2D, text1);
			screenquad->Draw();
		}
		basic2D->End();

		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
	}

#ifdef _DEBUG
	// check errors
	GLenum err = glGetError();

	if( err != GL_NO_ERROR )
		std::cout << "Error\n";
#endif

	SwapBuffers(hdc);
	mousedx = mousedy = 0;
}
