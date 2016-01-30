/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 * The engine ties all game modules together. 
 */

/** \file gameengine/Ketsji/KX_KetsjiEngine.cpp
 *  \ingroup ketsji
 */

#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif

#include <iostream>
#include <stdio.h>

#include "BLI_task.h"

#include "KX_KetsjiEngine.h"

#include "EXP_ListValue.h"
#include "EXP_IntValue.h"
#include "EXP_VectorValue.h"
#include "EXP_BoolValue.h"
#include "EXP_FloatValue.h"

#include "RAS_Rect.h"
#include "RAS_ICanvas.h"
#include "MT_Vector3.h"
#include "MT_Transform.h"
#include "SCA_IInputDevice.h"
#include "KX_Camera.h"
#include "KX_Light.h"
#include "KX_PythonInit.h"
#include "KX_PyConstraintBinding.h"
#include "PHY_IPhysicsEnvironment.h"

#include "NG_NetworkScene.h"
#include "NG_NetworkDeviceInterface.h"

#include "KX_WorldInfo.h"
#include "KX_ISceneConverter.h"
#include "KX_TimeCategoryLogger.h"

#include "RAS_FramingManager.h"
#include "DNA_world_types.h"
#include "DNA_scene_types.h"

#include "KX_NavMeshObject.h"

#include "BL_Action.h" // For managing action lock.

// Viewport drawing
#include "GPU_glew.h"
#include "GPU_framebuffer.h"
extern "C" {
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"
#include "ED_view3d.h"
#include "BKE_camera.h"
#include "BLI_math.h"
}


#define DEFAULT_LOGIC_TIC_RATE 60.0
//#define DEFAULT_PHYSICS_TIC_RATE 60.0

#ifdef FREE_WINDOWS /* XXX mingw64 (gcc 4.7.0) defines a macro for DrawText that translates to DrawTextA. Not good */
#ifdef DrawText
#undef DrawText
#endif
#endif

const char KX_KetsjiEngine::m_profileLabels[tc_numCategories][15] = {
	"Physics:",		// tc_physics
	"Logic:",		// tc_logic
	"Animations:",	// tc_animations
	"Network:",		// tc_network
	"Scenegraph:",	// tc_scenegraph
	"Rasterizer:",	// tc_rasterizer
	"Services:",	// tc_services
	"Overhead:",	// tc_overhead
	"Outside:",		// tc_outside
	"GPU Latency:"	// tc_latency
};

double KX_KetsjiEngine::m_ticrate = DEFAULT_LOGIC_TIC_RATE;
int	   KX_KetsjiEngine::m_maxLogicFrame = 5;
int	   KX_KetsjiEngine::m_maxPhysicsFrame = 5;
double KX_KetsjiEngine::m_anim_framerate = 25.0;
double KX_KetsjiEngine::m_suspendedtime = 0.0;
double KX_KetsjiEngine::m_suspendeddelta = 0.0;
double KX_KetsjiEngine::m_average_framerate = 0.0;
bool   KX_KetsjiEngine::m_restrict_anim_fps = false;
short  KX_KetsjiEngine::m_exitkey = 130; //ESC Key


/**
 *	Constructor of the Ketsji Engine
 */
KX_KetsjiEngine::KX_KetsjiEngine(KX_ISystem* system)
	:	m_canvas(NULL),
	m_kxsystem(system),
	m_sceneconverter(NULL),
	m_networkdevice(NULL),
#ifdef WITH_PYTHON
	m_pythondictionary(NULL),
#endif
	m_keyboarddevice(NULL),
	m_mousedevice(NULL),

	m_bInitialized(false),
	m_activecam(0),
	m_bFixedTime(false),
	m_useExternalClock(false),
	
	m_firstframe(true),
	
	m_frameTime(0.f),
	m_clockTime(0.f),
	m_previousClockTime(0.f),
	m_previousAnimTime(0.f),
	m_timescale(1.0f),
	m_previousRealTime(0.0f),


	m_exitcode(KX_EXIT_REQUEST_NO_REQUEST),
	m_exitstring(""),

	m_cameraZoom(1.0f),
	
	m_overrideCam(false),
	m_overrideCamUseOrtho(false),
	m_overrideCamNear(0.0f),
	m_overrideCamFar(0.0f),
	m_overrideCamZoom(1.0f),

	m_stereo(false),
	m_curreye(0),

	m_logger(NULL),
	
	// Set up timing info display variables
	m_show_framerate(false),
	m_show_profile(false),
	m_showProperties(false),
	m_showBackground(false),
	m_show_debug_properties(false),
	m_autoAddDebugProperties(true),

	m_animation_record(false),

	// Default behavior is to hide the cursor every frame.
	m_hideCursor(false),

	m_overrideFrameColor(false),
	m_overrideFrameColorR(0.0f),
	m_overrideFrameColorG(0.0f),
	m_overrideFrameColorB(0.0f)
{
	// Initialize the time logger
	m_logger = new KX_TimeCategoryLogger (25);

	for (int i = tc_first; i < tc_numCategories; i++)
		m_logger->AddCategory((KX_TimeCategory)i);

#ifdef WITH_PYTHON
	m_pyprofiledict = PyDict_New();
#endif

	m_taskscheduler = BLI_task_scheduler_create(TASK_SCHEDULER_AUTO_THREADS);

	BL_Action::InitLock();
}



/**
 *	Destructor of the Ketsji Engine, release all memory
 */
KX_KetsjiEngine::~KX_KetsjiEngine()
{
	delete m_logger;

#ifdef WITH_PYTHON
	Py_CLEAR(m_pyprofiledict);
#endif

	if (m_taskscheduler)
		BLI_task_scheduler_free(m_taskscheduler);

	BL_Action::EndLock();
}



void KX_KetsjiEngine::SetKeyboardDevice(SCA_IInputDevice* keyboarddevice)
{
	MT_assert(keyboarddevice);
	m_keyboarddevice = keyboarddevice;
}



void KX_KetsjiEngine::SetMouseDevice(SCA_IInputDevice* mousedevice)
{
	MT_assert(mousedevice);
	m_mousedevice = mousedevice;
}



void KX_KetsjiEngine::SetNetworkDevice(NG_NetworkDeviceInterface* networkdevice)
{
	MT_assert(networkdevice);
	m_networkdevice = networkdevice;
}


void KX_KetsjiEngine::SetCanvas(RAS_ICanvas* canvas)
{
	MT_assert(canvas);
	m_canvas = canvas;
}

#ifdef WITH_PYTHON
/*
 * At the moment the bge.logic module is imported into 'pythondictionary' after this function is called.
 * if this function ever changes to assign a copy, make sure the game logic module is imported into this dictionary before hand.
 */
void KX_KetsjiEngine::SetPyNamespace(PyObject *pythondictionary)
{
	MT_assert(pythondictionary);
	m_pythondictionary = pythondictionary;
}

PyObject* KX_KetsjiEngine::GetPyProfileDict()
{
	Py_INCREF(m_pyprofiledict);
	return m_pyprofiledict;
}
#endif


void KX_KetsjiEngine::SetSceneConverter(KX_ISceneConverter* sceneconverter)
{
	MT_assert(sceneconverter);
	m_sceneconverter = sceneconverter;
}

/**
 * Ketsji Init(), Initializes data-structures and converts data from
 * Blender into Ketsji native (realtime) format also sets up the
 * graphics context
 */
void KX_KetsjiEngine::StartEngine(bool clearIpo)
{
	m_clockTime = m_kxsystem->GetTimeInSeconds();
	m_frameTime = m_kxsystem->GetTimeInSeconds();
	m_previousClockTime = m_kxsystem->GetTimeInSeconds();
	m_previousRealTime = m_kxsystem->GetTimeInSeconds();

	m_firstframe = true;
	m_bInitialized = true;
	// there is always one scene enabled at startup
	Scene* scene = m_scenes[0]->GetBlenderScene();
	if (scene)
	{
		m_ticrate = scene->gm.ticrate ? scene->gm.ticrate : DEFAULT_LOGIC_TIC_RATE;
		m_maxLogicFrame = scene->gm.maxlogicstep ? scene->gm.maxlogicstep : 5;
		m_maxPhysicsFrame = scene->gm.maxphystep ? scene->gm.maxlogicstep : 5;
	}
	else
	{
		m_ticrate = DEFAULT_LOGIC_TIC_RATE;
		m_maxLogicFrame = 5;
		m_maxPhysicsFrame = 5;
	}
	
	if (m_animation_record)
	{
		m_sceneconverter->ResetPhysicsObjectsAnimationIpo(clearIpo);
		m_sceneconverter->WritePhysicsObjectToAnimationIpo(m_currentFrame);
	}

}

//#include "PIL_time.h"
//#include "LinearMath/btQuickprof.h"


bool KX_KetsjiEngine::NextFrame()
{
	double timestep =  m_timescale / m_ticrate;
	double framestep = timestep;
	//	static hidden::Clock sClock;

	m_logger->StartLog(tc_services, m_kxsystem->GetTimeInSeconds(),true);

	//float dt = sClock.getTimeMicroseconds() * 0.000001f;
	//sClock.reset();

	/*
	 * Clock advancement. There is basically three case:
	 *   - m_useExternalClock is true, the user is responsible to advance the time
	 *   manually using setClockTime, so here, we do not do anything.
	 *   - m_useExternalClock is false, m_bFixedTime is true, we advance for one
	 *   timestep, which already handle the time scaling parameter
	 *   - m_useExternalClock is false, m_bFixedTime is false, we consider how much
	 *   time has elapsed since last call and we scale this time by the time
	 *   scaling parameter. If m_timescale is 1.0 (default value), the clock
	 *   corresponds to the computer clock.
	 *
	 * Once clockTime has been computed, we will compute how many logic frames
	 * will be executed before the next rendering phase (which will occur at "clockTime").
	 * The game time elapsing between two logic frames (called framestep)
	 * depends on several variables:
	 *   - ticrate 
	 *   - max_physic_frame
	 *   - max_logic_frame
	 * XXX The logic over computation framestep is definitively not clear (and
	 * I'm not even sure it is correct). If needed frame is strictly greater
	 * than max_physics_frame, we are doing a jump in game time, but keeping
	 * framestep = 1 / ticrate, while if frames is greater than
	 * max_logic_frame, we increase framestep.
	 *
	 * XXX render.fps is not considred anywhere.
	 */
	if (!m_useExternalClock) {
		if (m_bFixedTime) {
			m_clockTime += timestep;
		}
		else {
			double current_time = m_kxsystem->GetTimeInSeconds();
			double dt = current_time - m_previousRealTime;
			m_previousRealTime = current_time;
			// m_clockTime += dt;
			m_clockTime += dt * m_timescale;
		}
	}
	
	double deltatime = m_clockTime - m_frameTime;
	if (deltatime<0.0)
	{
		// We got here too quickly, which means there is nothing todo, just return and don't render.
		// Not sure if this is the best fix, but it seems to stop the jumping framerate issue (#33088)
		return false;
	}

	// Compute the number of logic frames to do each update (fixed tic bricks)
	int frames = int(deltatime * m_ticrate / m_timescale + 1e-6);
//	if (frames>1)
//		printf("****************************************");
//	printf("dt = %f, deltatime = %f, frames = %d\n",dt, deltatime,frames);
	
//	if (!frames)
//		PIL_sleep_ms(1);
	KX_SceneList::iterator sceneit;
	
	if (frames>m_maxPhysicsFrame)
	{
	
	//	printf("framedOut: %d\n",frames);
		m_frameTime+=(frames-m_maxPhysicsFrame)*timestep;
		frames = m_maxPhysicsFrame;
	}
	

	bool doRender = frames>0;

	if (frames > m_maxLogicFrame)
	{
		framestep = (frames*timestep)/m_maxLogicFrame;
		frames = m_maxLogicFrame;
	}

	while (frames)
	{
	

		m_frameTime += framestep;
		
		m_sceneconverter->MergeAsyncLoads();

		for (sceneit = m_scenes.begin();sceneit != m_scenes.end(); ++sceneit)
		// for each scene, call the proceed functions
		{
			KX_Scene* scene = *sceneit;
	
			/* Suspension holds the physics and logic processing for an
			 * entire scene. Objects can be suspended individually, and
			 * the settings for that precede the logic and physics
			 * update. */
			m_logger->StartLog(tc_logic, m_kxsystem->GetTimeInSeconds(), true);

			m_sceneconverter->resetNoneDynamicObjectToIpo();//this is for none dynamic objects with ipo

			scene->UpdateObjectActivity();
	
			if (!scene->IsSuspended())
			{
				// if the scene was suspended recalcutlate the delta tu "curtime"
				m_suspendedtime = scene->getSuspendedTime();
				if (scene->getSuspendedTime()!=0.0)
					scene->setSuspendedDelta(scene->getSuspendedDelta()+m_clockTime-scene->getSuspendedTime());
				m_suspendeddelta = scene->getSuspendedDelta();

				
				m_logger->StartLog(tc_network, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_NETWORK);
				scene->GetNetworkScene()->proceed(m_frameTime);
	
				//m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				//SG_SetActiveStage(SG_STAGE_NETWORK_UPDATE);
				//scene->UpdateParents(m_frameTime);
				
				m_logger->StartLog(tc_physics, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_PHYSICS1);
				// set Python hooks for each scene
#ifdef WITH_PYTHON
				PHY_SetActiveEnvironment(scene->GetPhysicsEnvironment());
#endif
				KX_SetActiveScene(scene);
	
				scene->GetPhysicsEnvironment()->EndFrame();
				
				// Update scenegraph after physics step. This maps physics calculations
				// into node positions.
				//m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				//SG_SetActiveStage(SG_STAGE_PHYSICS1_UPDATE);
				//scene->UpdateParents(m_frameTime);
				
				// Process sensors, and controllers
				m_logger->StartLog(tc_logic, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_CONTROLLER);
				scene->LogicBeginFrame(m_frameTime);
	
				// Scenegraph needs to be updated again, because Logic Controllers 
				// can affect the local matrices.
				m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_CONTROLLER_UPDATE);
				scene->UpdateParents(m_frameTime);
	
				// Process actuators
	
				// Do some cleanup work for this logic frame
				m_logger->StartLog(tc_logic, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_ACTUATOR);
				scene->LogicUpdateFrame(m_frameTime, true);
				
				scene->LogicEndFrame();
	
				// Actuators can affect the scenegraph
				m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_ACTUATOR_UPDATE);
				scene->UpdateParents(m_frameTime);

				// update levels of detail
				scene->UpdateObjectLods();

				m_logger->StartLog(tc_physics, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_PHYSICS2);
				scene->GetPhysicsEnvironment()->BeginFrame();
		
				// Perform physics calculations on the scene. This can involve 
				// many iterations of the physics solver.
				scene->GetPhysicsEnvironment()->ProceedDeltaTime(m_frameTime,timestep,framestep);//m_deltatimerealDeltaTime);

				m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_PHYSICS2_UPDATE);
				scene->UpdateParents(m_frameTime);
			
			
				if (m_animation_record)
				{
					m_sceneconverter->WritePhysicsObjectToAnimationIpo(++m_currentFrame);
				}

				scene->setSuspendedTime(0.0);
			} // suspended
			else
				if (scene->getSuspendedTime()==0.0)
					scene->setSuspendedTime(m_clockTime);
			
			m_logger->StartLog(tc_services, m_kxsystem->GetTimeInSeconds(), true);
		}

		// update system devices
		m_logger->StartLog(tc_logic, m_kxsystem->GetTimeInSeconds(), true);
		if (m_keyboarddevice)
			m_keyboarddevice->NextFrame();
	
		if (m_mousedevice)
			m_mousedevice->NextFrame();
		
		if (m_networkdevice)
			m_networkdevice->NextFrame();

		// scene management
		ProcessScheduledScenes();
		
		frames--;
	}

	// Start logging time spend outside main loop
	m_logger->StartLog(tc_outside, m_kxsystem->GetTimeInSeconds(), true);
	
	return doRender;
}

void KX_KetsjiEngine::BlenderRender(View3D *v3d, ARegion *ar)
{
	KX_Scene *kxscene = m_scenes[0];
	Scene *scene = kxscene->GetBlenderScene();
	RegionView3D *rv3d = (RegionView3D*)ar->regiondata;
	char err_out[256] = "unknown";
	int sx = m_canvas->GetWidth() + 1;
	int sy = m_canvas->GetHeight() + 1;
	int samples = 0; // TODO get AA working
	short flag = v3d->flag, flag2 = v3d->flag2, flag3 = v3d->flag3;

	/* view state */
	GPUFXSettings fx_settings = v3d->fx_settings;
	bool is_ortho = false;
	float winmat[4][4];
	const char *viewname = NULL;

	m_logger->StartLog(tc_rasterizer, m_kxsystem->GetTimeInSeconds(), true);
	SG_SetActiveStage(SG_STAGE_RENDER);

	v3d->flag &= ~V3D_SELECT_OUTLINE;
	v3d->flag2 |= V3D_RENDER_OVERRIDE;
	v3d->flag3 |= V3D_SHOW_WORLD;

	GPUOffScreen *ofs = GPU_offscreen_create(
		sx,
		sy,
		samples,
		err_out
	);

	ED_view3d_draw_offscreen_init(scene, v3d);

	GPU_offscreen_bind(ofs, true);

	/* render 3d view */
	if (rv3d->persp == RV3D_CAMOB && v3d->camera) {
		CameraParams params;
		Object *camera = BKE_camera_multiview_render(scene, v3d->camera, viewname);

		BKE_camera_params_init(&params);
		/* fallback for non camera objects */
		params.clipsta = v3d->near;
		params.clipend = v3d->far;
		BKE_camera_params_from_object(&params, camera);
		BKE_camera_multiview_params(&scene->r, &params, camera, viewname);
		BKE_camera_params_compute_viewplane(&params, sx, sy, scene->r.xasp, scene->r.yasp);
		BKE_camera_params_compute_matrix(&params);

		BKE_camera_to_gpu_dof(camera, &fx_settings);

		is_ortho = params.is_ortho;
		copy_m4_m4(winmat, params.winmat);
	}
	else {
		rctf viewplane;
		float clipsta, clipend;

		is_ortho = ED_view3d_viewplane_get(v3d, rv3d, sx, sy, &viewplane, &clipsta, &clipend, NULL);
		if (is_ortho) {
			orthographic_m4(winmat, viewplane.xmin, viewplane.xmax, viewplane.ymin, viewplane.ymax, -clipend, clipend);
		}
		else {
			perspective_m4(winmat, viewplane.xmin, viewplane.xmax, viewplane.ymin, viewplane.ymax, clipsta, clipend);
		}
	}

	ED_view3d_draw_offscreen(
		scene, v3d, ar, sx, sy, NULL, winmat,
		false, true, !is_ortho, viewname,
		NULL, &fx_settings, ofs
	);

	/* unbind */
	GPU_offscreen_unbind(ofs, true);

	{
		// Draw fullscreen quad
		m_canvas->SetViewPort(0, 0, m_canvas->GetWidth(), m_canvas->GetHeight());
		int tex = GPU_offscreen_color_texture(ofs);

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, tex);
		glBegin(GL_QUADS);
			glColor4f(1.f, 1.f, 1.f, 1.f);
			//glColor4f(0.5f, 1.f, 0.f, 1.f);
			glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f,1.0f);
			glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,1.0f);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f,-1.0f);
			glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f,-1.0f);
		glEnd();

		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
	}

	GPU_offscreen_free(ofs);

	PostRenderScene(kxscene);
	m_canvas->SwapBuffers();

	// Restore flags
	v3d->flag = flag;
	v3d->flag2 = flag2;
	v3d->flag3 = flag3;
}


void KX_KetsjiEngine::RequestExit(int exitrequestmode)
{
	m_exitcode = exitrequestmode;
}



void KX_KetsjiEngine::SetNameNextGame(const STR_String& nextgame)
{
	m_exitstring = nextgame;
}



int KX_KetsjiEngine::GetExitCode()
{
	// if a gameactuator has set an exitcode or if there are no scenes left
	if (!m_exitcode)
	{
		if (m_scenes.begin()==m_scenes.end())
			m_exitcode = KX_EXIT_REQUEST_NO_SCENES_LEFT;
	}
	
	// check if the window has been closed.
	if (!m_exitcode)
	{
		//if (!m_canvas->Check()) {
		//	m_exitcode = KX_EXIT_REQUEST_OUTSIDE;
		//}
	}

	return m_exitcode;
}



const STR_String& KX_KetsjiEngine::GetExitString()
{
	return m_exitstring;
}

void KX_KetsjiEngine::EnableCameraOverride(const STR_String& forscene)
{
	m_overrideCam = true;
	m_overrideSceneName = forscene;
}

void KX_KetsjiEngine::SetCameraZoom(float camzoom)
{
	m_cameraZoom = camzoom;
}

void KX_KetsjiEngine::SetCameraOverrideUseOrtho(bool useOrtho)
{
	m_overrideCamUseOrtho = useOrtho;
}

void KX_KetsjiEngine::SetCameraOverrideProjectionMatrix(const MT_CmMatrix4x4& mat)
{
	m_overrideCamProjMat = mat;
}

void KX_KetsjiEngine::SetCameraOverrideViewMatrix(const MT_CmMatrix4x4& mat)
{
	m_overrideCamViewMat = mat;
}

void KX_KetsjiEngine::SetCameraOverrideClipping(float nearfrust, float farfrust)
{
	m_overrideCamNear = nearfrust;
	m_overrideCamFar = farfrust;
}

void KX_KetsjiEngine::SetCameraOverrideLens(float lens)
{
	m_overrideCamLens = lens;
}

void KX_KetsjiEngine::SetCameraOverrideZoom(float camzoom)
{
	m_overrideCamZoom = camzoom;
}

void KX_KetsjiEngine::GetSceneViewport(KX_Scene *scene, KX_Camera* cam, RAS_Rect& area, RAS_Rect& viewport)
{
	printf("KX_KetsjiEngine::GetSceneViewport() is no longer implemented");
#if 0
	// In this function we make sure the rasterizer settings are upto
	// date. We compute the viewport so that logic
	// using this information is upto date.

	// Note we postpone computation of the projection matrix
	// so that we are using the latest camera position.
	if (cam->GetViewport()) {
		RAS_Rect userviewport;

		userviewport.SetLeft(cam->GetViewportLeft()); 
		userviewport.SetBottom(cam->GetViewportBottom());
		userviewport.SetRight(cam->GetViewportRight());
		userviewport.SetTop(cam->GetViewportTop());

		// Don't do bars on user specified viewport
		RAS_FrameSettings settings = scene->GetFramingType();
		if (settings.FrameType() == RAS_FrameSettings::e_frame_bars)
			settings.SetFrameType(RAS_FrameSettings::e_frame_extend);

		RAS_FramingManager::ComputeViewport(
			scene->GetFramingType(),
			userviewport,
			viewport
		);

		area = userviewport;
	}
	else if ( !m_overrideCam || (scene->GetName() != m_overrideSceneName) ||  m_overrideCamUseOrtho ) {
		RAS_FramingManager::ComputeViewport(
			scene->GetFramingType(),
			m_canvas->GetDisplayArea(),
			viewport
		);

		area = m_canvas->GetDisplayArea();
	} else {
		viewport.SetLeft(0); 
		viewport.SetBottom(0);
		viewport.SetRight(int(m_canvas->GetWidth()));
		viewport.SetTop(int(m_canvas->GetHeight()));

		area = m_canvas->GetDisplayArea();
	}
#endif
}

void KX_KetsjiEngine::UpdateAnimations(KX_Scene *scene)
{
	if (scene->IsSuspended()) {
		return;
	}

	// Handle the animations independently of the logic time step
	if (GetRestrictAnimationFPS()) {
		double anim_timestep = 1.0 / KX_GetActiveScene()->GetAnimationFPS();
		if (m_frameTime - m_previousAnimTime > anim_timestep || m_frameTime == m_previousAnimTime) {
			// Sanity/debug print to make sure we're actually going at the fps we want (should be close to anim_timestep)
			// printf("Anim fps: %f\n", 1.0/(m_frameTime - m_previousAnimTime));
			m_previousAnimTime = m_frameTime;
			for (KX_SceneList::iterator sceneit = m_scenes.begin(); sceneit != m_scenes.end(); ++sceneit)
				(*sceneit)->UpdateAnimations(m_frameTime);
		}
	}
	else
		scene->UpdateAnimations(m_frameTime);
}

/*
 * To run once per scene
 */
void KX_KetsjiEngine::PostRenderScene(KX_Scene* scene)
{
	KX_SetActiveScene(scene);

	// We need to first make sure our viewport is correct (enabling multiple viewports can mess this up)
	m_canvas->SetViewPort(0, 0, m_canvas->GetWidth(), m_canvas->GetHeight());

#ifdef WITH_PYTHON
	PHY_SetActiveEnvironment(scene->GetPhysicsEnvironment());
	scene->RunDrawingCallbacks(scene->GetPostDrawCB());

	// Python draw callback can also call debug draw functions, so we have to clear debug shapes.
	//m_rasterizer->FlushDebugShapes(scene);
#endif
}

void KX_KetsjiEngine::StopEngine()
{
	if (m_bInitialized)
	{
		m_sceneconverter->FinalizeAsyncLoads();

		if (m_animation_record)
		{
//			printf("TestHandlesPhysicsObjectToAnimationIpo\n");
			m_sceneconverter->TestHandlesPhysicsObjectToAnimationIpo();
		}

		KX_SceneList::iterator sceneit;
		for (sceneit = m_scenes.begin();sceneit != m_scenes.end() ; sceneit++)
		{
			KX_Scene* scene = *sceneit;
			m_sceneconverter->RemoveScene(scene);
		}
		m_scenes.clear();
	}
}

// Scene Management is able to switch between scenes
// and have several scene's running in parallel
void KX_KetsjiEngine::AddScene(KX_Scene* scene)
{ 
	m_scenes.push_back(scene);
	PostProcessScene(scene);
}



void KX_KetsjiEngine::PostProcessScene(KX_Scene* scene)
{
	bool override_camera = (m_overrideCam && (scene->GetName() == m_overrideSceneName));

	SG_SetActiveStage(SG_STAGE_SCENE);

	// if there is no activecamera, or the camera is being
	// overridden we need to construct a temporarily camera
	if (!scene->GetActiveCamera() || override_camera)
	{
		KX_Camera* activecam = NULL;

		RAS_CameraData camdata = RAS_CameraData();
		if (override_camera)
		{
			camdata.m_lens = m_overrideCamLens;
			camdata.m_clipstart = m_overrideCamNear;
			camdata.m_clipend = m_overrideCamFar;
			
			camdata.m_perspective= !m_overrideCamUseOrtho;
		}
		activecam = new KX_Camera(scene,KX_Scene::m_callbacks,camdata);
		activecam->SetName("__default__cam__");
	
			// set transformation
		if (override_camera) {
			const MT_CmMatrix4x4& cammatdata = m_overrideCamViewMat;
			MT_Transform trans = MT_Transform(cammatdata.getPointer());
			MT_Transform camtrans;
			camtrans.invert(trans);
			
			activecam->NodeSetLocalPosition(camtrans.getOrigin());
			activecam->NodeSetLocalOrientation(camtrans.getBasis());
			activecam->NodeUpdateGS(0);
		} else {
			activecam->NodeSetLocalPosition(MT_Point3(0.0f, 0.0f, 0.0f));
			activecam->NodeSetLocalOrientation(MT_Vector3(0.0f, 0.0f, 0.0f));
			activecam->NodeUpdateGS(0);
		}

		scene->AddCamera(activecam);
		scene->SetActiveCamera(activecam);
		scene->GetObjectList()->Add(activecam->AddRef());
		scene->GetRootParentList()->Add(activecam->AddRef());
		//done with activecam
		activecam->Release();
	}
	
	scene->UpdateParents(0.0);
}


KX_SceneList* KX_KetsjiEngine::CurrentScenes()
{
	return &m_scenes;
}



KX_Scene* KX_KetsjiEngine::FindScene(const STR_String& scenename)
{
	KX_SceneList::iterator sceneit = m_scenes.begin();

	// bit risky :) better to split the second clause 
	while ( (sceneit != m_scenes.end()) 
			&& ((*sceneit)->GetName() != scenename))
	{
		sceneit++;
	}

	return ((sceneit == m_scenes.end()) ? NULL : *sceneit);
}



void KX_KetsjiEngine::ConvertAndAddScene(const STR_String& scenename,bool overlay)
{
	// only add scene when it doesn't exist!
	if (FindScene(scenename)) {
		printf("warning: scene %s already exists, not added!\n",scenename.ReadPtr());
	}
	else {
		if (overlay) {
			m_addingOverlayScenes.push_back(scenename);
		}
		else {
			m_addingBackgroundScenes.push_back(scenename);
		}
	}
}




void KX_KetsjiEngine::RemoveScene(const STR_String& scenename)
{
	if (FindScene(scenename))
	{
		m_removingScenes.push_back(scenename);
	}
	else
	{
//		STR_String tmpname = scenename;
		std::cout << "warning: scene " << scenename << " does not exist, not removed!" << std::endl;
	}
}



void KX_KetsjiEngine::RemoveScheduledScenes()
{
	if (m_removingScenes.size())
	{
		vector<STR_String>::iterator scenenameit;
		for (scenenameit=m_removingScenes.begin();scenenameit != m_removingScenes.end();scenenameit++)
		{
			STR_String scenename = *scenenameit;

			KX_SceneList::iterator sceneit;
			for (sceneit = m_scenes.begin();sceneit != m_scenes.end() ; sceneit++)
			{
				KX_Scene* scene = *sceneit;
				if (scene->GetName()==scenename)
				{
					m_sceneconverter->RemoveScene(scene);
					m_scenes.erase(sceneit);
					break;
				}
			}
		}
		m_removingScenes.clear();
	}
}

KX_Scene* KX_KetsjiEngine::CreateScene(Scene *scene, bool libloading)
{
	KX_Scene* tmpscene = new KX_Scene(m_keyboarddevice,
									  m_mousedevice,
									  m_networkdevice,
									  scene->id.name+2,
									  scene,
									  m_canvas);

	m_sceneconverter->ConvertScene(tmpscene,
							  m_canvas,
							  libloading);

	return tmpscene;
}

KX_Scene* KX_KetsjiEngine::CreateScene(const STR_String& scenename)
{
	Scene *scene = m_sceneconverter->GetBlenderSceneForName(scenename);
	if (!scene)
		return NULL;
	return CreateScene(scene);
}

void KX_KetsjiEngine::AddScheduledScenes()
{
	vector<STR_String>::iterator scenenameit;

	if (m_addingOverlayScenes.size())
	{
		for (scenenameit = m_addingOverlayScenes.begin();
			scenenameit != m_addingOverlayScenes.end();
			scenenameit++)
		{
			STR_String scenename = *scenenameit;
			KX_Scene* tmpscene = CreateScene(scenename);
			if (tmpscene) {
				m_scenes.push_back(tmpscene);
				PostProcessScene(tmpscene);
			} else {
				printf("warning: scene %s could not be found, not added!\n",scenename.ReadPtr());
			}
		}
		m_addingOverlayScenes.clear();
	}
	
	if (m_addingBackgroundScenes.size())
	{
		for (scenenameit = m_addingBackgroundScenes.begin();
			scenenameit != m_addingBackgroundScenes.end();
			scenenameit++)
		{
			STR_String scenename = *scenenameit;
			KX_Scene* tmpscene = CreateScene(scenename);
			if (tmpscene) {
				m_scenes.insert(m_scenes.begin(),tmpscene);
				PostProcessScene(tmpscene);
			} else {
				printf("warning: scene %s could not be found, not added!\n",scenename.ReadPtr());
			}
		}
		m_addingBackgroundScenes.clear();
	}
}



bool KX_KetsjiEngine::ReplaceScene(const STR_String& oldscene,const STR_String& newscene)
{
	// Don't allow replacement if the new scene doesn't exists.
	// Allows smarter game design (used to have no check here).
	// Note that it creates a small backward compatbility issue
	// for a game that did a replace followed by a lib load with the
	// new scene in the lib => it won't work anymore, the lib
	// must be loaded before doing the replace.
	if (m_sceneconverter->GetBlenderSceneForName(newscene) != NULL) {
		m_replace_scenes.push_back(std::make_pair(oldscene,newscene));
		return true;
	}
	return false;
}

// replace scene is not the same as removing and adding because the
// scene must be in exact the same place (to maintain drawingorder)
// (nzc) - should that not be done with a scene-display list? It seems
// stupid to rely on the mem allocation order...
void KX_KetsjiEngine::ReplaceScheduledScenes()
{
	if (m_replace_scenes.size())
	{
		vector<pair<STR_String,STR_String> >::iterator scenenameit;
		
		for (scenenameit = m_replace_scenes.begin();
			scenenameit != m_replace_scenes.end();
			scenenameit++)
		{
			STR_String oldscenename = (*scenenameit).first;
			STR_String newscenename = (*scenenameit).second;
			int i=0;
			/* Scenes are not supposed to be included twice... I think */
			KX_SceneList::iterator sceneit;
			for (sceneit = m_scenes.begin();sceneit != m_scenes.end() ; sceneit++) {
				KX_Scene* scene = *sceneit;
				if (scene->GetName() == oldscenename) {
					// avoid crash if the new scene doesn't exist, just do nothing
					Scene *blScene = m_sceneconverter->GetBlenderSceneForName(newscenename);
					if (blScene) {
						m_sceneconverter->RemoveScene(scene);
						KX_Scene* tmpscene = CreateScene(blScene);
						m_scenes[i]=tmpscene;
						PostProcessScene(tmpscene);
					}
					else {
						printf("warning: scene %s could not be found, not replaced!\n",newscenename.ReadPtr());
					}
				}
				i++;
			}
		}
		m_replace_scenes.clear();
	}
}



void KX_KetsjiEngine::SuspendScene(const STR_String& scenename)
{
	KX_Scene*  scene = FindScene(scenename);
	if (scene) scene->Suspend();
}



void KX_KetsjiEngine::ResumeScene(const STR_String& scenename)
{
	KX_Scene*  scene = FindScene(scenename);
	if (scene) scene->Resume();
}



void KX_KetsjiEngine::SetUseFixedTime(bool bUseFixedTime)
{
	m_bFixedTime = bUseFixedTime;
}

void KX_KetsjiEngine::SetUseExternalClock(bool useExternalClock)
{
	m_useExternalClock = useExternalClock;
}

void	KX_KetsjiEngine::SetAnimRecordMode(bool animation_record, int startFrame)
{
	m_animation_record = animation_record;
	if (animation_record)
	{
		//when recording physics keyframes, run at a variable (capped) frame rate (fixed time == full speed)
		m_bFixedTime = false;
	}
	m_currentFrame = startFrame;
}

int KX_KetsjiEngine::getAnimRecordFrame() const
{
	return m_currentFrame;
}

void KX_KetsjiEngine::setAnimRecordFrame(int framenr)
{
	m_currentFrame = framenr;
}

bool KX_KetsjiEngine::GetUseFixedTime(void) const
{
	return m_bFixedTime;
}

bool KX_KetsjiEngine::GetUseExternalClock(void) const
{
	return m_useExternalClock;
}

double KX_KetsjiEngine::GetSuspendedDelta()
{
	return m_suspendeddelta;
}

double KX_KetsjiEngine::GetTicRate()
{
	return m_ticrate;
}

void KX_KetsjiEngine::SetTicRate(double ticrate)
{
	m_ticrate = ticrate;
}

double KX_KetsjiEngine::GetTimeScale() const
{
	return m_timescale;
}

void KX_KetsjiEngine::SetTimeScale(double timescale)
{
	m_timescale = timescale;
}

int KX_KetsjiEngine::GetMaxLogicFrame()
{
	return m_maxLogicFrame;
}

void KX_KetsjiEngine::SetMaxLogicFrame(int frame)
{
	m_maxLogicFrame = frame;
}

int KX_KetsjiEngine::GetMaxPhysicsFrame()
{
	return m_maxPhysicsFrame;
}

void KX_KetsjiEngine::SetMaxPhysicsFrame(int frame)
{
	m_maxPhysicsFrame = frame;
}

bool KX_KetsjiEngine::GetRestrictAnimationFPS()
{
	return m_restrict_anim_fps;
}

void KX_KetsjiEngine::SetRestrictAnimationFPS(bool bRestrictAnimFPS)
{
	m_restrict_anim_fps = bRestrictAnimFPS;
}

double KX_KetsjiEngine::GetAnimFrameRate()
{
	return m_anim_framerate;
}

double KX_KetsjiEngine::GetClockTime(void) const
{
	return m_clockTime;
}

void KX_KetsjiEngine::SetClockTime(double externalClockTime)
{
	m_clockTime = externalClockTime;
}

double KX_KetsjiEngine::GetFrameTime(void) const
{
	return m_frameTime;
}

double KX_KetsjiEngine::GetRealTime(void) const
{
	return m_kxsystem->GetTimeInSeconds();
}

void KX_KetsjiEngine::SetAnimFrameRate(double framerate)
{
	m_anim_framerate = framerate;
}

double KX_KetsjiEngine::GetAverageFrameRate()
{
	return m_average_framerate;
}

void KX_KetsjiEngine::SetExitKey(short key)
{
	m_exitkey = key;
}

short KX_KetsjiEngine::GetExitKey()
{
	return m_exitkey;
}

void KX_KetsjiEngine::SetShowFramerate(bool frameRate)
{
	m_show_framerate = frameRate;
}

bool KX_KetsjiEngine::GetShowFramerate()
{
	return m_show_framerate;
}

void KX_KetsjiEngine::SetShowProfile(bool profile)
{
	m_show_profile = profile;
}

bool KX_KetsjiEngine::GetShowProfile()
{
	return m_show_profile;
}

void KX_KetsjiEngine::SetShowProperties(bool properties)
{
	m_show_debug_properties = properties;
}

bool KX_KetsjiEngine::GetShowProperties()
{
	return m_show_debug_properties;
}

void KX_KetsjiEngine::SetAutoAddDebugProperties(bool add)
{
	m_autoAddDebugProperties = add;
}

bool KX_KetsjiEngine::GetAutoAddDebugProperties()
{
	return m_autoAddDebugProperties;
}

void KX_KetsjiEngine::SetTimingDisplay(bool frameRate, bool profile, bool properties)
{
	m_show_framerate = frameRate;
	m_show_profile = profile;
	m_show_debug_properties = properties;
}



void KX_KetsjiEngine::GetTimingDisplay(bool& frameRate, bool& profile, bool& properties) const
{
	frameRate = m_show_framerate;
	profile = m_show_profile;
	properties = m_show_debug_properties;
}



void KX_KetsjiEngine::ProcessScheduledScenes(void)
{
	// Check whether there will be changes to the list of scenes
	if (m_addingOverlayScenes.size() ||
		m_addingBackgroundScenes.size() ||
		m_replace_scenes.size() ||
		m_removingScenes.size()) {

		// Change the scene list
		ReplaceScheduledScenes();
		RemoveScheduledScenes();
		AddScheduledScenes();
	}
}


void KX_KetsjiEngine::SetHideCursor(bool hideCursor)
{
	m_hideCursor = hideCursor;
}


bool KX_KetsjiEngine::GetHideCursor(void) const
{
	return m_hideCursor;
}


void KX_KetsjiEngine::SetUseOverrideFrameColor(bool overrideFrameColor)
{
	m_overrideFrameColor = overrideFrameColor;
}


bool KX_KetsjiEngine::GetUseOverrideFrameColor(void) const
{
	return m_overrideFrameColor;
}


void KX_KetsjiEngine::SetOverrideFrameColor(float r, float g, float b)
{
	m_overrideFrameColorR = r;
	m_overrideFrameColorG = g;
	m_overrideFrameColorB = b;
}


void KX_KetsjiEngine::GetOverrideFrameColor(float& r, float& g, float& b) const
{
	r = m_overrideFrameColorR;
	g = m_overrideFrameColorG;
	b = m_overrideFrameColorB;
}


void KX_KetsjiEngine::Resize()
{
	KX_SceneList::iterator sceneit;

	/* extended mode needs to recalculate camera frustrums when */
	KX_Scene* firstscene = *m_scenes.begin();
	const RAS_FrameSettings &framesettings = firstscene->GetFramingType();

	if (framesettings.FrameType() == RAS_FrameSettings::e_frame_extend) {
		for (sceneit = m_scenes.begin();sceneit != m_scenes.end(); sceneit++) {
			KX_Camera* cam = ((KX_Scene *)*sceneit)->GetActiveCamera();
			cam->InvalidateProjectionMatrix();
		}
	}
}

