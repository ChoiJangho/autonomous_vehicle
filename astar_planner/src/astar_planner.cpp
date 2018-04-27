#include <iostream>
#include <cstring>
#include <ros/ros.h>
#include <ros/package.h>
#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <nav_msgs/OccupancyGrid.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Image.h>
#include <image_transport/image_transport.h>

#include "constants.h"
#include "helper.h"
#include "collisiondetection.h"
#include "dynamicvoronoi.h"
#include "algorithm.h"
#include "node3d.h"
#include "path.h"
#include "smoother.h"
#include "visualize.h"
#include "lookup.h"
#include "std_msgs/Int32.h"
#include "geometry_msgs/Pose2D.h"
#include "geometry_msgs/Vector3.h"
#include "core_msgs/Vector3DArray.h"
#include "core_msgs/MapFlag.h"
#include "core_msgs/VehicleState.h"
#include "core_msgs/MissionPark.h"
#include "opencv2/opencv.hpp"
#include <boost/thread.hpp>
#include <boost/bind.hpp>

#define Z_DEBUG true

float vel =10;//m/s
float delta = M_PI/10;//delta in radian
float map_resol;
float wheelbase = 1.6;
bool isparkmission = false;
int map_width, map_height;
int headings;


image_transport::Publisher publishMonintorMap;
sensor_msgs::ImagePtr msgMonitorMap;


using namespace HybridAStar;

class Astar
{
public:
  Astar();
  void plan(geometry_msgs::PoseWithCovarianceStamped start, geometry_msgs::PoseStamped goal);
  void initializeLookups();
  cv::Mat gridmap;
  /// The path produced by the hybrid A* algorithm

  /// The smoother used for optimizing the path
  Smoother smoother;
  /// The path smoothed and ready for the controller
  Path smoothedPath = Path(true);
  /// The visualization used for search visualization
  // Visualize visualization;

  /// The collission detection for testing specific configurations
  CollisionDetection configurationSpace;
  /// The voronoi diagram
  DynamicVoronoi voronoiDiagram;
  Path path;
  Constants::config collisionLookup[Constants::headings * Constants::positions];
  float* dubinsLookup = new float [Constants::headings * Constants::headings * Constants::dubinsWidth * Constants::dubinsWidth];
};

Astar::Astar() {
  if(Z_DEBUG) std::cout<<"Astart Created"<<std::endl;
}

void Astar::plan(geometry_msgs::PoseWithCovarianceStamped start, geometry_msgs::PoseStamped goal) {
      // ___________________________

      //TODO Getting Rid of grid in this whole library, replace it with pacakge!!!!!!
      //!!!!!!!

      int depth = headings;
      int length = map_width * map_height * depth;
      // define list pointers and initialize lists
      std::cout<<"length is "<<length<<std::endl;
      Node3D* nodes3D = new Node3D[length](); //200*200*12 = 480000
      Node2D* nodes2D = new Node2D[map_width * map_height]();

      // ________________________
      // retrieving goal position
      //this is in image frame
      float x = goal.pose.position.x;
      float y = goal.pose.position.y;
      float t = tf::getYaw(goal.pose.orientation);
      // set theta to a value (0,2PI]
      t = Helper::normalizeHeadingRad(t);
      std::cout<<"goal heading in rad is "<<t<<std::endl;
      const Node3D nGoal(x, y, t, 0, 0, nullptr);
      // retrieving start position
      x = start.pose.pose.position.x;
      y = start.pose.pose.position.y;
      t = tf::getYaw(start.pose.pose.orientation);
      // set theta to a value (0,2PI]
      t = Helper::normalizeHeadingRad(t);
      std::cout<<"start heading in rad is "<<t<<std::endl;

      Node3D nStart(x, y, t, 0, 0, nullptr);
      // ___________
      // DEBUG START
      //    Node3D nStart(108.291, 30.1081, 0, 0, 0, nullptr);

      std::cout<<"nStart and nGoal set up properly" <<std::endl;

      // ___________________________
      // START AND TIME THE PLANNING
      ros::Time plan_t0 = ros::Time::now();

      // CLEAR THE VISUALIZATION
      //visualization.clear();

      // CLEAR THE PATH
      path.clear();
      smoothedPath.clear();
      // FIND THE PATH
      Node3D* nSolution = Algorithm::hybridAStar(nStart, nGoal, nodes3D, nodes2D, map_width, map_height, configurationSpace, dubinsLookup);
      std::cout<<"nSolution is solved" <<std::endl;

      //DEBUG
      ros::Time plan_t1 = ros::Time::now();
      ros::Duration d1(plan_t1 - plan_t0);
      std::cout << "Planning Time in ms: " << d1 * 1000 << std::endl;


      if(nSolution==nullptr) std::cout<<"nSolution is Null Pointer" <<std::endl;
      // TRACE THE PATH (update the path in the smoother object)
      smoother.tracePath(nSolution);
      // CREATE THE UPDATED PATH
      path.updatePath(smoother.getPath());
      // SMOOTH THE PATH
      smoother.smoothPath(voronoiDiagram);
      // CREATE THE UPDATED PATH
      smoothedPath.updatePath(smoother.getPath());
      ros::Time plan_t2 = ros::Time::now();
      ros::Duration d2(plan_t2 - plan_t1);
      std::cout << "Smoothing Time in ms: " << d2 * 1000 << std::endl;

      // _________________________________
      // PUBLISH THE RESULTS OF THE SEARCH
      path.publishPath();
      smoothedPath.publishPath();

      delete [] nodes3D;
      delete [] nodes2D;
}

void Astar::initializeLookups() {
  Lookup::collisionLookup(collisionLookup);
}

/// A pointer to the grid the planner runs on
//nav_msgs::OccupancyGrid::Ptr grid;
/// The start pose set through RViz

void drawMonitorMap(Astar& astar) {
  for(int i = 0; i< astar.smoothedPath.getPath().pathpoints.size(); i++) {
    int px = (int)astar.smoothedPath.getPath().pathpoints.at(i).x;
    int py = (int)astar.smoothedPath.getPath().pathpoints.at(i).y;
    astar.gridmap.at<cv::Vec3b>(cv::Point(px,py)) = cv::Vec3b(0,255,0);
  }
  msgMonitorMap = cv_bridge::CvImage(std_msgs::Header(),"rgb8", astar.gridmap).toImageMsg();
  publishMonintorMap.publish(msgMonitorMap);

}

void callbackState(const core_msgs::VehicleStateConstPtr& msg_state) {
  //TODO:
}

void callbackPark(const core_msgs::MissionParkConstPtr& park_){
  //TODO
}


void callbackTerminate(const std_msgs::Int32Ptr& record) {
  //TODO:
}

void callbackMain(const boost::shared_ptr<core_msgs::MapFlag const> msg_map, Astar& astar)
{
  if(Z_DEBUG)  std::cout << "------------------------------------------------------------------" << std::endl;

  ros::Time map_time = msg_map->header.stamp; //the time when the map is recorded

  astar.initializeLookups();

  cv_bridge::CvImageConstPtr cv_ptr;
  try
  {
    boost::shared_ptr<void const> tracked_object_tmp;
    cv_ptr = cv_bridge::toCvShare(msg_map->frame, tracked_object_tmp);
  }
  catch (cv_bridge::Exception& e)
  {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    return;
  }
  astar.gridmap = cv_ptr->image.clone();
  astar.configurationSpace.updateGrid(astar.gridmap);
  //create array for Voronoi diagram
  ros::Time t0 = ros::Time::now();
  int height = msg_map->frame.height;
  int width = msg_map->frame.width;
  //std::cout<<"height and width of msg_map is "<<height<<", "<<width<<std::endl;
  bool** binMap;
  binMap = new bool*[width];

  for (int x = 0; x < width; x++) { binMap[x] = new bool[height]; }

  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      cv::Vec3b px_val = astar.gridmap.at<cv::Vec3b>(x,y);
      //if(x == width/2 && y == height/2) std::cout<<"the red pix value of map center is "<<(int)px_val.val[0]<<std::endl;
      binMap[x][y] = (int)px_val.val[0]==255 ? true : false;//val[2]: red
    }
  }
  //if(binMap[width/2][height/2]) std::cout<<"the center is red!!"<<std::endl;

  astar.voronoiDiagram.initializeMap(width, height, binMap);
  astar.voronoiDiagram.update();
  std::string vorono_path = ros::package::getPath("astar_planner")+"/config/result.ppm";
  const char* vorono_path_chr = vorono_path.c_str();
  astar.voronoiDiagram.visualize(vorono_path_chr);
  //free won't work, is it neccessary?
  //free((char*)vorono_path_chr);
  //DEBUG
  ros::Time t1 = ros::Time::now();
  ros::Duration d(t1 - t0);
  std::cout << "created Voronoi Diagram in ms: " << d * 1000 << std::endl;

  // assign the values to start from base_link
  geometry_msgs::PoseWithCovarianceStamped start;

  //setting start point
  //TODO: check how the plan() function use this start point
  //TODO: change this later according to it
  float yaw_delta = (float)(vel*delta*d.toSec())/wheelbase;
  std::cout<<"yaw_delta in rad is"<<yaw_delta<<std::endl;
  if(yaw_delta !=0) {
    start.pose.pose.position.x = map_width/2 - (float)(wheelbase*(1-cos(yaw_delta))/delta)/map_resol;
    start.pose.pose.position.y = map_height - (float)(wheelbase*sin(yaw_delta)/delta)/map_resol;
    std::cout<<"start x and y is ("<<start.pose.pose.position.x<<", "<<start.pose.pose.position.y<<")"<<std::endl;
  }
  else {
    start.pose.pose.position.x = map_width/2;
    start.pose.pose.position.y = map_height - vel*d.toSec()/map_resol;
    std::cout<<"start x and y is ("<<start.pose.pose.position.x<<", "<<start.pose.pose.position.y<<")"<<std::endl;
  }

  tf::Quaternion q = tf::createQuaternionFromRPY(0, 0, yaw_delta+M_PI_2+M_PI);
  tf::quaternionTFToMsg(q,start.pose.pose.orientation);


  //TODO: have to define the goal as the center of the lane of the farrest side
  geometry_msgs::PoseStamped goal;
  goal.pose.position.x = map_width/2;
  goal.pose.position.y = 1;
  tf::Quaternion q_goal = tf::createQuaternionFromRPY(0, 0, M_PI_2+M_PI);
  tf::quaternionTFToMsg(q_goal,goal.pose.orientation);
  std::cout<<"goal x and y is ("<<goal.pose.position.x<<", "<<goal.pose.position.y<<")"<<std::endl;


  std::cout << "start and goal set properly!! " << std::endl;

  astar.plan(start, goal);
  drawMonitorMap(astar);
}

int main(int argc, char** argv) {
  std::string config_path = ros::package::getPath("map_generator");
	cv::FileStorage params_config(config_path+"/config/system_config.yaml", cv::FileStorage::READ);
  float car_width = params_config["Vehicle.width"];
  float car_length = params_config["Vehicle.length"];
  map_width = params_config["Map.width"];
  map_height = params_config["Map.height"];
  std::cout<<"map_width and map_height are "<<map_width<<", "<<map_height<<std::endl;
  map_resol = params_config["Map.resolution"];
  headings = params_config["Path.headings"];
  wheelbase = params_config["Vehicle.wheelbase"];
  ros::init(argc, argv, "astar_planner");
  ros::start();
  Astar astar;
  // /map publish를 위한 설정 (publishMap & msgMap)
  ros::NodeHandle nh;
  // this is for monitoring
  image_transport::ImageTransport it(nh);
  publishMonintorMap = it.advertise("/monitor_map",1);
  msgMonitorMap.reset(new sensor_msgs::Image);

  ros::Subscriber stateSub = nh.subscribe("/vehicle_state",1,callbackState);
  ros::Subscriber parkingSub = nh.subscribe("/mission_park",1,callbackPark);
  //TODO:: for park mission, the message also tell us the target point

  ros::Subscriber mapSub = nh.subscribe<core_msgs::MapFlag>("/occupancy_map",1,boost::bind(callbackMain, _1, boost::ref(astar)));
  ros::Subscriber endSub = nh.subscribe("/end_system",1,callbackTerminate);

  //TODO: add v & delta service client
  ros::Rate loop_rate(20);
  ros::spin();
  return 0;
}
