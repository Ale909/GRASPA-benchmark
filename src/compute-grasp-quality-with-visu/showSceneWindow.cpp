#include "common.h"
#include "showSceneWindow.h"
#include "VirtualRobot/EndEffector/EndEffector.h"
#include "VirtualRobot/Workspace/Reachability.h"
#include "VirtualRobot/RuntimeEnvironment.h"
#include "VirtualRobot/ManipulationObject.h"
#include "VirtualRobot/XML/ObjectIO.h"
#include "VirtualRobot/XML/RobotIO.h"
#include "VirtualRobot/Grasping/GraspSet.h"
#include "VirtualRobot/Visualization/CoinVisualization/CoinVisualizationFactory.h"

#include <GraspPlanning/GraspStudio.h>
#include <GraspPlanning/GraspQuality/GraspQualityMeasureWrenchSpace.h>
#include <GraspPlanning/ApproachMovementSurfaceNormal.h>
#include <GraspPlanning/GraspQuality/GraspEvaluationPoseUncertainty.h>

#include <QFileDialog>
#include <Eigen/Geometry>

#include <ctime>
#include <vector>
#include <iostream>
#include <cmath>

#include "Inventor/actions/SoLineHighlightRenderAction.h"
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoLightModel.h>

#include <sstream>

showSceneWindow::showSceneWindow(std::string& scene_filename, std::string& robot_filename, std::string& grasps_path)
    : QMainWindow(nullptr)
{
    VR_INFO << " start " << std::endl;

    sceneFile = scene_filename;
    robotFile = robot_filename;
    graspsPath = grasps_path;

    eefVisu = nullptr;

    sceneSep = new SoSeparator;
    sceneSep->ref();
    sceneVisuSep = new SoSeparator;
    sceneSep->addChild(sceneVisuSep);
    robotSep = new SoSeparator;
    objectSep = new SoSeparator;
    graspsSep = new SoSeparator;
    graspsSep->ref();

    sceneSep->addChild(robotSep);
    sceneSep->addChild(objectSep);
    sceneSep->addChild(graspsSep);

    setupUI();

    loadScene();

    loadRobot();

    viewer->viewAll();
}

showSceneWindow::~showSceneWindow()
{
    sceneSep->unref();
    graspsSep->unref();
    eefVisu->unref();

    delete viewer;
}

void showSceneWindow::setupUI()
{
    UI.setupUi(this);
    viewer = new SoQtExaminerViewer(UI.frameViewer, "", TRUE, SoQtExaminerViewer::BUILD_POPUP);

    // setup
    viewer->setBackgroundColor(SbColor(1.0f, 1.0f, 1.0f));
    viewer->setAccumulationBuffer(true);
    viewer->setAntialiasing(true, 4);

    viewer->setGLRenderAction(new SoLineHighlightRenderAction);
    viewer->setTransparencyType(SoGLRenderAction::SORTED_OBJECT_BLEND);
    viewer->setFeedbackVisibility(true);
    viewer->setSceneGraph(sceneSep);
    viewer->viewAll();
    viewer->setAntialiasing(true, 4);

}

void showSceneWindow::closeEvent(QCloseEvent* event)
{
    quit();
    QMainWindow::closeEvent(event);
}

void showSceneWindow::buildVisu()
{
    if (!scene)
    {
        return;
    }

    sceneVisuSep->removeAllChildren();
    VirtualRobot::SceneObject::VisualizationType visuType = VirtualRobot::SceneObject::Full;

    visualization = scene->getVisualization<VirtualRobot::CoinVisualization>(visuType);
    SoNode* visualisationNode = nullptr;


    if (visualization)
    {
        visualisationNode = visualization->getCoinVisualization();
    }

    if (visualisationNode)
    {
        sceneVisuSep->addChild(visualisationNode);
    }

    visualizationRobot = eefCloned->getVisualization<VirtualRobot::CoinVisualization>(visuType);
    SoNode* visualisationNode2 = visualizationRobot->getCoinVisualization();

    if (visualisationNode2)
    {
        robotSep->addChild(visualisationNode2);
    }

    viewer->scheduleRedraw();

}

int showSceneWindow::main()
{

    //  make sure a scene and a robot have been loaded

    if (!scene)
    {
        std::cout << "Error: Could not load scene. Quitting..." << std::endl;
        return -1;
    }

    if (!robot)
    {
        std::cout << "Error: Could not load robot. Quitting..." << std::endl;
        return -1;
    }

    //  retrieve manipulation object list from the scene

    objects_in_scene = scene->getManipulationObjects();

    if (!objects_in_scene.size())
    {
        std::cout << "Error: No manipulation objects in the scene. Quitting..." << std::endl;
        return -1;
    }

    //  load objects and grasps for each object

    loadObjectsGrasps();

    //  display robot end effector list

    std::vector<VirtualRobot::EndEffectorPtr> robotEndEffectors = robot->getEndEffectors();

    common::listEndEffectors(robotEndEffectors);

    //  retrieve the first EEF that was used in the grasps
    //TODO: CHANGE THIS FOR EVERY GRASP

    for (VirtualRobot::ManipulationObjectPtr object : objects_in_scene_with_grasps)
    {

        std::cout << "Evaluating grasps for " << object->getName() << std::endl;

        //  extract grasps

        std::vector<VirtualRobot::GraspSetPtr> all_grasp_sets = object->getAllGraspSets();

        for (VirtualRobot::GraspSetPtr grasp_set : all_grasp_sets)
        {

            //VirtualRobot::GraspSetPtr grasp_set = object->getGraspSet(eef);
            std::vector < VirtualRobot::GraspPtr > grasp_vec = grasp_set->getGrasps();

            common::listGrasps(grasp_vec);

            //  create quality measure and calculate OWS

            GraspStudio::GraspQualityMeasureWrenchSpacePtr qualityMeasure( new GraspStudio::GraspQualityMeasureWrenchSpace(object));

            qualityMeasure->calculateObjectProperties();

            for ( VirtualRobot::GraspPtr grasp : grasp_vec)
            {
                //  get eef for each grasp

                std::string eef_name = grasp->getEefName();

                //  assume eef is present in robot config, otherwise will crash

                currentEEF = robot->getEndEffector(eef_name);

                //  extract preshape and assign it

                std::string preshape_name = grasp->getPreshapeName();
                currentEEF->setPreshape(preshape_name);

                //  get the pose of the eef TCP

                Eigen::Matrix4Xf mObject = object->getGlobalPose();

                Eigen::Matrix4Xf mGrasp = grasp->getTcpPoseGlobal(object->getGlobalPose());

                Eigen::Matrix4Xf pose = grasp->getTransformation();

                Eigen::Matrix4Xf tcp2object = pose.inverse() * mObject;

                std::cout << mObject << std::endl;

                std::cout << mGrasp << std::endl;

                std::cout << pose << std::endl;

                std::cout << tcp2object << std::endl;

                grasp->setTransformation(tcp2object);

                mGrasp = grasp->getTcpPoseGlobal(object->getGlobalPose());

                std::cout << mGrasp << std::endl;

                //  not sure what this one does

                GraspStudio::ApproachMovementSurfaceNormalPtr approach(new GraspStudio::ApproachMovementSurfaceNormal(object, currentEEF));

                //  clone the eef and place it according to the TCP in the grasp pose

//                VirtualRobot::RobotPtr eefCloned = approach->getEEFRobotClone();
                eefCloned = approach->getEEFRobotClone();
                eefCloned->setGlobalPoseForRobotNode(eefCloned->getEndEffector(eef_name)->getTcp(), mGrasp);

                //std::cout << eefCloned->getEndEffector(eef_name)->getTcp()->getGlobalPose() << std::endl;

                //  obtain contact information

                eefCloned->getEndEffector(eef_name)->openActors();

                VirtualRobot::EndEffector::ContactInfoVector contacts = eefCloned->getEndEffector(eef_name)->closeActors(object);

                eefVisu = VirtualRobot::CoinVisualizationFactory::CreateEndEffectorVisualization(eefCloned->getEndEffector(eef_name));
                eefVisu->ref();

                //  add the grasp to visualization

                Eigen::Matrix4f m = grasp->getTcpPoseGlobal(object->getGlobalPose());
                SoSeparator* sep_grasp = new SoSeparator();
                SoMatrixTransform* mt = VirtualRobot::CoinVisualizationFactory::getMatrixTransformScaleMM2M(m);
                sep_grasp->addChild(mt);
                sep_grasp->addChild(eefVisu);
                graspsSep->addChild(sep_grasp);

                //  compute pose quality

                qualityMeasure->setVerbose(false);
                qualityMeasure->setContactPoints(contacts);

                float grasp_quality = qualityMeasure->getGraspQuality();

                bool is_grasp_fc = qualityMeasure->isGraspForceClosure();

                std::cout << "Grasp " << grasp->getName() << (is_grasp_fc ? " IS " : " IS NOT ") << "in force closure" << std::endl;
                std::cout << "Quality: " << grasp_quality << std::endl;

                //  evaluate robustness for some default parameters

                int num_samples = 500;
                float max_pos_delta = 10.0;
                float max_ori_delta = 10.0;

                GraspStudio::GraspEvaluationPoseUncertainty::PoseUncertaintyConfig uncertainty_config;
                uncertainty_config.init(max_pos_delta, max_ori_delta);

                GraspStudio::GraspEvaluationPoseUncertaintyPtr uncertainty_eval(new GraspStudio::GraspEvaluationPoseUncertainty(uncertainty_config));

                std::vector<Eigen::Matrix4f> poses_eval = uncertainty_eval->generatePoses(object->getGlobalPose(), contacts, num_samples);

                GraspStudio::GraspEvaluationPoseUncertainty::PoseEvalResults results = uncertainty_eval->evaluatePoses(eefCloned->getEndEffector(eef_name), object, poses_eval, qualityMeasure);

                std::cout << "Robustness: " << std::endl;

                results.print();

                //EXPERIMENTAL: DISPLAY EEF


                //  open the actors after grasp

                //eefCloned->getEndEffector(eef_name)->openActors();

            }
        }
    }

    buildVisu();
    viewer->viewAll();


    //  show the window and start the visu loop

    SoQt::show(this);
    SoQt::mainLoop();
    return 0;

}

void showSceneWindow::quit()
{
    std::cout << "showSceneWindow: Closing" << std::endl;
    this->close();
    SoQt::exitMainLoop();
}

void showSceneWindow::loadScene()
{
    sceneVisuSep->removeAllChildren();

    std::cout << "Loading Scene from " << sceneFile << std::endl;

    scene.reset();

    try
    {
        scene = VirtualRobot::SceneIO::loadScene(sceneFile);
    }
    catch (VirtualRobot::VirtualRobotException& e)
    {
        std::cout << "Could not find valid scene in file " << sceneFile << std::endl;
        return;
    }

}

void showSceneWindow::loadRobot()
{

    std::cout << "Loading Robot from " << robotFile << std::endl;

    try
    {
        robot = VirtualRobot::RobotIO::loadRobot(robotFile);
    }
    catch (VirtualRobot::VirtualRobotException &exc)
    {
        std::cout << "Error: " << exc.what() << std::endl;
    }

}

void showSceneWindow::loadObjectsGrasps()
{

    //  Load objects in the scene and the associated grasps

    for (auto & obj : objects_in_scene)
    {
        std::cout << "Object: " << obj->getName() << std::endl;
        std::cout << obj->getGlobalPose() << std::endl;

        //  look for a filename in data/grasps with a specific name

        std::string man_obj_filename = graspsPath + "/" + obj->getName() + "_grasp.xml";
        VirtualRobot::RuntimeEnvironment::getDataFileAbsolute(man_obj_filename);
        VirtualRobot::ManipulationObjectPtr man_obj;

        //  load the manipulation object; continue to next object if invalid

        try
        {
            man_obj = VirtualRobot::ObjectIO::loadManipulationObject(man_obj_filename);
        }
        catch (VirtualRobot::VirtualRobotException &exc)
        {
            std::cout << "Error: " << exc.what() << std::endl;
            continue;
        }

        VirtualRobot::GraspSetPtr man_obj_scene_grasp_set = man_obj->getGraspSet(scene->getName());
        if (!man_obj_scene_grasp_set || man_obj_scene_grasp_set->getSize() < 1)
        {
            //  no grasp set with appropriate name found, or empty
            continue;
        }

        VirtualRobot::GraspPtr tmp_grasp = man_obj_scene_grasp_set->getGrasp(0);

        //std::cout << "Grasp pose before setPose: " << std::endl << tmp_grasp->getTransformation() << std::endl;

        man_obj->setGlobalPose(obj->getGlobalPose());
        //std::cout << "Object pose after setPose: " << std::endl << man_obj->getGlobalPose() << std::endl;
        //std::cout << "Grasp pose after setPose: " << std:: endl << tmp_grasp->getTcpPoseGlobal(man_obj->getGlobalPose()) << std::endl;

        objects_in_scene_with_grasps.push_back(man_obj);
    }


}
