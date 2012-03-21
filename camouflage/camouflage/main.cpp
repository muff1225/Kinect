#include <iostream>
#include <stdexcept>

#include <opencv/cv.h>
#include <opencv/highgui.h>

#include <XnCppWrapper.h>
#include "SkeltonDrawer.h"

// �ݒ�t�@�C���̃p�X
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
		std::cout << "���[�U�[���o�F" << nId << " " << generator.GetNumberOfUsers() << "�l��" << std::endl;
	
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
		std::cout << "���[�U�[�����F" << nId << " " << generator.GetNumberOfUsers() << "�l��" << std::endl;
}

void XN_CALLBACK_TYPE PoseDetected(xn::PoseDetectionCapability& capabillity,
	const XnChar* strPose, XnUserID nId, void* pCookie)
{
	std::cout << "�|�[�Y���o:" << strPose << "���[�U:" << nId << std::endl;

	xn::UserGenerator* user = (xn::UserGenerator*)pCookie;
	user->GetPoseDetectionCap().StopPoseDetection(nId);
	user->GetSkeletonCap().RequestCalibration(nId, TRUE);
}

void XN_CALLBACK_TYPE PoseLost(xn::PoseDetectionCapability& capabillity,
	const XnChar* strPose, XnUserID nId, void* pCookie)
{
	std::cout << "�|�[�Y����:" << strPose << "���[�U:" << nId << std::endl;
}

void XN_CALLBACK_TYPE CalibrationStart(xn::SkeletonCapability& capability,
	XnUserID nId, void* pCookie)
{
	std::cout << "�L�����u���[�V�����J�n�B���[�U:" << nId << std::endl;
}

void XN_CALLBACK_TYPE CalibrationEnd(xn::SkeletonCapability& capability,
	XnUserID nId, XnBool bSuccess, void* pCookie)
{
	xn::UserGenerator* user = (xn::UserGenerator*)pCookie;

	if(bSuccess) {
		std::cout << "�L�����u���[�V���������B���[�U:" << nId << std::endl;
		user->GetSkeletonCap().StartTracking(nId);
	}
	else {
		std::cout << "�L�����u���[�V�������s�B���[�U:" << nId << std::endl;
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
		// �R���e�L�X�g�̏�����
		xn::Context context;
		XnStatus rc = context.InitFromXmlFile(CONFIG_XML_PATH);
		if(rc != XN_STATUS_OK) {
			throw std::runtime_error(::xnGetStatusString(rc));
		}
		std::cout << "Success" << std::endl;

		//�@�C���[�W�W�F�l���[�^�̍쐬
		xn::ImageGenerator image;
		rc = context.FindExistingNode(XN_NODE_TYPE_IMAGE, image);
		if(rc != XN_STATUS_OK) {
			throw std::runtime_error(xnGetStatusString(rc));
		}

		// �f�v�X�W�F�l���[�^�̍쐬
		xn::DepthGenerator depth;
		rc = context.FindExistingNode(XN_NODE_TYPE_DEPTH, depth);
		if(rc != XN_STATUS_OK) {
			throw std::runtime_error(xnGetStatusString(rc));
		}

		depth.GetAlternativeViewPointCap().SetViewPoint(image);

		// ���[�U�[�̍쐬
		xn::UserGenerator user;
		rc = context.FindExistingNode(XN_NODE_TYPE_USER,user);
		if(rc != XN_STATUS_OK) {
			rc = user.Create(context);
			if(rc != XN_STATUS_OK) {
				throw std::runtime_error(xnGetStatusString(rc));
			}
		}

		// ���[�U�[���o�@�\���T�|�[�g���Ă��邩�m�F
		if(!user.IsCapabilitySupported(XN_CAPABILITY_SKELETON)) {
			throw std::runtime_error("���[�U�[���o���T�|�[�g���Ă��܂���");
		}

		XnCallbackHandle userCallbacks, calibrationCallbacks, poseCallbacks;
		XnChar pose[20] = "";

		xn::SkeletonCapability skelton = user.GetSkeletonCap();
		if(skelton.NeedPoseForCalibration()) {
			if(!user.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION)) {
				throw std::runtime_error("�|�[�Y���o���T�|�[�g���Ă��܂���");
			}

			skelton.GetCalibrationPose(pose);

			xn::PoseDetectionCapability pose = user.GetPoseDetectionCap();
			pose.RegisterToPoseCallbacks(&::PoseDetected, &::PoseLost, &user, poseCallbacks);
		}

		//���[�U�[�F���̃R�[���o�b�N�o�^
		user.RegisterUserCallbacks(&::UserDetected, &::UserLost, pose, userCallbacks);
		//�X�P���g���̃R�[���o�b�N�o�^
		skelton.RegisterCalibrationCallbacks(&::CalibrationStart, &::CalibrationEnd,
			&user, calibrationCallbacks);

		skelton.SetSkeletonProfile(XN_SKEL_PROFILE_ALL);

		context.StartGeneratingAll();

				// �J�����T�C�Y�̃C���[�W�̍쐬
		XnMapOutputMode outputMode;
		image.GetMapOutputMode(outputMode);
		camera = ::cvCreateImage(cvSize(outputMode.nXRes, outputMode.nYRes), IPL_DEPTH_8U, 3);
		if(!camera) {
			throw std::runtime_error("error:cvCreateImage");
		}

		bool isBackgroundRefresh = true;
		bool isCamouflage = true;
		bool isShowSkelton = false;
		
		// �w�i�摜�̎擾
		background = ::cvCreateImage(cvSize(outputMode.nXRes, outputMode.nYRes), IPL_DEPTH_8U, 3);
		if(!background) {
			throw std::runtime_error("error : cvCreateImage");
		}

		// ���C�����[�v
		while(1) {
			//���ׂẴm�[�h�̍X�V��҂�
			context.WaitAndUpdateAll();

			//�摜�f�[�^�̎擾
			xn::ImageMetaData imageMD;
			image.GetMetaData(imageMD);

			// ���[�U�[�f�[�^�̎擾
			xn::SceneMetaData sceneMD;
			user.GetUserPixels(0, sceneMD);

			//�w�i�摜�̍X�V
			if(isBackgroundRefresh) {
				isBackgroundRefresh = false;
				memcpy(background->imageData, imageMD.RGB24Data(), background->imageSize);
				std::cout << "�J���t���[�W���f�[�^���X�V���܂����B" << std::endl;
			}

			// �J�����摜�̕\��
			char* dest = camera->imageData;
			char* back = background->imageData;
			for(int y = 0; y < imageMD.YRes(); ++y) {
				for (int x = 0; x < imageMD.XRes(); ++x) {
					// ���w���ʂ��L���ŁA���[�U�[������ꍇ�A�w�i��`�悷��
					if (isCamouflage && (sceneMD(x, y) != 0)) {
						dest[0] = back[0];
						dest[1] = back[1];
						dest[2] = back[2];
					}
					// ���[�U�[�ł͂Ȃ��̂ŁA�J�����摜��`�悷��
					else {
						//�J�����摜�̕\��
						XnRGB24Pixel rgb = imageMD.RGB24Map()(x, y);
						dest[0] = rgb.nRed;
						dest[1] = rgb.nGreen;
						dest[2] = rgb.nBlue;
					}

					dest += 3;
					back += 3;
				}
			}

			// �X�P���g���̕\��
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

			//�L�[���擾
			char key = cvWaitKey(10);
			//�I������
			if(key == 'q') {
				break;
			}
			//�w�i�̍X�V
			else if (key == 'r') {
				isBackgroundRefresh = true;
			}
			//�J���t���[�W���I���I�t
			else if(key == 'c') {
				isCamouflage = !isCamouflage;
				std::cout << "�J���t���[�W���F" << isCamouflage << std::endl;
			} else if(key == 's') {
				isShowSkelton = !isShowSkelton;
				std::cout << "�X�P���g���F" << isShowSkelton << std::endl;
			}
		}
	} catch (std::exception& ex) {
		std::cout << ex.what() <<std::endl;
	}

	::cvReleaseImage(&background);
	::cvReleaseImage(&camera);

	return 0;
}