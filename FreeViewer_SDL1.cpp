// FreeViewer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "networking.h"
#include "errorHandler.h"
#define GLM_FORCE_RADIANS
 //#define BOOST_LIB_DIAGNOSTIC

#include <glm/glm.hpp>
// glm::translate, glm::rotate, glm::scale
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>

#include <Ole2.h>

#include <NuiApi.h>
#include <NuiImageCamera.h>
#include <NuiSensor.h>


const int width = 640;
const int height = 480;
const int depthWidth = 320;
const int depthHeight = 240;

#define degreesToRadians(x) x*(3.141592f/180.0f)

using namespace std;
using namespace boost;

// buffer objects storing point clouds
GLuint vboId;
GLuint cboId;

//depth data
vector<long*> depthToRgbMaps;
vector<USHORT*> depths;

float angleKinects =90.0f;
float heightDiff = 0.0f ;
float radius = 1.53f;
float viewAngle = 0.0f;
float zPos = 1.53f;

long activePoints;
map<string, float> parameters;

const string ANGLE_KINECTS = "angleKinects";
const string HEIGHT_DIFF = "heightDiff";
const string RADIUS = "radius";
const string VIEW_ANGLE="viewAngle";
const string Z_POS = "zPos";
const float offset = 0.03f;
const int mainCamera = 0;
const long depthThreshold = 2000;

vector<HANDLE> depthStreams;
vector<HANDLE> rgbStreams;
vector<INuiSensor*> sensors;

DWORD WINAPI MyThreadFunction( LPVOID lpParam ) ;
void ErrorHandler(LPTSTR lpszFunction);


void transformPointCloud(glm::vec4 &V, const float angle, const float pos){
	glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f,0.0f,pos));
	transform = glm::rotate(transform, angle, glm::vec3(0.0f, 1.0f, 0.0f));
	transform = glm::translate(transform, glm::vec3(0.0f,0.0f,(-1)*pos));
	transform = glm::translate(transform, glm::vec3(0.0f,parameters[HEIGHT_DIFF],0.0f));

	V=transform*V;
}

bool initKinect() {
    int numSensors;
    if (NuiGetSensorCount(&numSensors) < 0 || numSensors < 1) return false;
    cout<<numSensors<<" sensors found!"<<endl;
    for(int i=0; i<numSensors; i++){
    	HANDLE depthStream;
		HANDLE rgbStream;
		INuiSensor* sensor;	
    	if (NuiCreateSensorByIndex(i, &sensor) < 0) return false;
	    // Initialize sensor
	    sensor->NuiInitialize(NUI_INITIALIZE_FLAG_USES_DEPTH | NUI_INITIALIZE_FLAG_USES_COLOR);
	    sensor->NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH, // Depth camera or rgb camera?
	        NUI_IMAGE_RESOLUTION_320x240,                // Image resolution
	        NUI_IMAGE_STREAM_FLAG_DISTINCT_OVERFLOW_DEPTH_VALUES,        // Image stream flags, e.g. near mode
	        2,        // Number of frames to buffer
	        NULL,     // Event handle
	        &depthStream);
		sensor->NuiImageStreamOpen(NUI_IMAGE_TYPE_COLOR, // Depth camera or rgb camera?
	        NUI_IMAGE_RESOLUTION_640x480,                // Image resolution
	        0,      // Image stream flags, e.g. near mode
	        2,      // Number of frames to buffer
	        NULL,   // Event handle
			&rgbStream);
		sensors.push_back(sensor);
		rgbStreams.push_back(rgbStream);
		depthStreams.push_back(depthStream);
    }
    return true;
}

GLubyte* getDepthData(GLubyte* dest, int idx) {
	float* fdest = (float*) dest;
	long* depth2rgb = (long*) depthToRgbMaps[idx];
    NUI_IMAGE_FRAME imageFrame;
    NUI_LOCKED_RECT LockedRect;
    HRESULT hr = sensors[idx]->NuiImageStreamGetNextFrame(depthStreams[idx], 500, &imageFrame);
    if (hr < 0){
    	cout<<"failed to get depth image frames from sensor "<<idx<<endl;
    	return NULL;
    }
    INuiFrameTexture* texture = imageFrame.pFrameTexture;
    texture->LockRect(0, &LockedRect, NULL, 0);
    if (LockedRect.Pitch != 0) {
        const USHORT* curr = (const USHORT*) LockedRect.pBits;
        for (int j = 0; j < depthHeight; ++j) {
			for (int i = 0; i < depthWidth; ++i) {
				// Get depth of pixel in millimeters
				USHORT depth = NuiDepthPixelToDepth(*curr++);	
				depths[idx][j*depthWidth+i]=depth;	

				//only interested in the person, not the background
				if(depth<=depthThreshold ){		
					// Store coordinates of the point corresponding to this pixel
					Vector4 pos = NuiTransformDepthImageToSkeleton(i, j, depth<<3, NUI_IMAGE_RESOLUTION_320x240);
					glm::vec4 V = glm::vec4(pos.x/pos.w, pos.y/pos.w, pos.z/pos.w, 1.0f);
			
					if(idx!=mainCamera){
						transformPointCloud(V, degreesToRadians(parameters[ANGLE_KINECTS]), parameters[Z_POS]);						
					}
					activePoints++;
					*fdest++ = V.x;
					*fdest++ = V.y;
					*fdest++ = V.z;
					// Store the index into the color array corresponding to this pixel
					if(NuiImageGetColorPixelCoordinatesFromDepthPixelAtResolution(
						NUI_IMAGE_RESOLUTION_640x480, NUI_IMAGE_RESOLUTION_320x240, NULL,
						i, j, depth<<3, depth2rgb, depth2rgb+1)!=S_OK){
						cout<<"the data "<<*depth2rgb<<" and "<<*(depth2rgb+1)<<" is invalid"<<endl;
					}
					depth2rgb += 2;
				}
			}
		}
    }
    texture->UnlockRect(0);
    sensors[idx]->NuiImageStreamReleaseFrame(depthStreams[idx], &imageFrame);
    return (GLubyte*)fdest;
}

void getColorPixel(const long &x, const long &y, const BYTE* start, vector<float> &val){
	float r=0.0f,g=0.0f,b=0.0f;
	int numValid = 0;
	for(int i=0; i<=1; i++){
		for(int j=0; j<=1; j++){
			int x_i = x+i;
			int y_j = y+j;
			if(x_i<0 || y_j<0 || x_i>width || y_j>height){
				continue;
			}else{
				const BYTE* curr = start + (x_i + width*y_j)*4;
				numValid++;
				r+=curr[2];
				g+=curr[1];
				b+=curr[0];
			}
		}
	}
	if(numValid!=0){
		val[0]=b/(float)numValid;
		val[1]=g/(float)numValid;
		val[2]=r/(float)numValid;
	}
}

GLubyte* getRgbData(GLubyte* dest, int idx) {
	float* fdest = (float*) dest;
	long* depth2rgb = (long*) depthToRgbMaps[idx];
	NUI_IMAGE_FRAME imageFrame;
    NUI_LOCKED_RECT LockedRect;
    HRESULT hr = sensors[idx]->NuiImageStreamGetNextFrame(rgbStreams[idx], 500, &imageFrame);
    if (hr < 0){
    	cout<<"failed to get rbg image frames from sensor "<<idx<<endl;
    	return NULL;
    }
    INuiFrameTexture* texture = imageFrame.pFrameTexture;
    texture->LockRect(0, &LockedRect, NULL, 0);
    if (LockedRect.Pitch != 0) {
        const BYTE* start = (const BYTE*) LockedRect.pBits;
		vector<float> curr(3,0.0f);
        for (int j = 0; j < depthHeight; ++j) {
			for (int i = 0; i < depthWidth; ++i) {
				//only interested in active points:
				if(depths[idx][j*depthWidth+i]<=depthThreshold ){
					// Determine rgb color for each depth pixel
					long x = *depth2rgb++;
					long y = *depth2rgb++;
					// If out of bounds, then don't color it at all
					if (x<0 || y<0 || x>width || y>height) {
						for (int n = 0; n < 3; ++n) *(fdest++) = 0.f;
					}else{
						const BYTE* curr = start + (x + width*y)*4;

						// getColorPixel(x,y,start,curr);
						
						for (int n = 0; n < 3; ++n) *fdest++ = curr[2-n]/255.f;
					}
				}
			}
		}
    }
    texture->UnlockRect(0);
    sensors[idx]->NuiImageStreamReleaseFrame(rgbStreams[idx], &imageFrame);
    return (GLubyte*)fdest;
}

void getKinectData() {
	const int dataSize = depthWidth*depthHeight*3*sizeof(float)*sensors.size();
	GLubyte* ptr;
	for(int i=0; i<(int)sensors.size();i++){
		if(i==mainCamera){
			glBindBuffer(GL_ARRAY_BUFFER, vboId);
			glBufferData(GL_ARRAY_BUFFER, dataSize, 0, GL_DYNAMIC_DRAW);
			ptr = (GLubyte*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		}	
		if (ptr) {
			ptr = getDepthData(ptr, i);
			if(ptr==NULL) cout<<"null after getdepthData"<<endl;		
		}else{
			// cout<<"cannot get depth data"<<endl;
		}
		if(i==sensors.size()-1){
			glUnmapBuffer(GL_ARRAY_BUFFER);			
		}
	}
	for(int i=0; i<(int)sensors.size();i++){
		if(i==mainCamera){
			glBindBuffer(GL_ARRAY_BUFFER, cboId);
			glBufferData(GL_ARRAY_BUFFER, dataSize, 0, GL_DYNAMIC_DRAW);
			ptr = (GLubyte*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		}	
		if (ptr) {
			ptr = getRgbData(ptr, i);	
			if(ptr==NULL) cout<<"null after getRgbData"<<endl;		
		}else{
			// cout<<"cannot get rgb data"<<endl;
		}
		if(i==sensors.size()-1){
			glUnmapBuffer(GL_ARRAY_BUFFER);
		}		
	}
}

void rotateCamera(float rotation) {
	viewAngle += rotation;
	float x = radius*sin(viewAngle);
	float z = radius*(1-cos(viewAngle));
	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
	gluLookAt(x,0,z,0,0,radius,0,1,0);
}

void updateRenderView(){
	float x = radius*sin(-1.0f*viewAngle);
	float z = radius*(1-cos(-1.0f*viewAngle));
	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
	gluLookAt(x,0,z,0,0,radius,0,1,0);
}


void drawKinectData() {
	getKinectData();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glVertexPointer(3, GL_FLOAT, 0, NULL);
	
	glBindBuffer(GL_ARRAY_BUFFER, cboId);
	glColorPointer(3, GL_FLOAT, 0, NULL);

	glPointSize(1.f);
	glDrawArrays(GL_POINTS, 0, activePoints);	
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	activePoints=0;
	// cout<<viewAngle<<endl;
	// rotateCamera(0);
	updateRenderView();

}

int loadFromIniFile(){
	ifstream myfile("FreeViewer.txt");
	string line;
	if (myfile.is_open()){
	    while ( getline (myfile,line) ){
	    	// std::string::size_type sz;
	    	erase_all(line, " ");
	    	vector<string> splited;
	    	split(splited, line, is_any_of("="));
	    	if(splited.size()!=2){
	    		continue;
	    	}
	    	float data = stof(splited[1]);
	    	parameters[splited[0]] = data;
	    }
	    if(parameters.size()!=5){
	    	cout<<"file does not contain enough information"<<endl;
	    	parameters.clear();
	    	parameters[ANGLE_KINECTS] = angleKinects;
	    	parameters[HEIGHT_DIFF] = heightDiff;
	    	parameters[RADIUS] = radius;
	    	parameters[VIEW_ANGLE] = viewAngle;
	    	parameters[Z_POS] = zPos;
	    }
	    myfile.close();
    }else{
    	cout << "Unable to open file"<<endl; 	
    	return 1;
    } 
    return 0;
}

int saveToIniFile(){
	ofstream myfile("FreeViewer.txt");
	if (myfile.is_open()){
		for(auto it = parameters.begin(); it!=parameters.end(); it++){
			myfile<<it->first<<"="<<it->second<<endl;
		}
		myfile.close();
	}else{
		cout<< "Unable to open file"<<endl;
		return 1;
	}
	return 0;
}

bool init(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Surface* screen = SDL_SetVideoMode(width, height, 32, SDL_HWSURFACE | SDL_GL_DOUBLEBUFFER | SDL_OPENGL);
    SDL_EnableUNICODE(1);
	activePoints=0;
    loadFromIniFile();
    return true;
}

void execute() {
    SDL_Event ev;
    bool running = true;
    while (running) {
        while (SDL_PollEvent(&ev)) {
        	switch(ev.type){
        		case SDL_QUIT:
        			running=false;
        			break;
        		case SDL_KEYDOWN:{
        			switch(ev.key.keysym.sym){
				        case SDLK_LEFT:
        				  viewAngle+=offset;
				          break;
				        case SDLK_RIGHT:
        				  viewAngle-=offset;
				          break;
				        case SDLK_UP:
				          parameters[HEIGHT_DIFF]+=0.01f;
				          cout<<"parameters[HEIGHT_DIFF]: "<<parameters[HEIGHT_DIFF]<<" m"<<endl;
				          break;
				        case SDLK_DOWN:
				          parameters[HEIGHT_DIFF]-=0.01f;
				          cout<<"parameters[HEIGHT_DIFF]: "<<parameters[HEIGHT_DIFF]<<" m"<<endl;
				          break;
				        case SDLK_a:
				          parameters[ANGLE_KINECTS]-=0.1f;
				          cout<<"parameters[ANGLE_KINECTS]: "<<parameters[ANGLE_KINECTS]<<" degree"<<endl;
				          break;
				        case SDLK_d:
				          parameters[ANGLE_KINECTS]+=0.1f;
				          cout<<"parameters[ANGLE_KINECTS]: "<<parameters[ANGLE_KINECTS]<<" degree"<<endl;
				          break;
				        case SDLK_w:
				          parameters[Z_POS]+=0.01f;
				          break;
				        case SDLK_s:
				          parameters[Z_POS]-=0.01f;
				          break;
				        case SDLK_0:
				          viewAngle = 0.0f;
				          break;
				        case SDLK_ESCAPE:
				          saveToIniFile();
				          break; 
				        default:
				          break;
      				}
      				break;
        		}
      			default:
        			break; 
        	}
        }
        drawKinectData();
        SDL_GL_SwapBuffers();
    }
}

int main(int argc, char* argv[]) {
    if (!init(argc, argv)) exit(1);
    if (!initKinect()) exit(1);

	GLenum err=glewInit();
	if(err!=GLEW_OK)
	{
       cout<<"glewInit failed, aborting."<<endl;
       exit(1);
	}

    // OpenGL setup
    glClearColor(0,0,0,0);
    glClearDepth(1.0f);

	// Set up array buffers
    glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glGenBuffers(1, &cboId);
	glBindBuffer(GL_ARRAY_BUFFER, cboId);

	//init depths arrays
	for(int i=0; i<(int)sensors.size(); i++){
		long *depth2rgb = new long[depthWidth*depthHeight*2];
		USHORT *depth = new USHORT[depthWidth*depthHeight];
		depthToRgbMaps.push_back(depth2rgb);
		depths.push_back(depth);
	}


	//create listening thread:
	DWORD   thread_id;
    HANDLE  listener; 

	listener = CreateThread(NULL, 0, MyThreadFunction, NULL, 0, &thread_id);
	if(thread_id == NULL){
		ErrorHandler(TEXT("Create listener thread"));
	}

    // Camera setup
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
	gluPerspective(46, depthWidth /(GLdouble) depthHeight, 0.1, 1000);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
	gluLookAt(0,0,0,0,0,radius,0,1,0);

    // Main loop
    execute();
    return 0;
}

DWORD WINAPI MyThreadFunction( LPVOID lpParam ) { 
    listeningForMsg();
    return 0; 
} 


