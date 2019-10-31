/**
 * @file
 *
 * @author Carlo Rizzardo (crizz, cr.git.mail@gmail.com)
 *
 * A simple ntp-like server for synchronizing nodes. Built for synchronizing applications
 * using rossharp, in particular rossharp on Unity on Android
 */

#include <ros/ros.h>
#include <opt_msgs/OptarNtpMessage.h>
#include <cv_bridge/cv_bridge.h>

/** Publisher for publishing the responses to the clients' queries */
ros::Publisher pub;

/**
 * Callback for incoming queries
 * @param inMsg Message from a client
 */
void callback(const opt_msgs::OptarNtpMessagePtr& inMsg)
{
	if(inMsg->type==opt_msgs::OptarNtpMessage::QUERY)
	{
		ROS_DEBUG_STREAM("received ntp query from "<<inMsg->id);
		opt_msgs::OptarNtpMessage response;
		response.type = opt_msgs::OptarNtpMessage::REPLY;
		response.serverTime = ros::Time::now();
		response.clientRequestTime = inMsg->clientRequestTime;
		response.id = inMsg->id;
		pub.publish(response);
		ROS_DEBUG_STREAM("handled ntp query from "<<inMsg->id<<"    partial time diff "<<(response.serverTime-response.clientRequestTime));
	}
}

/**
 * Main functin of the ROS node
 * @param  argc
 * @param  argv
 * @return
 */
int main(int argc, char** argv)
{
	ros::init(argc, argv, "ntp_server");
	ros::NodeHandle nh;

	ROS_INFO_STREAM("starting ntp_server");
	ros::Subscriber sub = nh.subscribe("/optar/ntp_chat", 1, callback);
	pub = nh.advertise<opt_msgs::OptarNtpMessage>("/optar/ntp_chat", 10);
	ros::spin();
}
