/*
* Author: Daniele Dal Degan [danieledaldegan@gmail.com]
*/

#include <ros/ros.h>
#include <tf/transform_listener.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PoseArray.h>
#include "std_msgs/String.h"


int main(int argc, char **argv)
{
  ros::init(argc, argv, "Origin_sender");
  ros::NodeHandle nh_origin;

  tf::TransformListener listener;   

  sleep(3.0);



  std::string nameTag;
  nh_origin.param("origin_sender/name_tag", nameTag, std::string("tag_0_arcore"));
  ROS_WARN("ORIGIN -> Got param name_tag: %s", nameTag.c_str());

  std::string output_topic;
  nh_origin.param("origin_sender/output_topic", output_topic, std::string("arcore/origin"));
  ROS_WARN("ORIGIN -> Got param output_topic: %s", output_topic.c_str());

  ros::Publisher vis_pub = nh_origin.advertise<geometry_msgs::PoseStamped>(output_topic.c_str(), 100);
  

  ros::Rate loop_rate_error(1);
  bool countClass = false;
  while(ros::ok())
  {   
    tf::StampedTransform transform;
    bool gotTransform = false;
    std::string targetFrame = nameTag;
    std::string sourceFrame = "/world";
    try
    {
      listener.waitForTransform(targetFrame, sourceFrame, ros::Time(0), ros::Duration(1));
      listener.lookupTransform(nameTag, sourceFrame, ros::Time(0), transform);
      gotTransform = true;
    }
    catch (tf::LookupException e)
    {
      ROS_WARN_STREAM("Frame doesn't currently exist: what()="<<e.what());
    }
    catch(tf::ConnectivityException e)
    {
      ROS_ERROR_STREAM("Frames are not connected: what()="<<e.what());
    }
    catch(tf::ExtrapolationException e)
    {
      ROS_WARN_STREAM("Centroid timestamp is too far off from now: what()="<<e.what());
    }
    catch(tf::TransformException e)
    {
      ROS_ERROR_STREAM(e.what());
    }

    if(gotTransform)
    {
      geometry_msgs::PoseStamped message; 
      message.header.frame_id = nameTag.c_str();
      message.header.stamp = ros::Time::now();
      message.pose.position.x = transform.getOrigin().x();
      message.pose.position.y = transform.getOrigin().y();
      message.pose.position.z = transform.getOrigin().z();
      message.pose.orientation.x = transform.getRotation().x();
      message.pose.orientation.y = transform.getRotation().y();
      message.pose.orientation.z = transform.getRotation().z();
      message.pose.orientation.w = transform.getRotation().w();
      vis_pub.publish(message);

      if(!countClass)
      {
        ROS_INFO("ORIGIN -> Write transformation: %s -> %s", nameTag.c_str(), "world");
        countClass = true;
      }
    }
    ros::spinOnce();
  }


  return 0;
}
