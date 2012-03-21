#include <iostream>
#include <stdexcept>

#include <opencv/cv.h>
#include <opencv/highgui.h>

#include <XnCppWrapper.h>
#include "SkeltonDrawer.h"

// 設定ファイルのパス
const char* CONFIG_XML_PATH = "SamplesConfig.xml";

const XnFloat Colors[][3] = {
	{1,1,1},
	{0,1,1}, {0,0,1}, {0,1,0},
	{1,1,0}, {1,0,0}, {1,.5,0},
	{.5,1,0}, {0,.5,1}, {.5,0,1},
	{1,1,.5},
};

void XN_CALLBACK_TYPE UserDetected(xn::UserGenerator& generator,
	XnUserID nId, void* pCookie) {
		std::cout << "ユーザー検出：" << nId << " " << generator.GetNumberOfUsers() << "人目" << std::endl;
	
	XnChar* pose = (XnChar*)pCookie;
	if(pose[0] != '0') {
		generator.GetPoseDetectionCap().StartPoseDetection(pose, nId);
	}
	else {
		generator.GetSkeletonCap().RequestCalibration(nId, TRUE);
	}
}

void XN_CALLBACK_TYPE UserLost(xn::UserGenerator& generator,
	XnUserID nId, void* pCookie) {
		std::cout << "ユーザー消失：" << nId << " " << generator.GetNumberOfUsers() << "人目" << std::endl;
}

void XN_CALLBACK_TYPE PoseDetected(xn::PoseDetectionCapability& capabillity,
	const XnChar* strPose, XnUserID nId, void* pCookie)
{
	std::cout << "ポーズ検出:" << strPose << "ユーザ:" << nId << std::endl;

	xn::UserGenerator* user = (xn::UserGenerator*)pCookie;
	user->GetPoseDetectionCap().StopPoseDetection(nId);
	user->GetSkeletonCap().RequestCalibration(nId, TRUE);
}

void XN_CALLBACK_TYPE PoseLost(xn::PoseDetectionCapability& capabillity,
	const XnChar* strPose, XnUserID nId, void* pCookie)
{
	std::cout << "ポーズ消失:" << strPose << "ユーザ:" << nId << std::endl;
}

void XN_CALLBACK_TYPE CalibrationStart(xn::SkeletonCapability& capability,
	XnUserID nId, void* pCookie)
{
	std::cout << "キャリブレーション開始。ユーザ:" << nId << std::endl;
}

void XN_CALLBACK_TYPE CalibrationEnd(xn::SkeletonCapability& capability,
	XnUserID nId, XnBool bSuccess, void* pCookie)
{
	xn::UserGenerator* user = (xn::UserGenerator*)pCookie;

	if(bSuccess) {
		std::cout << "キャリブレーション成功。ユーザ:" << nId << std::endl;
		user->GetSkeletonCap().StartTracking(nId);
	}
	else {
		std::cout << "キャリブレーション失敗。ユーザ:" << nId << std::endl;
	}
}

inline XnRGB24Pixel xnRGB24Pixel(int r, int g, int b) {
	XnRGB24Pixel pixel = {r, g, b};
	return pixel;
}

void XN_CALLBACK_TYPE ViewPointChange(xn::ProductionNode &node,void *pCookie) {
	std::cout << node.GetName() << "ViewPointChanged" << std::endl;
}

int main(int argc, char * argv[])
{
	IplImage* camera = 0; 
	IplImage* background = 0;

	try {
		// コンテキストの初期化
		xn::Context context;
		XnStatus rc = context.InitFromXmlFile(CONFIG_XML_PATH);
		if(rc != XN_STATUS_OK) {
			throw std::runtime_error(::xnGetStatusString(rc));
		}
		std::cout << "Success" << std::endl;

		//　イメージジェネレータの作成
		xn::ImageGenerator image;
		rc = context.FindExistingNode(XN_NODE_TYPE_IMAGE, image);
		if(rc != XN_STATUS_OK) {
			throw std::runtime_error(xnGetStatusString(rc));
		}

		// デプスジェネレータの作成
		xn::DepthGenerator depth;
		rc = context.FindExistingNode(XN_NODE_TYPE_DEPTH, depth);
		if(rc != XN_STATUS_OK) {
			throw std::runtime_error(xnGetStatusString(rc));
		}

		depth.GetAlternativeViewPointCap().SetViewPoint(image);

		// ユーザーの作成
		xn::UserGenerator user;
		rc = context.FindExistingNode(XN_NODE_TYPE_USER,user);
		if(rc != XN_STATUS_OK) {
			rc = user.Create(context);
			if(rc != XN_STATUS_OK) {
				throw std::runtime_error(xnGetStatusString(rc));
			}
		}

		// ユーザー検出機能をサポートしているか確認
		if(!user.IsCapabilitySupported(XN_CAPABILITY_SKELETON)) {
			throw std::runtime_error("ユーザー検出をサポートしていません");
		}

		XnCallbackHandle userCallbacks, calibrationCallbacks, poseCallbacks;
		XnChar pose[20] = "";

		xn::SkeletonCapability skelton = user.GetSkeletonCap();
		if(skelton.NeedPoseForCalibration()) {
			if(!user.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION)) {
				throw std::runtime_error("ポーズ検出をサポートしていません");
			}

			skelton.GetCalibrationPose(pose);

			xn::PoseDetectionCapability pose = user.GetPoseDetectionCap();
			pose.RegisterToPoseCallbacks(&::PoseDetected, &::PoseLost, &user, poseCallbacks);
		}

		//ユーザー認識のコールバック登録
		user.RegisterUserCallbacks(&::UserDetected, &::UserLost, pose, userCallbacks);
		//スケルトンのコールバック登録
		skelton.RegisterCalibrationCallbacks(&::CalibrationStart, &::CalibrationEnd,
			&user, calibrationCallbacks);

		skelton.SetSkeletonProfile(XN_SKEL_PROFILE_ALL);

		context.StartGeneratingAll();

				// カメラサイズのイメージの作成
		XnMapOutputMode outputMode;
		image.GetMapOutputMode(outputMode);
		camera = ::cvCreateImage(cvSize(outputMode.nXRes, outputMode.nYRes), IPL_DEPTH_8U, 3);
		if(!camera) {
			throw std::runtime_error("error:cvCreateImage");
		}

		bool isBackgroundRefresh = true;
		bool isCamouflage = true;
		bool isShowSkelton = false;
		
		// 背景画像の取得
		background = ::cvCreateImage(cvSize(outputMode.nXRes, outputMode.nYRes), IPL_DEPTH_8U, 3);
		if(!background) {
			throw std::runtime_error("error : cvCreateImage");
		}

		// メインループ
		while(1) {
			//すべてのノードの更新を待つ
			context.WaitAndUpdateAll();

			//画像データの取得
			xn::ImageMetaData imageMD;
			image.GetMetaData(imageMD);

			// ユーザーデータの取得
			xn::SceneMetaData sceneMD;
			user.GetUserPixels(0, sceneMD);

			//背景画像の更新
			if(isBackgroundRefresh) {
				isBackgroundRefresh = false;
				memcpy(background->imageData, imageMD.RGB24Data(), background->imageSize);
				std::cout << "カモフラージュデータを更新しました。" << std::endl;
			}

			// カメラ画像の表示
			char* dest = camera->imageData;
			char* back = background->imageData;
			for(int y = 0; y < imageMD.YRes(); ++y) {
				for (int x = 0; x < imageMD.XRes(); ++x) {
					// 光学迷彩が有効で、ユーザーがいる場合、背景を描画する
					if (isCamouflage && (sceneMD(x, y) != 0)) {
						dest[0] = back[0];
						dest[1] = back[1];
						dest[2] = back[2];
					}
					// ユーザーではないので、カメラ画像を描画する
					else {
						//カメラ画像の表示
						XnRGB24Pixel rgb = imageMD.RGB24Map()(x, y);
						dest[0] = rgb.nRed;
						dest[1] = rgb.nGreen;
						dest[2] = rgb.nBlue;
					}

					dest += 3;
					back += 3;
				}
			}

			// スケルトンの表示
			if (isShowSkelton) {
				XnUserID aUsers[15];
				XnUInt16 nUsers = 15;
				user.GetUsers(aUsers, nUsers);
				for(int i = 0; i < nUsers; ++i) {
					if(skelton.IsTracking(aUsers[i])) {
						SkeltonDrawer skeltonDrawer(camera, skelton,
							depth, aUsers[i]);
						skeltonDrawer.draw();
					}
				}
			}

			::cvCvtColor(camera, camera, CV_BGR2RGB);
			::cvShowImage("KinectImage", camera);

			//キーを取得
			char key = cvWaitKey(10);
			//終了する
			if(key == 'q') {
				break;
			}
			//背景の更新
			else if (key == 'r') {
				isBackgroundRefresh = true;
			}
			//カモフラージュオンオフ
			else if(key == 'c') {
				isCamouflage = !isCamouflage;
				std::cout << "カモフラージュ：" << isCamouflage << std::endl;
			} else if(key == 's') {
				isShowSkelton = !isShowSkelton;
				std::cout << "スケルトン：" << isShowSkelton << std::endl;
			}
		}
	} catch (std::exception& ex) {
		std::cout << ex.what() <<std::endl;
	}

	::cvReleaseImage(&background);
	::cvReleaseImage(&camera);

	return 0;
}