//=============================================================================================
// Bezier and Lagrange curve editor
//=============================================================================================
#include "framework.h"
#include <iostream>

// vertex shader in GLSL
const char* vertexSource = R"(
	#version 330
    precision highp float;

	uniform mat4 MVP;			// Model-View-Projection matrix in row-major format

	layout(location = 0) in vec2 vertexPosition;	// Attrib Array 0

	void main() {
		gl_Position = vec4(vertexPosition.x, vertexPosition.y, 0, 1) * MVP; 		// transform to clipping space
	}
)";

// fragment shader in GLSL
const char* fragmentSource = R"(
	#version 330
    precision highp float;

	uniform vec3 color;
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	void main() {
		fragmentColor = vec4(color, 1); // extend RGB to RGBA
	}
)";

// 2D camera
struct Camera {
	float wCx, wCy;	// center in world coordinates
	float wWx, wWy;	// width and height in world coordinates
public:
	Camera() {
		Animate(0);
	}

	mat4 V() { // view matrix: translates the center to the origin
		return mat4(1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			-wCx, -wCy, 0, 1);
	}

	mat4 P() { // projection matrix: scales it to be a square of edge length 2
		return mat4(2 / wWx, 0, 0, 0,
			0, 2 / wWy, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1);
	}

	mat4 Vinv() { // inverse view matrix
		return mat4(1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			wCx, wCy, 0, 1);
	}

	mat4 Pinv() { // inverse projection matrix
		return mat4(wWx / 2, 0, 0, 0,
			0, wWy / 2, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1);
	}

	void Animate(float t) {
		wCx = 0; // 10 * cosf(t);
		wCy = 0;
		wWx = 20;
		wWy = 20;
	}
};


Camera camera;	// 2D camera
float tCurrent = 0;	// current clock in sec
GPUProgram gpuProgram; // vertex and fragment shaders

class SubdivisionCurve {
	unsigned int vaoCurve, vboCurve;
	unsigned int vaoCtrlPoints, vboCtrlPoints;
	unsigned int vaoInterpolatePoints, vboInterpolatePoints;
	std::vector<vec4> wCurvePoints;
	std::vector<vec4> OGCtrlPoints;
	std::vector<vec4> InterpolateCtrlPoints;

public:
	SubdivisionCurve() {
		// Curve
		glGenVertexArrays(1, &vaoCurve);
		glBindVertexArray(vaoCurve);

		glGenBuffers(1, &vboCurve); // Generate 1 vertex buffer object
		glBindBuffer(GL_ARRAY_BUFFER, vboCurve);
		// Enable the vertex attribute arrays
		glEnableVertexAttribArray(0);  // attribute array 0
		// Map attribute array 0 to the vertex data of the interleaved vbo
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), NULL);

		// Control Points
		glGenVertexArrays(1, &vaoCtrlPoints);
		glBindVertexArray(vaoCtrlPoints);

		glGenBuffers(1, &vboCtrlPoints); // Generate 1 vertex buffer object
		glBindBuffer(GL_ARRAY_BUFFER, vboCtrlPoints);
		// Enable the vertex attribute arrays
		glEnableVertexAttribArray(0);  // attribute array 0
		// Map attribute array 0 to the vertex data of the interleaved vbo
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), NULL); 

		// Interpolate Points
		glGenVertexArrays(1, &vaoInterpolatePoints);
		glBindVertexArray(vaoInterpolatePoints);

		glGenBuffers(1, &vboInterpolatePoints); // Generate 1 vertex buffer object
		glBindBuffer(GL_ARRAY_BUFFER, vboInterpolatePoints);
		// Enable the vertex attribute arrays
		glEnableVertexAttribArray(0);  // attribute array 0
		// Map attribute array 0 to the vertex data of the interleaved vbo
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), NULL);
	}

void AddControlPoint(float cX, float cY) {
		int size = OGCtrlPoints.size();
		vec4 wVertex = vec4(cX, cY, 0, 1) * camera.Pinv() * camera.Vinv();
		if (size >= 4)
		{
			OGCtrlPoints.insert(OGCtrlPoints.begin() + size - 2, wVertex); //new control point inserted to the second to last position
			wCurvePoints = OGCtrlPoints;
			for (int i = 0; i < 4; ++i) 
			{
				nextSubdivisionCalculate();
			}
		}
		else if(size == 1) //second control point: content 0101
		{
			OGCtrlPoints.push_back(wVertex);
			OGCtrlPoints.push_back(OGCtrlPoints[0]);
			OGCtrlPoints.push_back(wVertex);
			wCurvePoints = OGCtrlPoints;
		}
		else //It is the first control point
		{
			OGCtrlPoints.push_back(wVertex);
		}
		InterpolateCtrlPoints = OGCtrlPoints;
	}

	void nextSubdivisionCalculate()
	{
		std::vector<vec4> wHalfPoints;
		std::vector<vec4> wNewVertices;
		for (int i = 0; i < wCurvePoints.size() - 1; ++i)
		{
			vec4 half = (wCurvePoints[i] + wCurvePoints[i + 1]) / 2;
			wHalfPoints.push_back(half);
		}
		for (int i = 1; i < wCurvePoints.size() - 1; ++i)
		{
			vec4 newVertex = (wCurvePoints[i - 1] + 6 * wCurvePoints[i] + wCurvePoints[i + 1]) / 8;
			wNewVertices.push_back(newVertex);
		}
		wCurvePoints.clear();
		for (int i = 0; i < wNewVertices.size(); ++i)
		{
			wCurvePoints.push_back(wHalfPoints[i]);
			wCurvePoints.push_back(wNewVertices[i]);
		}
		wCurvePoints.push_back(wHalfPoints[0]);
		wCurvePoints.push_back(wNewVertices[0]);
	}

	void InterpolateStep()
	{
		std::vector<vec4> verticesLimit;
		for (int i = 1; i < InterpolateCtrlPoints.size(); ++i)
		{
			vec4 vertexLimit = (InterpolateCtrlPoints[i - 1] + 4 * InterpolateCtrlPoints[i] + InterpolateCtrlPoints[i + 1]) / 6;
			verticesLimit.push_back(vertexLimit);
		}
		verticesLimit.insert(verticesLimit.begin(), verticesLimit[verticesLimit.size() - 2]);
		for (int i = 0; i < verticesLimit.size(); ++i)
		{
			InterpolateCtrlPoints[i] += OGCtrlPoints[i] - verticesLimit[i];
		}
		wCurvePoints = InterpolateCtrlPoints;
		for (int i = 0; i < 4; ++i)
		{
			nextSubdivisionCalculate();
		}
	}

	// Returns the selected control point or -1
	int PickControlPoint(float cX, float cY) {
		vec4 wVertex = vec4(cX, cY, 0, 1) * camera.Pinv() * camera.Vinv();
		for (unsigned int p = 0; p < OGCtrlPoints.size(); p++) {
			if (dot(OGCtrlPoints[p] - wVertex, OGCtrlPoints[p] - wVertex) < 0.5) return p;
		}
		return -1;
	}

	void MoveControlPoint(int p, float cX, float cY) {
		vec4 wVertex = vec4(cX, cY, 0, 1) * camera.Pinv() * camera.Vinv();
		OGCtrlPoints[p] = wVertex;
		wCurvePoints = OGCtrlPoints;
		for (int i = 0; i < 4; ++i)
		{
			nextSubdivisionCalculate();
		}
	}

	void Draw() {
		mat4 VPTransform = camera.V() * camera.P();

		gpuProgram.setUniform(VPTransform, "MVP");

		int colorLocation = glGetUniformLocation(gpuProgram.getId(), "color");
		if (OGCtrlPoints.size() > 0) {	// draw control points
			glBindVertexArray(vaoCtrlPoints);
			glBindBuffer(GL_ARRAY_BUFFER, vboCtrlPoints);
			glBufferData(GL_ARRAY_BUFFER, OGCtrlPoints.size() * 4 * sizeof(float), &OGCtrlPoints[0], GL_DYNAMIC_DRAW);
			if (colorLocation >= 0) glUniform3f(colorLocation, 1, 0, 0);
			glPointSize(6.0f);
			glDrawArrays(GL_POINTS, 0, OGCtrlPoints.size());
		}

		if (InterpolateCtrlPoints.size() > 0) {	// draw interpolate points
			glBindVertexArray(vaoInterpolatePoints);
			glBindBuffer(GL_ARRAY_BUFFER, vboInterpolatePoints);
			glBufferData(GL_ARRAY_BUFFER, InterpolateCtrlPoints.size() * 4 * sizeof(float), &InterpolateCtrlPoints[0], GL_DYNAMIC_DRAW);
			if (colorLocation >= 0) glUniform3f(colorLocation, 0, 0.5, 0);
			glPointSize(6.0f);
			glDrawArrays(GL_POINTS, 0, InterpolateCtrlPoints.size());
		}

		if (OGCtrlPoints.size() >= 2) {	// draw line
			glBindVertexArray(vaoCurve);
			glBindBuffer(GL_ARRAY_BUFFER, vboCurve);
			glBufferData(GL_ARRAY_BUFFER, wCurvePoints.size() * 4 * sizeof(float), &wCurvePoints[0], GL_DYNAMIC_DRAW);
			if (colorLocation >= 0) glUniform3f(colorLocation, 1, 1, 0);
			glDrawArrays(GL_LINE_STRIP, 0, wCurvePoints.size());
		}
	}
};


// The virtual world: collection of an objects
SubdivisionCurve* curve;


// Initialization, create an OpenGL context
void onInitialization() {
	curve = new SubdivisionCurve();

	glViewport(0, 0, windowWidth, windowHeight);
	glLineWidth(2.0f);

	// create program for the GPU
	gpuProgram.create(vertexSource, fragmentSource, "outColor");
}

// Window has become invalid: Redraw
void onDisplay() {
	glClearColor(0, 0, 0, 0);							// background color 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the screen

	curve->Draw();
	glutSwapBuffers();									// exchange the two buffers
}

// Key of ASCII code pressed
void onKeyboard(unsigned char key, int pX, int pY) {
	curve->InterpolateStep();
	glutPostRedisplay();        // redraw
}

// Key of ASCII code released
void onKeyboardUp(unsigned char key, int pX, int pY) {

}

int pickedControlPoint = -1;

// Mouse click event
void onMouse(int button, int state, int pX, int pY) {
	if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {  
		float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
		float cY = 1.0f - 2.0f * pY / windowHeight;
		curve->AddControlPoint(cX, cY);
		glutPostRedisplay();     // redraw
	}
	if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN) {  
		float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
		float cY = 1.0f - 2.0f * pY / windowHeight;
		pickedControlPoint = curve->PickControlPoint(cX, cY);
		glutPostRedisplay();     // redraw
	}
	if (button == GLUT_RIGHT_BUTTON && state == GLUT_UP) {  
		pickedControlPoint = -1;
	}
}

// Move mouse with key pressed
void onMouseMotion(int pX, int pY) {
	float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
	float cY = 1.0f - 2.0f * pY / windowHeight;
	if (pickedControlPoint >= 0) curve->MoveControlPoint(pickedControlPoint, cX, cY);
}

// Idle event indicating that some time elapsed: do animation here
void onIdle() {
	long time = glutGet(GLUT_ELAPSED_TIME); // elapsed time since the start of the program
	tCurrent = time / 1000.0f;				// convert msec to sec
	glutPostRedisplay();					// redraw the scene
}