/****************************************************************************
*                                                                           *
*  OpenNI 1.1 Alpha                                                         *
*  Copyright (C) 2011 PrimeSense Ltd.                                       *
*                                                                           *
*  This file is part of OpenNI.                                             *
*                                                                           *
*  OpenNI is free software: you can redistribute it and/or modify           *
*  it under the terms of the GNU Lesser General Public License as published *
*  by the Free Software Foundation, either version 3 of the License, or     *
*  (at your option) any later version.                                      *
*                                                                           *
*  OpenNI is distributed in the hope that it will be useful,                *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the             *
*  GNU Lesser General Public License for more details.                      *
*                                                                           *
*  You should have received a copy of the GNU Lesser General Public License *
*  along with OpenNI. If not, see <http://www.gnu.org/licenses/>.           *
*                                                                           *
****************************************************************************/
//---------------------------------------------------------------------------
// Includes
//---------------------------------------------------------------------------
#include <XnOpenNI.h>
#include <XnCodecIDs.h>
#include <XnCppWrapper.h>
#include "SceneDrawer.h"
#include <pandaFramework.h>
#include <pandaSystem.h>
#include <genericAsyncTask.h>
#include <asyncTaskManager.h>
#include <texture.h>
#include <texturePool.h>
#include <nodePathCollection.h>
#include <character.h>
#include <characterJointBundle.h>
#include <modelRoot.h>
#include <lmatrix.h>

//---------------------------------------------------------------------------
// Globals
//---------------------------------------------------------------------------
PandaFramework framework;
// The global task manager
PT(AsyncTaskManager) taskMgr = AsyncTaskManager::get_global_ptr(); 
// The global clock
PT(ClockObject) globalClock = ClockObject::get_global_clock();
// Here's what we'll store the camera in.
NodePath camera;

xn::Context g_Context;
xn::DepthGenerator g_DepthGenerator;
xn::UserGenerator g_UserGenerator;

XnBool g_bNeedPose = FALSE;
XnChar g_strPose[20] = "";
XnBool g_bDrawBackground = TRUE;
XnBool g_bDrawPixels = TRUE;
XnBool g_bDrawSkeleton = TRUE;
XnBool g_bPrintID = TRUE;
XnBool g_bPrintState = TRUE;

XnBool g_bPause = false;
XnBool g_bRecord = false;

XnBool g_bQuit = false;

//---------------------------------------------------------------------------
// Code
//---------------------------------------------------------------------------

// Callback: New user was detected
void XN_CALLBACK_TYPE User_NewUser(xn::UserGenerator& generator, XnUserID nId, void* pCookie)
{
	printf("New User %d\n", nId);
	// New user found
	if (g_bNeedPose)
	{
		g_UserGenerator.GetPoseDetectionCap().StartPoseDetection(g_strPose, nId);
	}
	else
	{
		g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
	}
}
// Callback: An existing user was lost
void XN_CALLBACK_TYPE User_LostUser(xn::UserGenerator& generator, XnUserID nId, void* pCookie)
{
	printf("Lost user %d\n", nId);
}
// Callback: Detected a pose
void XN_CALLBACK_TYPE UserPose_PoseDetected(xn::PoseDetectionCapability& capability, const XnChar* strPose, XnUserID nId, void* pCookie)
{
	printf("Pose %s detected for user %d\n", strPose, nId);
	g_UserGenerator.GetPoseDetectionCap().StopPoseDetection(nId);
	g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
}
// Callback: Started calibration
void XN_CALLBACK_TYPE UserCalibration_CalibrationStart(xn::SkeletonCapability& capability, XnUserID nId, void* pCookie)
{
	printf("Calibration started for user %d\n", nId);
}
// Callback: Finished calibration
void XN_CALLBACK_TYPE UserCalibration_CalibrationEnd(xn::SkeletonCapability& capability, XnUserID nId, XnBool bSuccess, void* pCookie)
{
	if (bSuccess)
	{
		// Calibration succeeded
		printf("Calibration complete, start tracking user %d\n", nId);
		g_UserGenerator.GetSkeletonCap().StartTracking(nId);
	}
	else
	{
		// Calibration failed
		printf("Calibration failed for user %d\n", nId);
		if (g_bNeedPose)
		{
			g_UserGenerator.GetPoseDetectionCap().StartPoseDetection(g_strPose, nId);
		}
		else
		{
			g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
		}
	}
}

#define XN_CALIBRATION_FILE_NAME "UserCalibration.bin"

// Save calibration to file
void SaveCalibration()
{
	XnUserID aUserIDs[20] = {0};
	XnUInt16 nUsers = 20;
	g_UserGenerator.GetUsers(aUserIDs, nUsers);
	for (int i = 0; i < nUsers; ++i)
	{
		// Find a user who is already calibrated
		if (g_UserGenerator.GetSkeletonCap().IsCalibrated(aUserIDs[i]))
		{
			// Save user's calibration to file
			g_UserGenerator.GetSkeletonCap().SaveCalibrationDataToFile(aUserIDs[i], XN_CALIBRATION_FILE_NAME);
			break;
		}
	}
}
// Load calibration from file
void LoadCalibration()
{
	XnUserID aUserIDs[20] = {0};
	XnUInt16 nUsers = 20;
	g_UserGenerator.GetUsers(aUserIDs, nUsers);
	for (int i = 0; i < nUsers; ++i)
	{
		// Find a user who isn't calibrated or currently in pose
		if (g_UserGenerator.GetSkeletonCap().IsCalibrated(aUserIDs[i])) continue;
		if (g_UserGenerator.GetSkeletonCap().IsCalibrating(aUserIDs[i])) continue;

		// Load user's calibration from file
		XnStatus rc = g_UserGenerator.GetSkeletonCap().LoadCalibrationDataFromFile(aUserIDs[i], XN_CALIBRATION_FILE_NAME);
		if (rc == XN_STATUS_OK)
		{
			// Make sure state is coherent
			g_UserGenerator.GetPoseDetectionCap().StopPoseDetection(aUserIDs[i]);
			g_UserGenerator.GetSkeletonCap().StartTracking(aUserIDs[i]);
		}
		break;
	}
}

#define SAMPLE_XML_PATH "SamplesConfig.xml"

#define CHECK_RC(nRetVal, what)										\
	if (nRetVal != XN_STATUS_OK)									\
	{																\
		printf("%s failed: %s\n", what, xnGetStatusString(nRetVal));\
		return nRetVal;												\
	}

// This is our task - a global or static function that has to return DoneStatus.
// The task object is passed as argument, plus a void* pointer, cointaining custom data.
// For more advanced usage, we can subclass AsyncTask and override the do_task method.
AsyncTask::DoneStatus spinCameraTask(GenericAsyncTask* task, void* data)
{
  // Calculate the new position and orientation (inefficient - change me!)
  double time = globalClock->get_real_time();
  double angledegrees = time * 6.0;
  double angleradians = angledegrees * (3.14 / 180.0);
  camera.set_pos(20*sin(angleradians),-20.0*cos(angleradians),3);
  camera.set_hpr(angledegrees, 0, 0);
 
  // Tell the task manager to continue this task the next frame.
  return AsyncTask::DS_done;
}

AsyncTask::DoneStatus updateNI(GenericAsyncTask* task, void* data)
{
	xn::SceneMetaData sceneMD;
	xn::DepthMetaData depthMD;

	if (!g_bPause)
	{
		// Read next available data
		g_Context.WaitOneUpdateAll(g_DepthGenerator);
	}

	// Process the data
	g_DepthGenerator.GetMetaData(depthMD);
	g_UserGenerator.GetUserPixels(0, sceneMD);
//	printf("in updateNI\n");
//	DrawDepthMap(depthMD, sceneMD);

    return AsyncTask::DS_cont;
}

AsyncTask::DoneStatus moveJoint(GenericAsyncTask* task, void* data)
{
    NodePath lArm = *(NodePath *)data;
 
	XnUserID aUsers[15];
	XnUInt16 nUsers = 15;
	g_UserGenerator.GetUsers(aUsers, nUsers);
	if (nUsers && g_UserGenerator.GetSkeletonCap().IsTracking(aUsers[0])) {
	    XnSkeletonJointOrientation orient;
	    g_UserGenerator.GetSkeletonCap().GetSkeletonJointOrientation(aUsers[0],XN_SKEL_LEFT_SHOULDER,orient);
	    XnFloat *e = orient.orientation.elements;
	    LMatrix4f mat = lArm.get_mat();
	    mat.set(e[0],e[1],e[2],0.0,e[3],e[4],e[5],0.0,e[6],e[7],e[8],0.0,0.0,0.0,0.0,1.0);
	    lArm.set_mat(mat);
	}
    
    std::cout << lArm.get_mat() << "\n\n";

    // Tell the task manager to continue this task the next frame.
    return AsyncTask::DS_cont;
}

int setupNI(const char *xmlFile)
{
	XnStatus nRetVal = XN_STATUS_OK;
    
    xn::EnumerationErrors errors;
	nRetVal = g_Context.InitFromXmlFile(xmlFile, &errors);
	if (nRetVal == XN_STATUS_NO_NODE_PRESENT)
	{
		XnChar strError[1024];
		errors.ToString(strError, 1024);
		printf("%s\n", strError);
		return (nRetVal);
	}
	else if (nRetVal != XN_STATUS_OK)
	{
		printf("Open failed: %s\n", xnGetStatusString(nRetVal));
		return (nRetVal);
	}

	nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_DEPTH, g_DepthGenerator);
	CHECK_RC(nRetVal, "Find depth generator");
	nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_USER, g_UserGenerator);
	if (nRetVal != XN_STATUS_OK)
	{
		nRetVal = g_UserGenerator.Create(g_Context);
		CHECK_RC(nRetVal, "Find user generator");
	}

	XnCallbackHandle hUserCallbacks, hCalibrationCallbacks, hPoseCallbacks;
	if (!g_UserGenerator.IsCapabilitySupported(XN_CAPABILITY_SKELETON))
	{
		printf("Supplied user generator doesn't support skeleton\n");
		return 1;
	}
	g_UserGenerator.RegisterUserCallbacks(User_NewUser, User_LostUser, NULL, hUserCallbacks);
	g_UserGenerator.GetSkeletonCap().RegisterCalibrationCallbacks(UserCalibration_CalibrationStart, UserCalibration_CalibrationEnd, NULL, hCalibrationCallbacks);

	if (g_UserGenerator.GetSkeletonCap().NeedPoseForCalibration())
	{
		g_bNeedPose = TRUE;
		if (!g_UserGenerator.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION))
		{
			printf("Pose required, but not supported\n");
			return 1;
		}
		g_UserGenerator.GetPoseDetectionCap().RegisterToPoseCallbacks(UserPose_PoseDetected, NULL, NULL, hPoseCallbacks);
		g_UserGenerator.GetSkeletonCap().GetCalibrationPose(g_strPose);
	}

	g_UserGenerator.GetSkeletonCap().SetSkeletonProfile(XN_SKEL_PROFILE_ALL);

	nRetVal = g_Context.StartGeneratingAll();
	CHECK_RC(nRetVal, "StartGenerating");
}

void printChildren(NodePath node)
{
    NodePathCollection npc = node.get_children();
    for (int i = 0; i < npc.size(); ++i) {
        std::cout << npc[i] << "\n";
        printChildren(npc[i]);
    }
}

void printCharacterChildren(PartGroup* bundle)
{
    for (int i = 0; i < bundle->get_num_children(); i++) {
        std::cout << bundle->get_child(i)->get_name() << "\n";
        printCharacterChildren(bundle->get_child(i));
    }
}

int main(int argc, char **argv)
{
    framework.open_framework(argc, argv);

    const char *xmlFile = SAMPLE_XML_PATH;

	if (argc > 1)
	{
		xmlFile = argv[1];
	}

    setupNI(xmlFile);

    framework.set_window_title("My Panda3D Window");
    WindowFramework *window = framework.open_window();
    // Get the camera and store it in a variable.
    camera = window->get_camera_group();
    camera.set_pos(0,-15,3);
    camera.set_hpr(0, 0, 0);
 
    // Load the environment model.
//    NodePath environ = window->load_model(framework.get_models(), "models/environment");
    NodePath environ = window->load_model(framework.get_models(), "MinecraftBody_bend.egg");
    environ.set_transparency(TransparencyAttrib::M_alpha);
    
    PT(Texture) tex;
    tex = TexturePool::load_texture("Char.png");
    tex->set_magfilter(Texture::FT_nearest);
    printf("texture alpha %d\n",(int)tex->has_alpha(tex->get_format()));
    environ.set_texture(tex, 1);
    
    // Reparent the model to render.
    environ.reparent_to(window->get_render());
    // Apply scale and position transforms to the model.
//    environ.set_scale(0.25, 0.25, 0.25);
//    environ.set_pos(-8, 42, 0);
 
    ModelRoot* eveN = (ModelRoot*)environ.node();
    NodePath eveChNP = environ.find("**/CharRig");      
    Character* eveCH = (Character*)eveChNP.node();
    CharacterJointBundle* eveBundle = eveCH->get_bundle(0);

    NodePath lArm = environ.attach_new_node("lArm");
    eveBundle->control_joint("l_arm", lArm.node());
    CharacterJoint *joint = (CharacterJoint *)eveBundle->find_child("l_arm");
    lArm.set_mat(joint->get_default_value());

    printChildren(environ);
    printCharacterChildren(eveBundle);
 
    // Add our task.
    // If we specify custom data instead of NULL, it will be passed as the second argument
    // to the task function.
//    taskMgr->add(new GenericAsyncTask("Spins the camera", &spinCameraTask, (void*) NULL));
    taskMgr->add(new GenericAsyncTask("Updates OpenNI data", &updateNI, (void*) NULL));
    taskMgr->add(new GenericAsyncTask("Moves a joint", &moveJoint, &lArm));
 
    // Run the engine.
    framework.main_loop();
    // Shut down the engine when done.
    framework.close_framework();
    return (0);
}
