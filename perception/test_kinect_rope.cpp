#include "basicobjects.h"
#include "bullet_io.h"
#include "clouds/comm_cv.h"
#include "clouds/comm_pcl.h"
#include "clouds/utils_cv.h"
#include "clouds/utils_pcl.h"
#include "comm/comm2.h"
#include "config.h"
#include "config_bullet.h"
#include "config_perception.h"
#include "make_bodies.h"
#include "rope.h"
#include "simplescene.h"
#include "trackers.h"
#include "utils_perception.h"
#include "vector_io.h"
#include "optimization_forces.h"
#include "visibility.h"
#include "apply_impulses.h"

#include <pcl/common/transforms.h>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

const float forceMultiplier = 1; // todo: set this in a sensible way based on 
// physical params and config option

struct CustomSceneConfig : Config {
  static int record;
  CustomSceneConfig() : Config() {
    params.push_back(new Parameter<int>("record", &record, "record every n frames (default 0 means record nothing)"));
  }
};

struct CustomScene : Scene {
  osgViewer::ScreenCaptureHandler* captureHandler;
  int framecount;
  int captureNumber;
  CustomScene() : Scene() {
   // add the screen capture handler
    framecount = 0;
    captureHandler = new osgViewer::ScreenCaptureHandler(new osgViewer::ScreenCaptureHandler::WriteToFile("screenshots/img", "jpg", osgViewer::ScreenCaptureHandler::WriteToFile::SEQUENTIAL_NUMBER));
    viewer.addEventHandler(captureHandler);
    //   captureHandler->startCapture();
  };
  void draw() {
    if (CustomSceneConfig::record && framecount % CustomSceneConfig::record==0) captureHandler->captureNextFrame(viewer);
    framecount++;
    Scene::draw();
  }

};

int CustomSceneConfig::record = 0;

int main(int argc, char *argv[]) {

  // command line options
  GeneralConfig::scale = 10;
  SceneConfig::enableIK = SceneConfig::enableHaptics = SceneConfig::enableRobot = false;
  
  Parser parser;
  parser.addGroup(GeneralConfig());
  parser.addGroup(BulletConfig());
  parser.addGroup(TrackingConfig());
  parser.addGroup(SceneConfig());
  parser.addGroup(CustomSceneConfig());
  parser.read(argc,argv);

  // comm stuff
  setDataRoot("/home/joschu/comm/rope_hands");
  FileSubscriber pcSub("kinect","pcd");
  CloudMessage cloudMsg;
  FileSubscriber ropeSub("rope_pts","pcd");
  CloudMessage ropeMsg;
  FileSubscriber labelSub("labels","png");
  ImageMessage labelMsg;
  FileSubscriber endSub("rope_ends","txt");
  VecVecMessage<float> endMsg;
  // load table
  /////////////// load table
  vector<btVector3> tableCornersCam = toBulletVectors(floatMatFromFile(onceFile("table_corners.txt").string()));
  CoordinateTransformer CT(getCamToWorldFromTable(tableCornersCam));
  vector<btVector3> tableCornersWorld = CT.toWorldFromCamN(tableCornersCam);
  BulletObject::Ptr table = makeTable(tableCornersWorld, .1*GeneralConfig::scale);
  table->setColor(1,1,1,.25);


  // load rope
  vector<btVector3> ropePtsCam = toBulletVectors(floatMatFromFile(onceFile("init_rope.txt").string()));
  CapsuleRope::Ptr rope(new CapsuleRope(CT.toWorldFromCamN(ropePtsCam), .0075*METERS));

  // plots
  PlotPoints::Ptr kinectPts(new PlotPoints(2));
  CorrPlots corrPlots;

  // setup scene
  CustomScene scene;
  scene.env->add(kinectPts);
  scene.env->add(rope);
  scene.env->add(table);
  scene.env->add(corrPlots.m_lines);


  // end tracker
  vector<RigidBodyPtr> rope_ends;
  rope_ends.push_back(rope->bodies[0]);
  rope_ends.push_back(rope->bodies[rope->bodies.size()-1]);
  MultiPointTrackerRigid endTracker(rope_ends,scene.env->bullet->dynamicsWorld);
  TrackerPlotter trackerPlotter(endTracker);
  scene.env->add(trackerPlotter.m_fakeObjects[0]);
  scene.env->add(trackerPlotter.m_fakeObjects[1]);

  scene.startViewer();
  scene.setSyncTime(true);
  scene.idle(true);

  int count=0;
  while (pcSub.recv(cloudMsg)) {
    ColorCloudPtr cloudCam  = cloudMsg.m_data;
    ColorCloudPtr cloudWorld(new ColorCloud());
    pcl::transformPointCloud(*cloudCam, *cloudWorld, CT.worldFromCamEigen);
    kinectPts->setPoints(cloudWorld);
    cout << "loaded cloud " << count << endl;
    count++;

    assert(ropeSub.recv(ropeMsg));
    vector<btVector3> obsPts = CT.toWorldFromCamN(toBulletVectors(ropeMsg.m_data));
    assert(labelSub.recv(labelMsg));
    cv::Mat labels = toSingleChannel(labelMsg.m_data);
    assert(endSub.recv(endMsg));
    vector<btVector3> newEnds = CT.toWorldFromCamN(toBulletVectors(endMsg.m_data));
    endTracker.update(newEnds);
    trackerPlotter.update();

    for (int iter=0; iter<TrackingConfig::nIter; iter++) {
      cout << "iteration " << iter << endl;
      vector<btVector3> estPts = rope->getNodes();
      cv::Mat ropeMask = toSingleChannel(labels) == 1;
      Eigen::MatrixXf ropePtsCam = toEigenMatrix(CT.toCamFromWorldN(estPts));
      Eigen::MatrixXf depthImage = getDepthImage(cloudCam);
      vector<float> pVis = calcVisibility(ropePtsCam, depthImage, ropeMask); 
      colorByVisibility(rope, pVis);
      SparseArray corr = calcCorrNN(estPts, obsPts, pVis);
      corrPlots.update(estPts, obsPts, corr);
      vector<btVector3> impulses = calcImpulsesSimple(estPts, obsPts, corr, forceMultiplier);
      applyImpulses(impulses, rope);
      scene.step(DT);

    }
  }
}
