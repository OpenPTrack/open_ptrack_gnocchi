

#include "ARDeviceRegistrationEstimator.hpp"
#include <opencv2/highgui/highgui.hpp>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <ros/ros.h>
#include <opt_msgs/ArcoreCameraImage.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <opencv2/xphoto.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/features2d/features2d.hpp>
#include <chrono>
#include <tf/transform_listener.h>
#include <Eigen/Dense>
#include <opencv2/calib3d.hpp>
#include <opencv2/core/eigen.hpp>
#include <thread>         // std::this_thread::sleep_for
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include <tf/transform_broadcaster.h>
#include <image_transport/image_transport.h>



#include "../utils.hpp"

using namespace std;
using namespace cv;

ARDeviceRegistrationEstimator::ARDeviceRegistrationEstimator(string ARDeviceId, ros::NodeHandle& nh, geometry_msgs::TransformStamped transformKinectToWorld, std::string debugImagesTopic, std::string fixed_sensor_name)
{
	this->transformKinectToWorld = transformKinectToWorld;
	this->ARDeviceId = ARDeviceId;
	this->fixed_sensor_name = fixed_sensor_name;

	pose_raw_pub = nh.advertise<geometry_msgs::PoseStamped>("optar/"+ARDeviceId+"/"+outputPoseRaw_topicName, 10);
	pose_marker_pub = nh.advertise<visualization_msgs::MarkerArray>("optar/"+ARDeviceId+"/"+outputPoseMarker_topicName, 1);

	image_transport::ImageTransport it(nh);
	matches_images_pub = it.advertise("optar/"+ARDeviceId+"/img_matches_"+fixed_sensor_name, 1);
	reproj_images_pub = it.advertise("optar/"+ARDeviceId+"/img_reprojection_"+fixed_sensor_name, 1);

}



void ARDeviceRegistrationEstimator::setupParameters(double pnpReprojectionError,
					double pnpConfidence,
					double pnpIterations,
					double matchingThreshold,
					double reprojectionErrorDiscardThreshold,
					int orbMaxPoints,
					double orbScaleFactor,
					int orbLevelsNumber,
					double phoneOrientationDifferenceThreshold_deg,
					bool showImages,
					unsigned int minimumMatchesNumber)
{
	this->pnpReprojectionError=pnpReprojectionError;
	this->pnpConfidence=pnpConfidence;
	this->pnpIterations=pnpIterations;
	this->matchingThreshold=matchingThreshold;
	this->reprojectionErrorDiscardThreshold=reprojectionErrorDiscardThreshold;
	this->orbMaxPoints=orbMaxPoints;
	this->orbScaleFactor=orbScaleFactor;
	this->orbLevelsNumber=orbLevelsNumber;
	this->phoneOrientationDifferenceThreshold_deg=phoneOrientationDifferenceThreshold_deg;
	this->showImages = showImages;
	this->minimumMatchesNumber = minimumMatchesNumber;

}


int ARDeviceRegistrationEstimator::featuresCallback(const opt_msgs::ArcoreCameraFeaturesConstPtr& arcoreInputMsg,
					const sensor_msgs::ImageConstPtr& kinectInputCameraMsg,
					const sensor_msgs::ImageConstPtr& kinectInputDepthMsg,
					const sensor_msgs::CameraInfo& kinectCameraInfo)
{
	std::chrono::steady_clock::time_point beginning = std::chrono::steady_clock::now();
	long arcoreTime = arcoreInputMsg->header.stamp.sec*1000000000L + arcoreInputMsg->header.stamp.nsec;
	long kinectTime = kinectInputCameraMsg->header.stamp.sec*1000000000L + kinectInputCameraMsg->header.stamp.nsec;	
	ROS_INFO_STREAM("Parameters: "<<endl<<
            "pnp iterations = "<<pnpIterations<<endl<<
            "pnp confidence = "<<pnpConfidence<<endl<<
            "pnp reporjection error = "<<pnpReprojectionError<<endl<<
            "matching threshold ="<<matchingThreshold<<endl<<
			"reporjection discard threshold =" <<reprojectionErrorDiscardThreshold<<endl<<
			"orb max points = "<<orbMaxPoints<<endl<<
			"orb scale factor = "<<orbScaleFactor<<endl<<
			"orb levels number = "<<orbLevelsNumber<<endl<<
			"phone orientation difference threshold = "<<phoneOrientationDifferenceThreshold_deg<<endl<<
			"show images = "<<showImages);

	ROS_DEBUG("Received images. time diff = %+7.5f sec.  arcore time = %012ld  kinect time = %012ld",(arcoreTime-kinectTime)/1000000000.0, arcoreTime, kinectTime);






	//:::::::::::::::Decode received images and stuff::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

	int r;



	cv::Mat arcoreCameraMatrix;
	cv::Mat arcoreDescriptors;
	std::vector<cv::KeyPoint>  arcoreKeypoints;
	cv::Size arcoreImageSize;
	cv::Mat kinectCameraMatrix;
	cv::Mat kinectCameraImg;
	cv::Mat kinectDepthImg;
	tf::Pose phonePoseArcoreFrameConverted;
	cv::Mat arcoreImage;
	r = readReceivedMessages_features(arcoreInputMsg,kinectInputCameraMsg,kinectInputDepthMsg,kinectCameraInfo,
					arcoreCameraMatrix,
					arcoreDescriptors,
					arcoreKeypoints,
					arcoreImageSize,
					kinectCameraMatrix,
					kinectCameraImg,
					kinectDepthImg,
					phonePoseArcoreFrameConverted,
					arcoreImage);
	if(r<0)
	{
		ROS_ERROR("Invalid input messages. Dropping frame");
		return -1;
	}



	//:::::::::::::::Compute the features in the images::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::










	//find matches
	std::vector<cv::KeyPoint>  kinectKeypoints;
	cv::Mat kinectDescriptors;
	r =computeOrbFeatures(kinectCameraImg, kinectKeypoints, kinectDescriptors);
	if(r<0)
	{
		ROS_ERROR("error computing camera features");
		return -2;
	}

	r = 10*update(	arcoreKeypoints,
				arcoreDescriptors,
				kinectKeypoints,
				kinectDescriptors,
				arcoreImageSize,
				kinectCameraImg.size(),
				arcoreCameraMatrix,
				kinectCameraMatrix,
				kinectDepthImg,
				kinectCameraImg,
				arcoreImage,
				phonePoseArcoreFrameConverted,
				arcoreInputMsg->header.stamp,
				kinectInputCameraMsg->header.frame_id);


	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	unsigned long totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - beginning).count();
	ROS_INFO("total duration is %lu ms",totalDuration);

	
	return r;
}

int ARDeviceRegistrationEstimator::imagesCallback(const opt_msgs::ArcoreCameraImageConstPtr& arcoreInputMsg,
					const sensor_msgs::ImageConstPtr& kinectInputCameraMsg,
					const sensor_msgs::ImageConstPtr& kinectInputDepthMsg,
					const sensor_msgs::CameraInfo& kinectCameraInfo)
{
	std::chrono::steady_clock::time_point beginning = std::chrono::steady_clock::now();
	long arcoreTime = arcoreInputMsg->header.stamp.sec*1000000000L + arcoreInputMsg->header.stamp.nsec;
	long kinectTime = kinectInputCameraMsg->header.stamp.sec*1000000000L + kinectInputCameraMsg->header.stamp.nsec;
	ROS_DEBUG("Received images. time diff = %+7.5f sec.  arcore time = %012ld  kinect time = %012ld",(arcoreTime-kinectTime)/1000000000.0, arcoreTime, kinectTime);





	//:::::::::::::::Decode received images and stuff::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

	int r;



	cv::Mat arcoreCameraMatrix;
	cv::Mat kinectCameraMatrix;
	cv::Mat arcoreImg;
	cv::Mat kinectCameraImg;
	cv::Mat kinectDepthImg;
	tf::Pose phonePoseArcoreFrameConverted;
	r = readReceivedImageMessages( arcoreInputMsg,
					kinectInputCameraMsg,
					kinectInputDepthMsg,
					kinectCameraInfo,
					arcoreCameraMatrix,
					arcoreImg,
					kinectCameraMatrix,
					kinectCameraImg,
					kinectDepthImg,
					phonePoseArcoreFrameConverted);
	if(r<0)
	{
		ROS_ERROR("Invalid input messages. Dropping frame");
		return -1;
	}



	//:::::::::::::::Compute the features in the images::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::










	//find matches
	std::vector<cv::KeyPoint>  arcoreKeypoints;
	cv::Mat arcoreDescriptors;
	r = computeOrbFeatures(arcoreImg, arcoreKeypoints, arcoreDescriptors);
	if(r<0)
	{
		ROS_ERROR("error computing arcore features");
		return -2;
	}
	std::vector<cv::KeyPoint>  kinectKeypoints;
	cv::Mat kinectDescriptors;
	r =computeOrbFeatures(kinectCameraImg, kinectKeypoints, kinectDescriptors);
	if(r<0)
	{
		ROS_ERROR("error computing camera features");
		return -3;
	}

	r = 10*update (	arcoreKeypoints,
				arcoreDescriptors,
				kinectKeypoints,
				kinectDescriptors,
				arcoreImg.size(),
				kinectCameraImg.size(),
				arcoreCameraMatrix,
				kinectCameraMatrix,
				kinectDepthImg,
				kinectCameraImg,
				arcoreImg,
				phonePoseArcoreFrameConverted,
				arcoreInputMsg->header.stamp,
				kinectInputCameraMsg->header.frame_id);


	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	unsigned long totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(end - beginning).count();
	ROS_INFO("total duration is %lu ms",totalDuration);

	
	return r;
}


/**
 * @brief      Estimates the transformation using the descriptors and keypoints from the fixed camera and the AR device
 * 				if ARDeviceRegistrationEstimator#showImages is set it will also publish an image showing the matches between the images
 *
 * @param[in]  arcoreKeypoints                The keypoints in the AR device image
 * @param[in]  arcoreDescriptors              The descriptors corrwsponding to the AR device keypoints
 * @param[in]  fixedKeypoints                 The keypoints in the fixed camera image
 * @param[in]  fixedDescriptors               The descriptors corresponding to the fixed camera keypoints
 * @param[in]  arcoreImageSize                The pixel size of the AR device image
 * @param[in]  kinectImageSize                The pixel size of the fixed camera image
 * @param[in]  arcoreCameraMatrix             The camera matrix for the AR device camera
 * @param[in]  fixedCameraMatrix              The camera matrix for the fixed camera
 * @param      kinectDepthImage               The fixed camera depth image
 * @param[in]  kinectMonoImage                The fixed camera monochrome image
 * @param[in]  arcoreImageDbg                 The image from the AR device camera, only here for debug purpouses.You can 
 * 												pass a null Mat if you want. The idea is to pass here a super-low resolution
 * 												image to display in the matches_img
 * @param[in]  phonePoseArcoreFrameConverted  The AR device pose in the AR coordinate frame
 * @param[in]  timestamp                      The timestamp of the images. The images should be all roughly from the same instant
 * @param[in]  fixedCameraFrameId             The fixed camera tf frame_id
 *
 * @return     returns zero in case of success, a negative value in case of an internal error, a positive value greater than zero if
 * 				it couldn't determine the transformation because the device is looking at something too different to what the fixed camera is seeing.
 */
int ARDeviceRegistrationEstimator::update(	const std::vector<cv::KeyPoint>& arcoreKeypoints,
				const cv::Mat& arcoreDescriptors,
				const std::vector<cv::KeyPoint>& fixedKeypoints,
				const cv::Mat& fixedDescriptors,
				const cv::Size& arcoreImageSize,
				const cv::Size& kinectImageSize,
				const cv::Mat& arcoreCameraMatrix,
				const cv::Mat& fixedCameraMatrix,
				cv::Mat& kinectDepthImage,
				const cv::Mat& kinectMonoImage,
				const cv::Mat& arcoreImageDbg,
				const tf::Pose& phonePoseArcoreFrameConverted,
				const ros::Time& timestamp,
				const std::string fixedCameraFrameId)
{

	//if arcoreImage is not set just use a black image, it's just for visualization
	cv::Mat arcoreImage;
	if(!arcoreImageDbg.data)
		arcoreImage = cv::Mat(arcoreImageSize.height, arcoreImageSize.width, CV_8UC1, Scalar(0,0,0));
	else
		arcoreImage = arcoreImageDbg;


	std::chrono::steady_clock::time_point beforeMatching = std::chrono::steady_clock::now();


	std::vector<cv::DMatch> matches;
	int r = findOrbMatches(arcoreKeypoints, arcoreDescriptors, fixedKeypoints, fixedDescriptors, matches);
	if(r<0)
	{
		ROS_ERROR("error finding matches");
		return -1;
	}
	ROS_DEBUG_STREAM("got "<<matches.size()<<" matches");

  //filter matches
	std::vector< cv::DMatch > goodMatchesWithNull;
	r = filterMatches(matches,goodMatchesWithNull);
	if(r<0)
	{
		ROS_ERROR("error filtering matches");
		return -2;
	}
	ROS_DEBUG("Got %lu good matches, but some could be invalid",goodMatchesWithNull.size());

  	//On the kinect side the depth could be zero at the match location
  	//we try to get the nearest non-zero depth, if it's too far we discard the match
	std::vector< cv::DMatch > goodMatches;

	r = fixMatchesDepthOrDrop(goodMatchesWithNull, fixedKeypoints, kinectDepthImage ,goodMatches);
	if(r<0)
	{
		ROS_ERROR("error fixing matches depth");
		return -3;
	}
	ROS_INFO_STREAM("got "<<goodMatches.size()<<" actually good matches");



	std::chrono::steady_clock::time_point afterMatchesComputation = std::chrono::steady_clock::now();
	unsigned long matchesComputationDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterMatchesComputation - beforeMatching).count();
	//ROS_DEBUG("Matches computation took %lu ms",matchesComputationDuration);







	//:::::::::::::::Find the 3d position of the matches::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::












	//find the 3d poses corresponding to the goodMatches, these will be relative to the kinect frame
	std::vector<cv::Point3f> goodMatches3dPos;
	std::vector<cv::Point2f> goodMatchesImgPos;

	r = get3dPositionsAndImagePositions(goodMatches,fixedKeypoints, arcoreKeypoints, kinectDepthImage,fixedCameraMatrix,goodMatches3dPos, goodMatchesImgPos);
	if(r<0)
	{
  		ROS_ERROR("error getting matching points");
  		return -4;
	}


	std::chrono::steady_clock::time_point after3dpositionsComputation = std::chrono::steady_clock::now();
	unsigned long _3dPositionsComputationDuration = std::chrono::duration_cast<std::chrono::milliseconds>(after3dpositionsComputation - afterMatchesComputation).count();
	//ROS_DEBUG("3D positions computation took %lu ms",_3dPositionsComputationDuration);



	//used to publish visualizations to rviz
	visualization_msgs::MarkerArray markerArray;
	for(unsigned int i=0; i<goodMatches3dPos.size(); i++)//build the rviz markers
	{
		cv::Point3f pos3d = goodMatches3dPos.at(i);
		markerArray.markers.push_back( buildMarker(	pos3d,"match"+std::to_string(i),0,0,1,1, 0.2, fixedCameraFrameId));//matches are blue
	}
	pose_marker_pub.publish(markerArray);
	cv::Mat matchesImg;
	if(showImages)
	{
		cv::drawMatches(arcoreImage, arcoreKeypoints, kinectMonoImage, fixedKeypoints, goodMatches, matchesImg, cv::Scalar::all(-1), cv::Scalar::all(-1), std::vector<char>(), cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
		cv::putText(matchesImg, std::to_string(goodMatches.size()).c_str(),cv::Point(0,matchesImg.rows-5),FONT_HERSHEY_SIMPLEX,2,Scalar(255,0,0),3);
	}

	//If we have less than 4 matches we cannot procede, pnp wouldn't be able to estimate the phone position
	if(goodMatches.size()<4 || goodMatches.size()<minimumMatchesNumber)
	{
		//cv::drawKeypoints(arcoreImg,arcoreKeypoints)
		if(matchesImg.data)
		{
			sensor_msgs::ImagePtr msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", matchesImg).toImageMsg();
			matches_images_pub.publish(msg);
		}
		ROS_WARN("not enough matches to determine position");
		return 1;
	}









	//:::::::::::::::Determine the phone position::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::











	ROS_DEBUG_STREAM("arcoreCameraMatrix = \n"<<arcoreCameraMatrix);
	cv::Vec3d tvec;
	cv::Vec3d rvec;
	if(didComputeEstimation) //initialize with the previous estimate
	{
		tf::Pose lastEstimateTf;
		tf::transformMsgToTF(lastEstimate.transform,lastEstimateTf);
		tfPoseToOpenCvPose(lastEstimateTf, rvec, tvec);
	}
	std::vector<int> inliers;
	ROS_DEBUG_STREAM("Running pnpRansac with iterations="<<pnpIterations<<" pnpReprojectionError="<<pnpReprojectionError<<" pnpConfidence="<<pnpConfidence);
	cv::solvePnPRansac(	goodMatches3dPos,goodMatchesImgPos,
						arcoreCameraMatrix,cv::noArray(),
						rvec,tvec,
						didComputeEstimation,
						pnpIterations,
						pnpReprojectionError,
						pnpConfidence,
						inliers);

	ROS_DEBUG_STREAM("solvePnPRansac used "<<inliers.size()<<" inliers and says:\t tvec = "<<tvec.t()<<"\t rvec = "<<rvec.t());

	std::chrono::steady_clock::time_point afterPnpComputation = std::chrono::steady_clock::now();
	unsigned long pnpComputationDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterPnpComputation - after3dpositionsComputation).count();
	//ROS_DEBUG("PNP computation took %lu ms",pnpComputationDuration);

	//reproject points to then check reprojection error (and visualize them)
	std::vector<Point2f> reprojPoints;
	cv::projectPoints 	(goodMatches3dPos,
						rvec, tvec,
						arcoreCameraMatrix,
						cv::noArray(),
						reprojPoints);



	cv::Mat reprojectionImg;
	if(showImages)
		cvtColor(arcoreImage, reprojectionImg, CV_GRAY2RGB);
	double reprojError = 0;
	//calculate reprojection error mean and draw reprojections
	for(unsigned int i=0;i<inliers.size();i++)
	{
		Point2f pix = goodMatchesImgPos.at(inliers.at(i));
		Point2f reprojPix = reprojPoints.at(inliers.at(i));
		reprojError += hypot(pix.x-reprojPix.x, pix.y-reprojPix.y)/reprojPoints.size();

		if(reprojectionImg.data)
		{
			int r = ((double)rand())/RAND_MAX*255;
			int g = ((double)rand())/RAND_MAX*255;
			int b = ((double)rand())/RAND_MAX*255;
			Scalar color = Scalar(r,g,b);
			cv::circle(reprojectionImg,pix,15,color,5);
			cv::line(reprojectionImg,pix,reprojPix,color,3);
		}
	}
	cv::putText(reprojectionImg, std::to_string(goodMatches.size()).c_str(),cv::Point(0,reprojectionImg.rows-5),FONT_HERSHEY_SIMPLEX,2,Scalar(255,0,0),3);

	ROS_INFO_STREAM("inliers reprojection error = "<<reprojError);


	std::chrono::steady_clock::time_point reprojectionComputation = std::chrono::steady_clock::now();
	unsigned long reprojectionComputationDuration = std::chrono::duration_cast<std::chrono::milliseconds>(reprojectionComputation- afterPnpComputation).count();
	ROS_DEBUG("Reprojection error computation took %lu ms",reprojectionComputationDuration);

	if(showImages && matchesImg.data)
	{
		sensor_msgs::ImagePtr msgMatches = cv_bridge::CvImage(std_msgs::Header(), "bgr8", matchesImg).toImageMsg();
		matches_images_pub.publish(msgMatches);
	}
	if(showImages && reprojectionImg.data)
	{
		sensor_msgs::ImagePtr msgReproj = cv_bridge::CvImage(std_msgs::Header(), "bgr8", reprojectionImg).toImageMsg();
		reproj_images_pub.publish(msgReproj);
	}


	std::chrono::steady_clock::time_point afterDrawMatches = std::chrono::steady_clock::now();
	unsigned long drawMatchesDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterDrawMatches - reprojectionComputation).count();
	ROS_DEBUG("draw matches took %lu ms",drawMatchesDuration);


	//discard bad frames
	if(reprojError>reprojectionErrorDiscardThreshold)
	{
		ROS_WARN("Reprojection error beyond threshold, discarding frame");
		return 2;
	}

	//convert to ros format
	Eigen::Vector3d position;
	Eigen::Quaterniond rotation;
	opencvPoseToEigenPose(rvec,tvec,position,rotation);
	geometry_msgs::Pose poseNotStamped = buildRosPose(position,rotation);
	//invert the pose, because that's what you do
	tf::Pose poseTf;
	tf::poseMsgToTF(poseNotStamped,poseTf);
	tf::poseTFToMsg(poseTf.inverse(),poseNotStamped);
	//make it stamped
	geometry_msgs::PoseStamped phonePoseKinect;
	phonePoseKinect.pose = poseNotStamped;
	phonePoseKinect.header.frame_id = "kinect01_rgb_optical_frame";
	phonePoseKinect.header.stamp = timestamp;
	//transform to world frame
	geometry_msgs::PoseStamped phonePose;
	tf2::doTransform(phonePoseKinect,phonePose,transformKinectToWorld);
	phonePose.header.frame_id = "/world";

	//ROS_INFO_STREAM("estimated pose before transform: "<<phonePoseKinect.pose.position.x<<" "<<phonePoseKinect.pose.position.y<<" "<<phonePoseKinect.pose.position.z<<" ; "<<phonePoseKinect.pose.orientation.x<<" "<<phonePoseKinect.pose.orientation.y<<" "<<phonePoseKinect.pose.orientation.z<<" "<<phonePoseKinect.pose.orientation.w);

	pose_raw_pub.publish(phonePose);
	//publishPoseAsTfFrame(phonePose,ARDeviceId+"/"+"mobile_pose");


	ROS_DEBUG_STREAM("estimated pose is                "<<phonePose.pose.position.x<<" "<<phonePose.pose.position.y<<" "<<phonePose.pose.position.z<<" ; "<<phonePose.pose.orientation.x<<" "<<phonePose.pose.orientation.y<<" "<<phonePose.pose.orientation.z<<" "<<phonePose.pose.orientation.w);










	//:::::::::::::::Get arcore world frame of reference::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::











	tf::Pose phonePoseTf;
	tf::poseMsgToTF(phonePose.pose,phonePoseTf);
	ROS_DEBUG_STREAM("phonePoseTf = "<<poseToString(phonePoseTf));
	//Dim:
	// let phonePoseArcoreFrameConverted = Pa
	// let arcoreWorld = A
	// let phonePoseTf = Pr
	//
	// A*Pa*0 = Pr*0
	// so:
	// A*Pa = Pr
	// so:
	// A*Pa*Pa^-1 = Pr*Pa^-1
	// so:
	// A = Pr*Pa^-1
	tf::Pose arcoreWorld = phonePoseTf * phonePoseArcoreFrameConverted.inverse();
	//publishTransformAsTfFrame(arcoreWorld,ARDeviceId+"_world","/world",arcoreInputMsg->header.stamp);
	//publishTransformAsTfFrame(phonePoseArcoreFrameConverted,ARDeviceId+"/"+"phone_arcore_arcore","/arcore_world",arcoreInputMsg->header.stamp);
	//publishTransformAsTfFrame(phonePoseArcoreFrameConverted.inverse(),ARDeviceId+"/"+"phone_arcore_conv_inv","/phone_estimate",arcoreInputMsg->header.stamp);


	if(!isPoseValid(arcoreWorld))
	{
		ROS_WARN_STREAM("Dropping transform estimation as it is invalid");
		return -8;
	}

	//compute orientation difference with respect to the fixed camera
	tf::Pose phonePoseTf_kinectFrame;
	tf::poseMsgToTF(phonePoseKinect.pose,phonePoseTf_kinectFrame);
	tf::Vector3 zUnitVector(0,0,1);
	tf::Vector3 phoneOpticalAxis_kinectFrame = tf::Transform(phonePoseTf_kinectFrame.getRotation()) * zUnitVector;
	double phoneToCameraRotationAngle = std::abs(phoneOpticalAxis_kinectFrame.angle(zUnitVector))*180/3.14159;
	//ROS_INFO_STREAM("phoneOpticalAxis_kinectFrame = "<<phoneOpticalAxis_kinectFrame.x()<<" "<<phoneOpticalAxis_kinectFrame.y()<<" "<<phoneOpticalAxis_kinectFrame.z());
	ROS_DEBUG_STREAM("Angle = "<<phoneToCameraRotationAngle);
	if(phoneToCameraRotationAngle>phoneOrientationDifferenceThreshold_deg)
	{
		ROS_INFO_STREAM("Orientation difference between phone and camera is too high, discarding estimation ("<<phoneToCameraRotationAngle<<")");
		return 3;
	}

	publishTransformAsTfFrame(phonePoseTf,ARDeviceId+"_estimate_"+fixed_sensor_name,"/world",timestamp);
	publishTransformAsTfFrame(arcoreWorld,ARDeviceId+"_world_"+fixed_sensor_name,"/world",timestamp);


	tf::StampedTransform stampedTransform(arcoreWorld, timestamp, "/world", getARDeviceId()+"_world");
	geometry_msgs::TransformStamped stampedTransformMsg;
	tf::transformStampedTFToMsg(stampedTransform,stampedTransformMsg);

	lastEstimate = stampedTransformMsg;
	lastEstimateMatchesNumber = goodMatches.size();
	lastEstimateReprojectionError = reprojError;
	didComputeEstimation=true;

	ROS_DEBUG_STREAM("matchesComputationDuration="<<matchesComputationDuration);
	ROS_DEBUG_STREAM("_3dPositionsComputationDuration="<<_3dPositionsComputationDuration);
	ROS_DEBUG_STREAM("pnpComputationDuration="<<pnpComputationDuration);
	ROS_DEBUG_STREAM("reprojectionComputationDuration="<<reprojectionComputationDuration);
	ROS_DEBUG_STREAM("drawMatchesDuration="<<drawMatchesDuration);
	return 0;
}













int ARDeviceRegistrationEstimator::computeOrbFeatures(const cv::Mat& image,
					std::vector<cv::KeyPoint>& keypoints,
					cv::Mat& descriptors)
{
	keypoints.clear();
	cv::Ptr<cv::ORB> orb;

    orb = cv::ORB::create(orbMaxPoints,orbScaleFactor,orbLevelsNumber);


    orb->detect(image, keypoints);
	if ( keypoints.empty() )
	{
		ROS_ERROR("No keypoints found");
		return -1;
	}

	orb->compute(image,keypoints,descriptors);
	if ( descriptors.empty() )
	{
		ROS_ERROR("No descriptors");
		return -2;
	}
	return 0;
}



int ARDeviceRegistrationEstimator::findOrbMatches(	const std::vector<cv::KeyPoint>& arcoreKeypoints,
													const cv::Mat& arcoreDescriptors,
													const std::vector<cv::KeyPoint>& kinectKeypoints,
													const cv::Mat& kinectDescriptors,
													std::vector<cv::DMatch>& matches)
{

  	//find matches between the descriptors
   	std::chrono::steady_clock::time_point beforeMatching = std::chrono::steady_clock::now();
    cv::BFMatcher::create(cv::NORM_HAMMING)->match(arcoreDescriptors,kinectDescriptors,matches);//arcore is query, kinect is trains
   	std::chrono::steady_clock::time_point afterMatching = std::chrono::steady_clock::now();
	unsigned long matchingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterMatching - beforeMatching).count();
	ROS_DEBUG("Descriptors matching took %lu ms",matchingDuration);
	return 0;
}

int ARDeviceRegistrationEstimator::filterMatches(const std::vector<cv::DMatch>& matches, std::vector<cv::DMatch>& goodMatches)
{
	double max_dist = -10000000;
  	double min_dist = 10000000;
	//Find minimum and maximum distances between matches
	for( unsigned int i = 0; i < matches.size(); i++ )
	{
		double dist = matches[i].distance;
		if( dist < min_dist )
		{
			min_dist = dist;
		}
		if( dist > max_dist )
			max_dist = dist;
	}
	ROS_INFO_STREAM("Best/Worst matches = "<<min_dist<<"/"<<max_dist);


	//Filter the matches to keep just the best ones
	for( unsigned int i = 0; i < matches.size(); i++ )
	{
		if( matches[i].distance <= matchingThreshold/*std::min(min_dist+(max_dist-min_dist)*0.2,25.0)*/ )
		{
			goodMatches.push_back( matches[i]);
			//ROS_INFO_STREAM("got a good match, dist="<<matches[i].distance);
		}
	}

/*
	//take best 4
	std::vector<int> goodMatchesIdx;
	for(int i=0;i<4;i++)
	{
		double bestDist = 10000000;
		int bestMatchIdx = 0;
		for(unsigned int j=0;j<matches.size();j++)
		{
			double dist = matches[j].distance;
			if( dist < bestDist && std::find(goodMatchesIdx.begin(), goodMatchesIdx.end(), j) == goodMatchesIdx.end())//if it is better and it is not in goodMatches
			{
				bestDist = dist;
				bestMatchIdx = j;
			}
		}
		goodMatchesIdx.push_back(bestMatchIdx);
		goodMatches.push_back(matches.at(bestMatchIdx));
	}
	*/
	//goodMatches.push_back(*bestMatch);
	return 0;
}

int ARDeviceRegistrationEstimator::readReceivedImageMessages(const opt_msgs::ArcoreCameraImageConstPtr& arcoreInputMsg,
					const sensor_msgs::ImageConstPtr& kinectInputCameraMsg,
					const sensor_msgs::ImageConstPtr& kinectInputDepthMsg,
					const sensor_msgs::CameraInfo& kinectCameraInfo,
					cv::Mat& arcoreCameraMatrix,
					cv::Mat& arcoreImg,
					cv::Mat& kinectCameraMatrix,
					cv::Mat& kinectCameraImg,
					cv::Mat& kinectDepthImg,
					tf::Pose& phonePoseArcoreFrameConverted)
{
	std::chrono::steady_clock::time_point beginning = std::chrono::steady_clock::now();

	//convert the camera matrix in a cv::Mat
	arcoreCameraMatrix = cv::Mat(3, 3, CV_64FC1);
	arcoreCameraMatrix.at<double>(0,0) = arcoreInputMsg->focal_length_x_px;
	arcoreCameraMatrix.at<double>(0,1) = 0;
	arcoreCameraMatrix.at<double>(0,2) = arcoreInputMsg->principal_point_x_px;
	arcoreCameraMatrix.at<double>(1,0) = 0;
	arcoreCameraMatrix.at<double>(1,1) = arcoreInputMsg->focal_length_y_px;
	arcoreCameraMatrix.at<double>(1,2) = arcoreInputMsg->principal_point_y_px;
	arcoreCameraMatrix.at<double>(2,0) = 0;
	arcoreCameraMatrix.at<double>(2,1) = 0;
	arcoreCameraMatrix.at<double>(2,2) = 1;


	kinectCameraMatrix = cv::Mat(3, 3, CV_64FC1);
	kinectCameraMatrix.at<double>(0,0) = kinectCameraInfo.P[4*0+0];
	kinectCameraMatrix.at<double>(0,1) = kinectCameraInfo.P[4*0+1];
	kinectCameraMatrix.at<double>(0,2) = kinectCameraInfo.P[4*0+2];
	kinectCameraMatrix.at<double>(1,0) = kinectCameraInfo.P[4*1+0];
	kinectCameraMatrix.at<double>(1,1) = kinectCameraInfo.P[4*1+1];
	kinectCameraMatrix.at<double>(1,2) = kinectCameraInfo.P[4*1+2];
	kinectCameraMatrix.at<double>(2,0) = kinectCameraInfo.P[4*2+0];
	kinectCameraMatrix.at<double>(2,1) = kinectCameraInfo.P[4*2+1];
	kinectCameraMatrix.at<double>(2,2) = kinectCameraInfo.P[4*2+2];

	//decode arcore image
	cv::Mat rawImageData(cv::Mat(arcoreInputMsg->image.data));
	if(rawImageData.empty())
	{
		ROS_ERROR("Invalid arcore image (it's empty!)");
		return -1;
	}

	arcoreImg = cv::imdecode(rawImageData,1);//convert compressed image data to cv::Mat
	if(!arcoreImg.data)
	{
		ROS_ERROR("couldn't decode arcore image");
		return -2;
	}
	if(arcoreImg.channels()!=3)
	{
		ROS_ERROR("Color image expected from arcore device, received something different");
		return -3;
	}
	//The image sent by the Android app is monochrome, but it is stored in a 3-channel PNG image as the red channel
	//So we extract the red channel and use that.
	//Also the image is flipped on the y axis
	cv::Mat planes[3];
	split(arcoreImg,planes);  // planes[2] is the red channel
	arcoreImg = planes[2];
	cv::Mat flippedArcoreImg;
	cv::flip(arcoreImg,flippedArcoreImg,0);
	arcoreImg=flippedArcoreImg;
	//cv::xphoto::createSimpleWB()->balanceWhite(flippedArcoreImg,arcoreImg);
	//cv::equalizeHist(arcoreImg,arcoreImg);
    ROS_DEBUG("decoded arcore image");
    //cv::imshow("Arcore", arcoreImg);

    //decode kinect rgb image
	kinectCameraImg = cv_bridge::toCvShare(kinectInputCameraMsg)->image;//convert compressed image data to cv::Mat
	if(!kinectCameraImg.data)
	{
		ROS_ERROR("couldn't extract kinect camera opencv image");
		return -4;
	}
    ROS_DEBUG("decoded kinect camera image");
	//cv::equalizeHist(kinectCameraImg,kinectCameraImg);
    //cv::imshow("Kinect", kinectCameraImg);


    //decode kinect depth image
	kinectDepthImg = cv_bridge::toCvShare(kinectInputDepthMsg)->image;//convert compressed image data to cv::Mat
	if(!kinectDepthImg.data)
	{
		ROS_ERROR("couldn't extract kinect depth opencv image");
		return -5;
	}
    ROS_DEBUG("decoded kinect depth image");
  /*  cv::namedWindow("KinectDepth", cv::WINDOW_NORMAL);
	cv::resizeWindow("KinectDepth",1280,720);
    cv::imshow("KinectDepth", kinectDepthImg);
*/






	// Convert phone arcore pose
	// ARCore on Unity uses Unity's coordinate systema, which is left-handed, normally in arcore for Android the arcore
	// camera position is defined with x pointing right, y pointing up and -z pointing where the camera is facing.
	// As provided from all ARCore APIs, Poses always describe the transformation from object's local coordinate space
	// to the world coordinate space. This is the usual pose representation, same as ROS
	tf::Pose phonePoseArcoreFrameUnity;
	tf::poseMsgToTF(arcoreInputMsg->mobileFramePose,phonePoseArcoreFrameUnity);
	tf::Pose phonePoseArcoreFrame = convertPoseUnityToRos(phonePoseArcoreFrameUnity);

	//publishTransformAsTfFrame(phonePoseArcoreFrame,"phone_arcore","/world",arcoreInputMsg->header.stamp);
	//publishTransformAsTfFrame(phonePoseArcoreFrameUnity,"phone_arcore_left","/world",arcoreInputMsg->header.stamp);

	//from x to the right, y up, z back to x to the right, y down, z forward
	tf::Transform cameraConventionTransform = tf::Transform(tf::Quaternion(tf::Vector3(1,0,0), 3.1415926535897));//rotate 180 degrees around x axis
	//assuming z is pointing foward:
	tf::Transform portraitToLandscape = tf::Transform(tf::Quaternion(tf::Vector3(0,0,1), 3.1415926535897/2));//rotate +90 degrees around z axis
	tf::Transform justRotation = tf::Transform(phonePoseArcoreFrame.getRotation()) * portraitToLandscape;
	tf::Transform justTranslation = tf::Transform(tf::Quaternion(1,0,0,0),phonePoseArcoreFrame.getOrigin());

	//tf::Pose phonePoseArcoreInverted = tf::Transform(tf::Quaternion(tf::Vector3(1,0,0),0),phonePoseArcoreFrame.getOrigin()).inverse() * tf::Transform(phonePoseArcoreFrame.getRotation()).inverse();
	phonePoseArcoreFrameConverted =  justTranslation *cameraConventionTransform*justRotation;
	ROS_DEBUG_STREAM("phonePoseArcoreFrame = "<<poseToString(phonePoseArcoreFrame));
	//publishTransformAsTfFrame(phonePoseArcoreFrameConverted,"phone_arcore_converted","/world",arcoreInputMsg->header.stamp);
	//publishTransformAsTfFrame(phonePoseArcoreInverted,"phone_arcore_inv","/world",arcoreInputMsg->header.stamp);


	std::chrono::steady_clock::time_point afterDecoding = std::chrono::steady_clock::now();
	unsigned long decodingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterDecoding - beginning).count();
	ROS_DEBUG("Images decoding and initialization took %lu ms",decodingDuration);

	return 0;
}



int ARDeviceRegistrationEstimator::readReceivedMessages_features(const opt_msgs::ArcoreCameraFeaturesConstPtr& arcoreInputMsg,
					const sensor_msgs::ImageConstPtr& kinectInputCameraMsg,
					const sensor_msgs::ImageConstPtr& kinectInputDepthMsg,
					const sensor_msgs::CameraInfo& kinectCameraInfo,
					cv::Mat& arcoreCameraMatrix,
					cv::Mat& arcoreDescriptors,
					std::vector<cv::KeyPoint>& arcoreKeypoints,
					cv::Size& arcoreImageSize,
					cv::Mat& kinectCameraMatrix,
					cv::Mat& kinectCameraImg,
					cv::Mat& kinectDepthImg,
					tf::Pose& phonePoseArcoreFrameConverted,
					cv::Mat& debugArcoreImage)
{
	std::chrono::steady_clock::time_point beginning = std::chrono::steady_clock::now();

	//convert the camera matrix in a cv::Mat
	arcoreCameraMatrix = cv::Mat(3, 3, CV_64FC1);
	arcoreCameraMatrix.at<double>(0,0) = arcoreInputMsg->focal_length_x_px;
	arcoreCameraMatrix.at<double>(0,1) = 0;
	arcoreCameraMatrix.at<double>(0,2) = arcoreInputMsg->principal_point_x_px;
	arcoreCameraMatrix.at<double>(1,0) = 0;
	arcoreCameraMatrix.at<double>(1,1) = arcoreInputMsg->focal_length_y_px;
	arcoreCameraMatrix.at<double>(1,2) = arcoreInputMsg->principal_point_y_px;
	arcoreCameraMatrix.at<double>(2,0) = 0;
	arcoreCameraMatrix.at<double>(2,1) = 0;
	arcoreCameraMatrix.at<double>(2,2) = 1;


	kinectCameraMatrix = cv::Mat(3, 3, CV_64FC1);
	kinectCameraMatrix.at<double>(0,0) = kinectCameraInfo.P[4*0+0];
	kinectCameraMatrix.at<double>(0,1) = kinectCameraInfo.P[4*0+1];
	kinectCameraMatrix.at<double>(0,2) = kinectCameraInfo.P[4*0+2];
	kinectCameraMatrix.at<double>(1,0) = kinectCameraInfo.P[4*1+0];
	kinectCameraMatrix.at<double>(1,1) = kinectCameraInfo.P[4*1+1];
	kinectCameraMatrix.at<double>(1,2) = kinectCameraInfo.P[4*1+2];
	kinectCameraMatrix.at<double>(2,0) = kinectCameraInfo.P[4*2+0];
	kinectCameraMatrix.at<double>(2,1) = kinectCameraInfo.P[4*2+1];
	kinectCameraMatrix.at<double>(2,2) = kinectCameraInfo.P[4*2+2];

	

	arcoreImageSize.width = arcoreInputMsg->image_width_px;
	arcoreImageSize.height = arcoreInputMsg->image_height_px;

	for(unsigned int i=0;i<arcoreInputMsg->keypoints.size(); i++)
	{
		arcoreKeypoints.push_back(cv::KeyPoint(cv::Point2f(arcoreInputMsg->keypoints[i].x_pos,arcoreInputMsg->keypoints[i].y_pos),
									arcoreInputMsg->keypoints[i].size,
									arcoreInputMsg->keypoints[i].angle,
									arcoreInputMsg->keypoints[i].response,
									arcoreInputMsg->keypoints[i].octave,
									arcoreInputMsg->keypoints[i].class_id));
	}

	cv::Mat receivedDescriptors( arcoreInputMsg->descriptors_mat_rows, arcoreInputMsg->descriptors_mat_cols,  arcoreInputMsg->descriptors_mat_type,  (void*)&(arcoreInputMsg->descriptors_mat_data)[0]);
	arcoreDescriptors = receivedDescriptors;




	
	//decode arcore image if present
	if(arcoreInputMsg->image.data.size()>0)
	{
		debugArcoreImage = cv_bridge::toCvCopy(arcoreInputMsg->image)->image;//convert compressed image data to cv::Mat

		if(!debugArcoreImage.data)
		{
			ROS_ERROR("couldn't decode arcore image");
			return -2;
		}
		if(debugArcoreImage.channels()==3)
		{
			//The image sent by the Android app is monochrome, but it is stored in a 3-channel PNG image as the red channel
			//So we extract the red channel and use that.
			//Also the image is flipped on the y axis
			//take red channel
			cv::Mat planes[3];
			split(debugArcoreImage,planes);  // planes[2] is the red channel
			debugArcoreImage = planes[2];
			cv::Mat flippedArcoreImg;
			cv::flip(debugArcoreImage,flippedArcoreImg,0);
			debugArcoreImage=flippedArcoreImg;
		}
		else if(debugArcoreImage.channels()==1)
		{
			cv::Mat flippedArcoreImg;
			cv::flip(debugArcoreImage,flippedArcoreImg,0);
			debugArcoreImage=flippedArcoreImg;
		}
		else
		{
			ROS_ERROR("received an invalid image, should have either one or three channels");
			return -3;
		}
		//the debug image is not at full resolution, scale it up
		if(debugArcoreImage.cols!=arcoreImageSize.width || debugArcoreImage.rows!=arcoreImageSize.height)
		{
			cv::Mat scaledMat;
			cv::resize(debugArcoreImage,scaledMat,arcoreImageSize,CV_INTER_LINEAR);//resize image
			debugArcoreImage = scaledMat;
		}
	}

    //decode kinect rgb image
	kinectCameraImg = cv_bridge::toCvShare(kinectInputCameraMsg)->image;//convert compressed image data to cv::Mat
	if(!kinectCameraImg.data)
	{
		ROS_ERROR("couldn't extract kinect camera opencv image");
		return -4;
	}
    ROS_DEBUG("decoded kinect camera image");
	//cv::equalizeHist(kinectCameraImg,kinectCameraImg);
    //cv::imshow("Kinect", kinectCameraImg);


    //decode kinect depth image
	kinectDepthImg = cv_bridge::toCvShare(kinectInputDepthMsg)->image;//convert compressed image data to cv::Mat
	if(!kinectDepthImg.data)
	{
		ROS_ERROR("couldn't extract kinect depth opencv image");
		return -5;
	}
    ROS_DEBUG("decoded kinect depth image");
  /*  cv::namedWindow("KinectDepth", cv::WINDOW_NORMAL);
	cv::resizeWindow("KinectDepth",1280,720);
    cv::imshow("KinectDepth", kinectDepthImg);
*/






	// Convert phone arcore pose
	// ARCore on Unity uses Unity's coordinate systema, which is left-handed, normally in arcore for Android the arcore
	// camera position is defined with x pointing right, y pointing up and -z pointing where the camera is facing.
	// As provided from all ARCore APIs, Poses always describe the transformation from object's local coordinate space
	// to the world coordinate space. This is the usual pose representation, same as ROS
	tf::Pose phonePoseArcoreFrameUnity;
	tf::poseMsgToTF(arcoreInputMsg->mobileFramePose,phonePoseArcoreFrameUnity);
	tf::Pose phonePoseArcoreFrame = convertPoseUnityToRos(phonePoseArcoreFrameUnity);

	//publishTransformAsTfFrame(phonePoseArcoreFrame,"phone_arcore","/world",arcoreInputMsg->header.stamp);
	//publishTransformAsTfFrame(phonePoseArcoreFrameUnity,"phone_arcore_left","/world",arcoreInputMsg->header.stamp);

	//from x to the right, y up, z back to x to the right, y down, z forward
	tf::Transform cameraConventionTransform = tf::Transform(tf::Quaternion(tf::Vector3(1,0,0), 3.1415926535897));//rotate 180 degrees around x axis
	//assuming z is pointing foward:
	tf::Transform portraitToLandscape = tf::Transform(tf::Quaternion(tf::Vector3(0,0,1), 3.1415926535897/2));//rotate +90 degrees around z axis
	tf::Transform justRotation = tf::Transform(phonePoseArcoreFrame.getRotation()) * portraitToLandscape;
	tf::Transform justTranslation = tf::Transform(tf::Quaternion(1,0,0,0),phonePoseArcoreFrame.getOrigin());

	//tf::Pose phonePoseArcoreInverted = tf::Transform(tf::Quaternion(tf::Vector3(1,0,0),0),phonePoseArcoreFrame.getOrigin()).inverse() * tf::Transform(phonePoseArcoreFrame.getRotation()).inverse();
	phonePoseArcoreFrameConverted =  justTranslation *cameraConventionTransform*justRotation;
	ROS_DEBUG_STREAM("phonePoseArcoreFrame = "<<poseToString(phonePoseArcoreFrame));
	//publishTransformAsTfFrame(phonePoseArcoreFrameConverted,"phone_arcore_converted","/world",arcoreInputMsg->header.stamp);
	//publishTransformAsTfFrame(phonePoseArcoreInverted,"phone_arcore_inv","/world",arcoreInputMsg->header.stamp);


	std::chrono::steady_clock::time_point afterDecoding = std::chrono::steady_clock::now();
	unsigned long decodingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(afterDecoding - beginning).count();
	ROS_DEBUG("Images decoding and initialization took %lu ms",decodingDuration);

	return 0;
}

/**
 * Checks the provided matches to ensure they have valid depth info. If the depth is not available in the depth image
 * this funciton will try to fix the image by getting the closest depth value. If the closest valid pixel is too far
 * the match will be dropped.
 */
int ARDeviceRegistrationEstimator::fixMatchesDepthOrDrop(const std::vector<cv::DMatch>& inputMatches, const std::vector<cv::KeyPoint>& kinectKeypoints, cv::Mat& kinectDepthImg,std::vector<cv::DMatch>& outputMatches)
{
	outputMatches.clear();
	for( unsigned int i = 0; i < inputMatches.size(); i++ )
	{
		//QueryIdx is for arcore descriptors, TrainIdx is for kinect. This is because of how we passed the arguments to BFMatcher::match
		cv::Point2f imgPos = kinectKeypoints.at(inputMatches.at(i).trainIdx).pt;
		//try to find the depth using the closest pixel
		if(kinectDepthImg.at<uint16_t>(imgPos)==0)
		{
			Point2i nnz = findNearestNonZeroPixel(kinectDepthImg,imgPos.x,imgPos.y,100);
			double nnzDist = hypot(nnz.x-imgPos.x,nnz.y-imgPos.y);
			nnz = findLowestNonZeroInRing(kinectDepthImg,imgPos.x,imgPos.y, nnzDist+10, nnzDist);

			//ROS_INFO("Got closest non-zero pixel, %d;%d",nnz.x,nnz.y);
			kinectDepthImg.at<uint16_t>(imgPos)=kinectDepthImg.at<uint16_t>(nnz);
		}

		if(kinectDepthImg.at<uint16_t>(imgPos)==0)
		{
			//ROS_INFO("Dropped match as it had zero depth");
		}
		else
		{
			outputMatches.push_back(inputMatches.at(i));
		}
	}
	return 0;
}


int ARDeviceRegistrationEstimator::get3dPositionsAndImagePositions(const std::vector<cv::DMatch>& inputMatches,
	const std::vector<cv::KeyPoint>& kinectKeypoints,
	const std::vector<cv::KeyPoint>& arcoreKeypoints,
	const cv::Mat& kinectDepthImg,
	const cv::Mat& kinectCameraMatrix,
    std::vector<cv::Point3f>& matches3dPos,
    std::vector<cv::Point2f>& matchesImgPos)
{
	matches3dPos.clear();
	matchesImgPos.clear();
	for( unsigned int i = 0; i < inputMatches.size(); i++ )
	{
		//QueryIdx is for arcore descriptors, TrainIdx is for kinect. This is because of how we passed the arguments to BFMatcher::match
		cv::Point2f kinectPixelPos	= kinectKeypoints.at(inputMatches.at(i).trainIdx).pt;
		cv::Point2f arcorePixelPos	= arcoreKeypoints.at(inputMatches.at(i).queryIdx).pt;
		cv::Point3f pos3d 			= get3dPoint(	kinectPixelPos.x,kinectPixelPos.y,
													kinectDepthImg.at<uint16_t>(kinectPixelPos),
													kinectCameraMatrix.at<double>(0,0),kinectCameraMatrix.at<double>(1,1),kinectCameraMatrix.at<double>(0,2),kinectCameraMatrix.at<double>(1,2));
		matches3dPos.push_back(pos3d);
		matchesImgPos.push_back(arcorePixelPos);


		ROS_DEBUG_STREAM("good match between "<<kinectPixelPos.x<<";"<<kinectPixelPos.y<<" \tand \t"<<arcorePixelPos.x<<";"<<arcorePixelPos.y<<" \tdistance = "<<inputMatches.at(i).distance);
		//ROS_INFO_STREAM("depth = "<<kinectDepthImg.at<uint16_t>(kinectPixelPos));
	}
	return 0;
}


int ARDeviceRegistrationEstimator::getLastEstimateMatchesNumber()
{
	return lastEstimateMatchesNumber;
}

double ARDeviceRegistrationEstimator::getLastEstimateReprojectionError()
{
	return lastEstimateReprojectionError;
}

geometry_msgs::TransformStamped ARDeviceRegistrationEstimator::getLastEstimate()
{
	return lastEstimate;
}

string ARDeviceRegistrationEstimator::getARDeviceId()
{
	return ARDeviceId;
}

bool ARDeviceRegistrationEstimator::hasEstimate()
{
	return didComputeEstimation;
}