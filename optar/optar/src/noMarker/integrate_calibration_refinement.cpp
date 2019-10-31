/**
 * @file
 *
 *
 * @author Carlo Rizzardo (crizz, cr.git.mail@gmail.com)
 *
 *
 * This ROS node allows you to integrate the information from the calibration refinement
 * in the actual calibration.
 * To make this work you will need to first load the network parameters and the
 * current calibration with rosparam:
 *  rosparm load opt_calibration/conf/camera_poses.yaml
 *  rosparm load opt_calibration/conf/camera_network.yaml
 *
 * The you can run this node:
 *  rosrun optar integrate_calibration_refinement
 *
 * This just updated the camera_poses.yaml file and moved the refinement files
 * (opt_calibration/conf/registration_*) so that they are not used anymore.
 * To actually use the new calibration you have to update the launch files, because
 * it seems the actual information is hardcoded in those.
 * To do so run this on the master PC:
 *  roslaunch opt_calibration detection_initializer.launch
 * and this on all the clients (I guess also on the master):
 *  roslaunch opt_calibration listener.launch
 *
 */

#include <ros/ros.h>
#include <ros/package.h>
#include <tf/tf.h>
#include <geometry_msgs/Pose.h>
#include <string>
#include <iostream>
#include <fstream>
#include <Eigen/Dense>
#include <eigen_conversions/eigen_msg.h>


#include <cstdio>

#include "../utils.hpp"

using namespace std;

/** name of the ROS node */
const string node_name = "integrate_calibration_refinement";
/** name of the output calibration file */
const string outputCalibrationFileName_parameterName = "calibration_file_name";
/** default name fo r the output calibration file */
string defaultOutputCalibrationFile;

/**
 * Gets the names of the sensprs from rosparam
 * @param  nodeHandle NodeHandle for this node
 * @return            The list of the sensors names
 */
vector<string> readSensorNamesFromParameterNetwork(const ros::NodeHandle& nodeHandle)
{
  //The structure is the one in camera_network.yaml
  // so:
  // network:
  // - pc: "PC1"
  //   sensors:
  //     - type: kinect2
  //       id: "kinect01"
  //       people_detector: hog
  // - pc: "PC2"
  //   sensors:
  //     - type: kinect2
  //       id: "kinect02"
  //       people_detector: hog
  vector<string> sensorNames;
  XmlRpc::XmlRpcValue network;
  int r = nodeHandle.getParam("/network", network);
  if(!r)
    return sensorNames;
  for (int i = 0; i < network.size(); i++)
  {
    for (int j = 0; j < network[i]["sensors"].size(); j++)
    {
      string sensorName = network[i]["sensors"][j]["id"];
      sensorNames.push_back(sensorName);
      ROS_INFO_STREAM("detected sensor "<<sensorName);
    }
  }
  return sensorNames;
}

/**
 * Struct containing a pose and its inverse, reflecting the opt_calibration files
 * @param pose        [description]
 * @param inversePose [description]
 */
struct PoseAndInverse
{
  /** the pose */
  tf::Pose pose;
  /** the inverse of #pose */
  tf::Pose inversePose;

  /**
   * Constructor
   * @param pose        [description]
   * @param inversePose [description]
   */
  PoseAndInverse(const tf::Pose& pose, const tf::Pose& inversePose) : pose(pose), inversePose(inversePose)
  {}
};

/**
 * Read the current calibration poses from rosparam
 * @param nodeHandle  Current nodeHandle
 * @param sensorNames Names of the sensors to search the poses of
 */
map<string,PoseAndInverse> readPosesFromParameterServer(const ros::NodeHandle& nodeHandle, const vector<string>& sensorNames)
{
  map<string,PoseAndInverse> poses;
  for(string sensorName : sensorNames)
  {
    std::string pose_s = "/poses/" + sensorName;
    std::string inverse_pose_s = "/inverse_poses/" + sensorName;

    int r = true;
    // Read pose:
    double x,y,z;
    r &= nodeHandle.getParam(pose_s + "/translation/x", x);
    r &= nodeHandle.getParam(pose_s + "/translation/y", y);
    r &= nodeHandle.getParam(pose_s + "/translation/z", z);

    double qx,qy,qz,qw;
    r &= nodeHandle.getParam(pose_s + "/rotation/x", qx);
    r &= nodeHandle.getParam(pose_s + "/rotation/y", qy);
    r &= nodeHandle.getParam(pose_s + "/rotation/z", qz);
    r &= nodeHandle.getParam(pose_s + "/rotation/w", qw);

    // Read inverse pose
    double ix,iy,iz;
    r &= nodeHandle.getParam(inverse_pose_s + "/translation/x", ix);
    r &= nodeHandle.getParam(inverse_pose_s + "/translation/y", iy);
    r &= nodeHandle.getParam(inverse_pose_s + "/translation/z", iz);

    double iqx,iqy,iqz,iqw;
    r &= nodeHandle.getParam(inverse_pose_s + "/rotation/x", iqx);
    r &= nodeHandle.getParam(inverse_pose_s + "/rotation/y", iqy);
    r &= nodeHandle.getParam(inverse_pose_s + "/rotation/z", iqz);
    r &= nodeHandle.getParam(inverse_pose_s + "/rotation/w", iqw);

    if(!r)
    {
      ROS_ERROR_STREAM("Could not get all the pose info for "<<sensorName);
    }
    else
    {
      ROS_INFO_STREAM("Found pose for sensor "<<sensorName);
      poses.insert(
        std::map<string, PoseAndInverse>::value_type(sensorName,
          PoseAndInverse(tf::Pose(tf::Quaternion(qx,qy,qz,qw),tf::Vector3(x,y,z)),
                         tf::Pose(tf::Quaternion(iqx,iqy,iqz,iqw),tf::Vector3(ix,iy,iz))                        )
          ));
    }

  }
  return poses;
}


/**
 * Writes the porvided sensor sposes to file in the opt_calibration format
 * @param  outputFileName name of the output file
 * @param  sensors        The sensors to write
 * @return                zero if successfull
 */
int writeSensorPoses(const string& outputFileName, const map<string,PoseAndInverse>& sensors)
{
  // Save tfs between sensors and world coordinate system (last checherboard) to file
  std::ofstream file;
  file.open(outputFileName.c_str());

  if (!file.is_open())
  {
    ROS_ERROR_STREAM("Couldn't open file at "<<outputFileName);
    return -1;
  }

  file << "# Auto generated file." << std::endl;
  file << "calibration_id: " << ros::Time::now().sec << std::endl << std::endl;

  // Write TF transforms between cameras and world frame in the user-defined reference frame:
  file << "# Poses w.r.t. the \"world\" reference frame" << std::endl;
  file << "poses:" << std::endl;
  for (auto sensor : sensors)
  {
    tf::Pose pose = sensor.second.pose;

    file << "  " << sensor.first << ":" << std::endl;

    file << "    translation:" << std::endl
         << "      x: " << pose.getOrigin().x() << std::endl
         << "      y: " << pose.getOrigin().y() << std::endl
         << "      z: " << pose.getOrigin().z() << std::endl;

    file << "    rotation:" << std::endl
         << "      x: " << pose.getRotation().x() << std::endl
         << "      y: " << pose.getRotation().y() << std::endl
         << "      z: " << pose.getRotation().z() << std::endl
         << "      w: " << pose.getRotation().w() << std::endl;
  }

  file << std::endl << "# Inverse poses" << std::endl;
  file << "inverse_poses:" << std::endl;
  for (auto sensor : sensors)
  {
    tf::Pose pose = sensor.second.inversePose;

    file << "  " << sensor.first << ":" << std::endl;

    file << "    translation:" << std::endl
         << "      x: " << pose.getOrigin().x() << std::endl
         << "      y: " << pose.getOrigin().y() << std::endl
         << "      z: " << pose.getOrigin().z() << std::endl;

    file << "    rotation:" << std::endl
         << "      x: " << pose.getRotation().x() << std::endl
         << "      y: " << pose.getRotation().y() << std::endl
         << "      z: " << pose.getRotation().z() << std::endl
         << "      w: " << pose.getRotation().w() << std::endl;
  }


  file.close();
  return 0;
}

/**
 * Read a matrix from a text file
 * @param  filename the file's name
 * @return          The matrix
 */
Eigen::Affine3d readMatrixFromFile (std::string filename)
{
  Eigen::Affine3d matrix;
  std::string line;
  std::ifstream myfile (filename.c_str());
  if (myfile.is_open())
  {
    std::string number;
    for(int row = 0; row<4; row++)
    {
      for(int col = 0; col<4; col++)
      {
        myfile >> number;
        if(!myfile)
          throw runtime_error("Invalid file syntax reading matrix column "+to_string(col)+" row "+to_string(row));
        matrix(row,col) = atof(number.c_str());
      }
    }
    /*
    int k=0;
    while (myfile >> number)
    {
      if (int(k/4) < matrix.Rows)
      {
        matrix(int(k/4), int(k%4)) = std::atof(number.c_str());
      }
      k++;
    }*/
    myfile.close();
  }
  else
  {
    throw runtime_error("Couldn't open file");
  }

  return matrix;
}





/**
 * Transform the pose of a sensor according to a calibration refinement file
 * @param  sensor             The sensor pose (this will be modified)
 * @param  refinementFileName The refinement file to use
 * @return                    The distance fo the new pose form the original one
 */
double transformSensor(PoseAndInverse& sensor, const string& refinementFileName)
{
  Eigen::Affine3d refinementMatrix = readMatrixFromFile(refinementFileName);
  geometry_msgs::Pose poseMsg;
  tf::poseEigenToMsg(refinementMatrix,poseMsg);
  tf::Pose poseTf;
  poseMsgToTF(poseMsg,poseTf);
  tf::Pose newPose = poseTf * sensor.pose;
  double dist = poseDistance(sensor.pose, newPose);
  sensor.pose = newPose;
  sensor.inversePose = sensor.pose.inverse();
  return dist;
}

/**
 * Copy a file
 * @param origin      source file name
 * @param destination destination file name
 */
void copyFile(const string& origin, const string& destination)
{
  std::ifstream  src(origin,        std::ios::binary);
  std::ofstream  dst(destination,   std::ios::binary);
  dst << src.rdbuf();
}


/**
 * Move a file
 * @param origin      source file name
 * @param destination destination file name
 */
void moveFile(const string& origin, const string& destination)
{
  {
    std::ifstream  src(origin,        std::ios::binary);
    std::ofstream  dst(destination,   std::ios::binary);
    dst << src.rdbuf();
  }
  remove(origin.c_str());
}

/**
 * Mainf fuinction for the node
 * @param  argc
 * @param  argv
 * @return      
 */
int main (int argc, char** argv)
{
  ros::init(argc, argv, node_name);
  ros::NodeHandle nh("~");
  defaultOutputCalibrationFile = ros::package::getPath("opt_calibration")+"/conf/camera_poses.yaml";

  string outputCalibrationFileName = defaultOutputCalibrationFile;
  bool r = nh.getParam(outputCalibrationFileName_parameterName, outputCalibrationFileName);
  if(!r)
  {
    cout<<"No output calibration file name provided will use "<<defaultOutputCalibrationFile<<endl;
  }



  vector<string> sensorNames = readSensorNamesFromParameterNetwork(nh);
  map<string, PoseAndInverse> sensors = readPosesFromParameterServer(nh,sensorNames);

  if(sensors.size()==0)
  {
      ROS_ERROR("Couldn't detect any sensor. Did you load the parameters?");
      ROS_ERROR("You should run these two commands:");
      ROS_ERROR("   rosparam load opt_calibration/conf/camera_poses.yaml");
      ROS_ERROR("   rosparam load opt_calibration/conf/camera_network.yaml");
  }

  vector<string> usedRefinementFiles;
  for(string sensorToBeTransformed : sensorNames)
  {
    auto sensor = sensors.find(sensorToBeTransformed);
    if(sensor!=sensors.end())
    {
      string refinementFileName = ros::package::getPath("opt_calibration")+"/conf/registration_"+sensorToBeTransformed+"_rgb_optical_frame.txt";
      string refinementFileName_ir = ros::package::getPath("opt_calibration")+"/conf/registration_"+sensorToBeTransformed+"_ir_optical_frame.txt";
      if(ifstream(refinementFileName).good())
      {
        double dist = transformSensor(sensor->second, refinementFileName);
        cout<<"Computed new pose for sensor "<<sensorToBeTransformed<<". The sensor has been moved by "<<dist<<"m"<<endl;
        usedRefinementFiles.push_back(refinementFileName);
        usedRefinementFiles.push_back(refinementFileName_ir);
      }
      else
      {
        ROS_ERROR_STREAM("couldn't access refinement file for sensor "<<sensorToBeTransformed<<" ("<<refinementFileName<<")");
        return -2;
      }
    }
    else
    {
      ROS_ERROR_STREAM("Couldn't find pose for sensor "<<sensorToBeTransformed);
      return -3;
    }
  }

  string now = to_string(ros::Time::now().sec);
  for(string refinementFile : usedRefinementFiles)
  {
    if(!ifstream(outputCalibrationFileName.c_str()).good())//if the file to be moved doesn't exist
      continue;
    string newFilename = refinementFile+"."+now+".bak";
    moveFile(refinementFile, newFilename);
  }
  cout<<"The refinement files have been moved to <original_name>."+now+".bak"<<endl;
  bool outFileAlreadyExist = ifstream(outputCalibrationFileName.c_str()).good();
  if(outFileAlreadyExist)//if the output file exists already make a backup
  {
    string backupCalibrationFile = outputCalibrationFileName+"."+now+".bak";
    copyFile(outputCalibrationFileName,backupCalibrationFile);
    cout<<"The old calibration has been saved to "<<backupCalibrationFile<<endl;
  }

  writeSensorPoses(outputCalibrationFileName,sensors);
  cout<<"New calibration saved to "<<outputCalibrationFileName<<endl;


  return 0;
}
