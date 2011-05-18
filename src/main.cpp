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
#include "MinecraftGenerator.h"
#include "SendCharacter.h"
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
#include <pgEntry.h>
#include <keyboardButton.h>

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
WindowFramework* window;
CharacterJointBundle* mcBundle;
TextNode *text;

xn::Context g_Context;
xn::DepthGenerator g_DepthGenerator;
xn::UserGenerator g_UserGenerator;
xn::ImageGenerator g_ImageGenerator;

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

XnBool g_generate_texture = false;
XnBool g_reset = false;

enum {
    ANT_FARM_WAITING = 0,
    ANT_FARM_CALIBRATING = 1,
    ANT_FARM_TRACKING = 2
};

int app_state = ANT_FARM_WAITING;

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
	if (app_state == ANT_FARM_WAITING) {
	    text->set_text("Calibrating...");
	    app_state = ANT_FARM_CALIBRATING;
	}
}
// Callback: Finished calibration
void XN_CALLBACK_TYPE UserCalibration_CalibrationEnd(xn::SkeletonCapability& capability, XnUserID nId, XnBool bSuccess, void* pCookie)
{
	if (bSuccess)
	{
		// Calibration succeeded
		printf("Calibration complete, start tracking user %d\n", nId);
		g_UserGenerator.GetSkeletonCap().StartTracking(nId);
		text->set_text("Enter Twitter handle, email address, or whatev");
		app_state = ANT_FARM_TRACKING;
		g_generate_texture = true;
	}
	else
	{
	    if (app_state == ANT_FARM_CALIBRATING) {
	        text->set_text("Looking for user...");
	        app_state = ANT_FARM_WAITING;
	    }
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

#define SAMPLE_XML_PATH "SamplesConfig.xml"

#define CHECK_RC(nRetVal, what)										\
	if (nRetVal != XN_STATUS_OK)									\
	{																\
		printf("%s failed: %s\n", what, xnGetStatusString(nRetVal));\
		return nRetVal;												\
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
	nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_IMAGE, g_ImageGenerator);
	CHECK_RC(nRetVal, "Find image generator");
	nRetVal = g_ImageGenerator.SetPixelFormat(XN_PIXEL_FORMAT_RGB24);
	CHECK_RC(nRetVal, "Set image format");
	
    // Registration
	if (g_DepthGenerator.IsCapabilitySupported(XN_CAPABILITY_ALTERNATIVE_VIEW_POINT))
	{
		nRetVal = g_DepthGenerator.GetAlternativeViewPointCap().SetViewPoint(g_ImageGenerator);
		printf("Doing image registration\n");
		CHECK_RC(nRetVal, "Registration");
	}
	
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

void resetUsers(const Event *theEvent, void *data)
{
    XnUserID aUsers[15];
	XnUInt16 nUsers = 15;
	g_UserGenerator.GetUsers(aUsers, nUsers);
	int i = 0;
	for (i = 0; i < nUsers; ++i) {
	    g_UserGenerator.GetSkeletonCap().Reset(aUsers[i]);
	    g_UserGenerator.GetPoseDetectionCap().StartPoseDetection(g_strPose, aUsers[i]);
	}
	
	text->set_text("Looking for user...");
	app_state = ANT_FARM_WAITING;
	g_reset = true;

    printf("Restarting UserGenerator\n");
}

void acceptEntry(const Event *theEvent, void *data)
{
    NodePath *inputNP = (NodePath *)data;
    PGEntry *input = (PGEntry *)inputNP->node();
    std::cout << input->get_text() << "\n";
    
    if (input->get_text().length()) {
        SendCharacter("skin.png",input->get_text().c_str());
    }
    
    input->set_text("");
    input->set_focus(true);
    
    resetUsers(NULL, NULL);
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

    static int stabilize = 15;
    if (g_generate_texture == true && !stabilize-- && data) {

        printf("Generating texture\n");

        GenerateMinecraftCharacter(depthMD, sceneMD, g_ImageGenerator.GetRGB24ImageMap());
    
        NodePath character = *(NodePath *)data;
        TexturePool::release_all_textures();
        Texture *tex = TexturePool::load_texture("../skin.png");
        tex->set_magfilter(Texture::FT_nearest);
        character.set_texture(tex, 1);
        
        stabilize = 15;
        g_generate_texture = false;
    }
    
    if (g_reset == true && data) {
        NodePath character = *(NodePath *)data;
        PT(Texture) tex;
        tex = TexturePool::load_texture("Char.png");
        tex->set_magfilter(Texture::FT_nearest);
        character.set_texture(tex, 1);
        g_reset = false;
    }

    return AsyncTask::DS_cont;
}

XnSkeletonJoint jointForName(string name)
{
    if (name.compare("body") == 0) {
        return XN_SKEL_TORSO;
    } else if (name.compare("body_lower") == 0) {
        return XN_SKEL_TORSO;
    } else if (name.compare("head") == 0) {
        return XN_SKEL_HEAD;
    } else if (name.compare("l_arm") == 0) {
        return XN_SKEL_RIGHT_SHOULDER;
    } else if (name.compare("l_arm_lower") == 0) {
        return XN_SKEL_RIGHT_ELBOW;
    } else if (name.compare("r_arm") == 0) {
        return XN_SKEL_LEFT_SHOULDER;
    } else if (name.compare("r_arm_lower") == 0) {
        return XN_SKEL_LEFT_ELBOW;
    } else if (name.compare("l_leg") == 0) {
        return XN_SKEL_RIGHT_HIP;
    } else if (name.compare("l_leg_lower") == 0) {
        return XN_SKEL_RIGHT_KNEE;
    } else if (name.compare("r_leg") == 0) {
        return XN_SKEL_LEFT_HIP;
    } else if (name.compare("r_leg_lower") == 0) {
        return XN_SKEL_LEFT_KNEE;
    } else {
        return XN_SKEL_LEFT_FOOT; // Treat my left foot as the error case
    }
}

AsyncTask::DoneStatus moveJoint(GenericAsyncTask* task, void* data)
{
    if (data == NULL) return AsyncTask::DS_cont;
    NodePathCollection *collection = (NodePathCollection *)data;
 
	XnUserID aUsers[15];
	XnUInt16 nUsers = 15;
    g_UserGenerator.GetUsers(aUsers, nUsers);
    
	if (nUsers && g_UserGenerator.GetSkeletonCap().IsTracking(aUsers[0])) {
	
	    for (int i = 0; i < collection->size(); i++) {
	        NodePath node = collection->get_path(i);
	        
	        XnSkeletonJoint joint = jointForName(node.get_name());

	        if ((joint != XN_SKEL_LEFT_FOOT)) {
	            XnSkeletonJointOrientation orient;
	            XnSkeletonJointPosition pos;
	            g_UserGenerator.GetSkeletonCap().GetSkeletonJointOrientation(aUsers[0],joint,orient);
	            XnFloat *e = orient.orientation.elements;

                CharacterJoint *j = (CharacterJoint *)mcBundle->find_child(node.get_name());
                LMatrix4f jmat = j->get_default_value();
                LMatrix4f mat4 = node.get_mat();
	            LMatrix3f mat = jmat.get_upper_3();
	            LMatrix3f omat = LMatrix3f::ident_mat();
                omat.set(e[0],-e[2],e[1],-e[6],e[8],-e[7],e[3],-e[5],e[4]);
                LMatrix4f omat4 = LMatrix4f(omat);
                mat *= omat;
                mat4.set_upper_3(mat);
                node.set_mat(mat4);
        //      node.set_r(-node.get_r());
        //	    LVecBase3f hpr = node.get_hpr();
        //	    node.set_hpr(hpr.get_x(),hpr.get_y(),-hpr.get_z());
	        }
	    }
	}

    // Tell the task manager to continue this task the next frame.
    return AsyncTask::DS_cont;
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

void addBones(PartGroup *bundle, NodePathCollection *collection)
{
    for (int i = 0; i < bundle->get_num_children(); i++) {
        CharacterJoint *joint = (CharacterJoint *)bundle->get_child(i);
        
        std::cout << bundle->get_name() << " " << bundle->get_num_children() << " " << i << " " << joint->get_name() << "\n";
        
        NodePath bone = window->get_render().attach_new_node(joint->get_name());
        mcBundle->control_joint(joint->get_name(), bone.node());
        bone.set_mat(joint->get_default_value());
        collection->append(bone);
        
        addBones(bundle->get_child(i), collection);
    }
}

int main(int argc, char **argv)
{
    SendCharacterInit();
    framework.open_framework(argc, argv);

    const char *xmlFile = SAMPLE_XML_PATH;

	if (argc > 1)
	{
		xmlFile = argv[1];
	}

    setupNI(xmlFile);

    framework.set_window_title("Maker Ant Farm");
    window = framework.open_window();
    // Get the camera and store it in a variable.
    camera = window->get_camera_group();
    camera.set_pos(0,-12,2);
    camera.set_hpr(0, 0, 0);
 
    // Load the environment model.
//    NodePath environ = window->load_model(framework.get_models(), "models/environment");
    NodePath environ = window->load_model(framework.get_models(), "../new/MinecraftBody_bend.egg");
    environ.set_transparency(TransparencyAttrib::M_alpha);
    environ.set_pos(0,0,0);
    
    PT(Texture) tex;
    tex = TexturePool::load_texture("Char.png");
    tex->set_magfilter(Texture::FT_nearest);
    environ.set_texture(tex, 1);
    
    // Reparent the model to render.
    environ.reparent_to(window->get_render());
    // Apply scale and position transforms to the model.
 
    ModelRoot* eveN = (ModelRoot*)environ.node();
    NodePath eveChNP = environ.find("**/CharRig");      
    Character* eveCH = (Character*)eveChNP.node();
    mcBundle = eveCH->get_bundle(0);

//    NodePath bone = window->get_render().attach_new_node("head");
//    mcBundle->control_joint("head", bone.node());
//    CharacterJoint *joint = (CharacterJoint *)mcBundle->find_child("head");
//    bone.set_mat(joint->get_default_value());
//    std::cout << joint->get_default_value() << "\n";
//    taskMgr->add(new GenericAsyncTask("Moves a joint", &moveJoint, bone));

//    bone = window->get_render().attach_new_node("body");
//    mcBundle->control_joint("head", bone.node());
//    CharacterJoint *joint = (CharacterJoint *)mcBundle->find_child("head");
//    bone.set_mat(joint->get_default_value());
//    std::cout << joint->get_default_value() << "\n";

    NodePathCollection mcNodes = NodePathCollection();

    printChildren(environ);
    printCharacterChildren(mcBundle);
    addBones(mcBundle->find_child("<skeleton>"),&mcNodes);
 
    PT(PGEntry) input = new PGEntry("Name Input");
    input->setup(19, 1);
    input->set_focus(true);
    NodePath inputNP = window->get_aspect_2d().attach_new_node(input);
    framework.get_event_handler().add_hook(input->get_accept_event(KeyboardButton::enter()), acceptEntry, &inputNP);
    inputNP.set_scale(0.1);
    inputNP.set_pos(-0.9,0.0,-0.9);
//    inputNP.hide();
 
    text = new TextNode("Instructions");
    text->set_text("Looking for user...");
    NodePath textNodePath = window->get_aspect_2d().attach_new_node(text);
    textNodePath.set_scale(0.1);
    textNodePath.set_pos(-0.9,0.0,-0.75);
 
    // Add our task.
    // If we specify custom data instead of NULL, it will be passed as the second argument
    // to the task function.
//    taskMgr->add(new GenericAsyncTask("Spins the camera", &spinCameraTask, (void*) NULL));
    taskMgr->add(new GenericAsyncTask("Updates OpenNI data", &updateNI, &environ));
    taskMgr->add(new GenericAsyncTask("Moves a joint", &moveJoint, &mcNodes));
    window->enable_keyboard();

    framework.define_key("f1", "Reset", resetUsers, NULL);
 
    // Run the engine.
    framework.main_loop();
    // Shut down the engine when done.
    framework.close_framework();
    SendCharacterCleanup();
    return (0);
}
