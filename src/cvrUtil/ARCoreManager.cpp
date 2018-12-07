#include <cvrUtil/ARCoreManager.h>
#include <cvrUtil/AndroidStdio.h>
#include <cvrUtil/OsgGlesMath.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkImage.h>
#include <cvrUtil/LightingEstimator.h>
#include <osg/Image>

using namespace cvr;
using namespace osg;
ARCoreManager::ARCoreManager(){
    view_mat = new Matrixf;
    proj_mat = new Matrixf;

//    _stitcher = cv::Stitcher::create(cv::Stitcher::PANORAMA);
}

ARCoreManager* ARCoreManager::_myPtr = nullptr;
ARCoreManager *ARCoreManager::instance() {
    if(!_myPtr) _myPtr = new ARCoreManager;
    return _myPtr;
}

ARCoreManager::~ARCoreManager() {
    if(_ar_session){
        ArSession_destroy(_ar_session);
        ArFrame_destroy(_ar_frame);
    }
}

bool GetNdkImageProperties(const AImage* ndk_image, int32_t* out_format,
                           int32_t* out_width, int32_t* out_height,
                           int32_t* out_plane_num, int32_t* out_stride,int32_t* out_strideuv) {
    if (ndk_image == nullptr) {
        return false;
    }
    media_status_t status = AImage_getFormat(ndk_image, out_format);
    if (status != AMEDIA_OK) {
        return false;
    }

    status = AImage_getWidth(ndk_image, out_width);
    if (status != AMEDIA_OK) {
        return false;
    }

    status = AImage_getHeight(ndk_image, out_height);
    if (status != AMEDIA_OK) {
        return false;
    }

    status = AImage_getNumberOfPlanes(ndk_image, out_plane_num);
    if (status != AMEDIA_OK) {
        return false;
    }

    status = AImage_getPlaneRowStride(ndk_image, 0, out_stride);
    if (status != AMEDIA_OK) {
        return false;
    }
    status = AImage_getPlaneRowStride(ndk_image, 1, out_strideuv);
    if (status != AMEDIA_OK) {
        return false;
    }
    return true;
}

void ARCoreManager::onViewChanged(int rot, int width, int height){
    _displayRotation = rot;
    _width = width;
    _height = height;
    if (_ar_session != nullptr)
        ArSession_setDisplayGeometry(_ar_session, rot, width, height);
}

void ARCoreManager::onPause(){
    if(_ar_session) ArSession_pause(_ar_session);
}

void ARCoreManager::onResume(void *env, void *context, void *activity){
    if(nullptr == _ar_session){
        ArInstallStatus install_status;
        bool user_requested_install = !_install_requested;

//            CHECK(ArCoreApk_requestInstall(env, activity, user_requested_install,&install_status) == AR_SUCCESS);

        switch (install_status) {
            case AR_INSTALL_STATUS_INSTALLED:
                break;
            case AR_INSTALL_STATUS_INSTALL_REQUESTED:
                _install_requested = true;
                return;
        }
        CHECK(ArSession_create(env, context, &_ar_session)==AR_SUCCESS);
        CHECK(_ar_session);
        ArFrame_create(_ar_session, &_ar_frame);
        CHECK(_ar_frame);
        ArSession_setDisplayGeometry(_ar_session, _displayRotation, _width, _height);


        ArCloudAnchorMode out_cloud_anchor_mode;
        ArFocusMode focus_mode;
        ArPlaneFindingMode plane_finding_mode;
        ArUpdateMode update_mode;

        ArConfig_create(_ar_session, &_config);
        CHECK(_config);
        ArConfig_getCloudAnchorMode(_ar_session, _config, &out_cloud_anchor_mode);
        ArConfig_getFocusMode(_ar_session, _config, &focus_mode);
        ArConfig_getPlaneFindingMode(_ar_session, _config, &plane_finding_mode);
        ArConfig_getUpdateMode(_ar_session, _config, &update_mode);

        ArConfig_setFocusMode(_ar_session, _config, AR_FOCUS_MODE_AUTO);
        ArConfig_setUpdateMode(_ar_session, _config, AR_UPDATE_MODE_LATEST_CAMERA_IMAGE);
        CHECK(ArSession_configure(_ar_session, _config) == AR_SUCCESS);
    }

    const ArStatus status = ArSession_resume(_ar_session);
    CHECK(status == AR_SUCCESS);
}

static inline uint32_t YUV2RGB(int nY, int nU, int nV, uint8_t&r, uint8_t&g, uint8_t&b) {
    nY -= 16;
    nU -= 128;
    nV -= 128;
    if (nY < 0) nY = 0;

    // This is the floating point equivalent. We do the conversion in integer
    // because some Android devices do not have floating point in hardware.
    // nR = (int)(1.164 * nY + 1.596 * nV);
    // nG = (int)(1.164 * nY - 0.813 * nV - 0.391 * nU);
    // nB = (int)(1.164 * nY + 2.018 * nU);

    int nR = (int)(1192 * nY + 1634 * nV);
    int nG = (int)(1192 * nY - 833 * nV - 400 * nU);
    int nB = (int)(1192 * nY + 2066 * nU);

    nR = std::min(262143, std::max(0, nR));
    nG = std::min(262143, std::max(0, nG));
    nB = std::min(262143, std::max(0, nB));

    nR = (nR >> 10) & 0xff;
    nG = (nG >> 10) & 0xff;
    nB = (nB >> 10) & 0xff;

    b = (uint8_t)nB;
    g = (uint8_t)nG;
    r = (uint8_t)nR;

    return 0xff000000 | (nR << 16) | (nG << 8) | nB;
}

void convert_xyz_to_cube_uv(float x, float y, float z, float *u, float *v, int *index) {
    float absX = fabs(x);
    float absY = fabs(y);
    float absZ = fabs(z);

    int isXPositive = x > 0 ? 1 : 0;
    int isYPositive = y > 0 ? 1 : 0;
    int isZPositive = z > 0 ? 1 : 0;

    float maxAxis, uc = 0, vc = 0;

    // POSITIVE X
    if (isXPositive && absX >= absY && absX >= absZ) {
        // u (0 to 1) goes from +z to -z
        // v (0 to 1) goes from -y to +y
        maxAxis = absX;
        uc = -z;
        vc = y;
        *index = 0;
    }
    // NEGATIVE X
    if (!isXPositive && absX >= absY && absX >= absZ) {
        // u (0 to 1) goes from -z to +z
        // v (0 to 1) goes from -y to +y
        maxAxis = absX;
        uc = z;
        vc = y;
        *index = 1;
    }
    // POSITIVE Y
    if (isYPositive && absY >= absX && absY >= absZ) {
        // u (0 to 1) goes from -x to +x
        // v (0 to 1) goes from +z to -z
        maxAxis = absY;
        uc = x;
        vc = -z;
        *index = 2;
    }
    // NEGATIVE Y
    if (!isYPositive && absY >= absX && absY >= absZ) {
        // u (0 to 1) goes from -x to +x
        // v (0 to 1) goes from -z to +z
        maxAxis = absY;
        uc = x;
        vc = z;
        *index = 3;
    }
    // POSITIVE Z
    if (isZPositive && absZ >= absX && absZ >= absY) {
        // u (0 to 1) goes from -x to +x
        // v (0 to 1) goes from -y to +y
        maxAxis = absZ;
        uc = x;
        vc = y;
        *index = 4;
    }
    // NEGATIVE Z
    if (!isZPositive && absZ >= absX && absZ >= absY) {
        // u (0 to 1) goes from +x to -x
        // v (0 to 1) goes from -y to +y
        maxAxis = absZ;
        uc = -x;
        vc = y;
        *index = 5;
    }

    // Convert range from -1 to 1 to 0 to 1
    *u = 0.5f * (uc / maxAxis + 1.0f);
    *v = 0.5f * (vc / maxAxis + 1.0f);
}

void ARCoreManager::update_ndk_image(){
    int32_t format = 0, width, height, num_plane = 0, stride = 0, strideuv=0;
    if (bg_image != nullptr) {
        if (GetNdkImageProperties(bg_image, &format, &width, &height, &num_plane,
                                  &stride, &strideuv)) {
            if(!_ndk_image_width){
                _ndk_image_width = height; _ndk_image_height = width;
                _current_img = cv::Mat(_ndk_image_height, _ndk_image_width, CV_8UC3);
            }
            if (format == AIMAGE_FORMAT_YUV_420_888) {
                if (_ndk_image_width > 0 || _ndk_image_height > 0 || num_plane > 0 || stride > 0) {
//                    memset(_warp_img,(uint8_t)0,3*_ndk_image_width*_ndk_image_height* sizeof(uint8_t));
                    uint8_t *yPixel, *uPixel, *vPixel;
                    int32_t yLen, uLen, vLen;

                    int32_t uvPixelStride;
                    AImage_getPlanePixelStride(bg_image, 1, &uvPixelStride);
                    AImageCropRect srcRect;
                    AImage_getCropRect(bg_image, &srcRect);

                    AImage_getPlaneData(bg_image, 0, &yPixel, &yLen);
                    AImage_getPlaneData(bg_image, 1, &vPixel, &vLen);
                    AImage_getPlaneData(bg_image, 2, &uPixel, &uLen);

                    Matrixf invMat = Matrixf::inverse(getMVPMatrix());


                    for (int32_t y = 0; y < _ndk_image_width; y++) {
                        const uint8_t *pY = yPixel + stride * (y + srcRect.top) + srcRect.left;

                        int32_t uv_row_start = strideuv * ((y + srcRect.top) >> 1);
                        const uint8_t *pU = uPixel + uv_row_start + (srcRect.left >> 1);
                        const uint8_t *pV = vPixel + uv_row_start + (srcRect.left >> 1);
                        for (int32_t x = 0; x < _ndk_image_height; x++) {
                            const int32_t uv_offset = (x >> 1) * uvPixelStride;

                            YUV2RGB(pY[x], pU[uv_offset], pV[uv_offset],
                                    _current_img.at<cv::Vec3b>(x,_ndk_image_width-y)[2],
                                    _current_img.at<cv::Vec3b>(x,_ndk_image_width-y)[1],
                                    _current_img.at<cv::Vec3b>(x,_ndk_image_width-y)[0]);

//                            float imgx = (x-halfWidth) / halfWidth;
//                            float imgx = (float)(x + halfWidth) / halfWidth;
//                            float imgy = (float) (-y + halfHeight) / halfHeight;
//
//                            Vec4f farPlanePos = Vec4f(imgx,imgy, 1.0, 1.0f) * invMat;
//                            float inv_w = 1.0f / farPlanePos.w();
//                            Vec3f pxDir =  Vec3f(farPlanePos.x() * inv_w, farPlanePos.y()*inv_w, farPlanePos.z()*inv_w);
//                            pxDir-= Vec3f(camera_pose_raw[4], camera_pose_raw[5], camera_pose_raw[6]);
//                            pxDir.normalize();
//                            float fu, fv;
//                            int faceIdx;
//                            convert_xyz_to_cube_uv(pxDir.x(), pxDir.y(), pxDir.z(), &fu, &fv, &faceIdx);
//                            int realX = (int)(fu * TEX_SIZE);
//                            int realY = (int)(fv * TEX_SIZE);
//                            int realIdx = realY * TEX_SIZE + realX;
//                            if(_envImgs[faceIdx][3*realIdx] == 0 &&
//                                    _envImgs[faceIdx][3*realIdx+1] ==0 &&
//                                    _envImgs[faceIdx][3*realIdx+2] ==0){
//                                _envImgs[faceIdx][3*realIdx] = _rgb_image[3*idx];
//                                _envImgs[faceIdx][3*realIdx+1] = _rgb_image[3*idx+1];
//                                _envImgs[faceIdx][3*realIdx+2] = _rgb_image[3*idx+2];
//                            }

                        }
                    }

                }
            } else {
                LOGE("Expected image in YUV_420_888 format.");
            }
        }
    }

}

void ARCoreManager::onDrawFrame() {
    if(_ar_session == nullptr)
        return;
    _frame++;
    if(!_setTexture){
        ArSession_setCameraTextureName(_ar_session, bgTextureId);
        _setTexture = true;
    }
    //ArSession_setCameraTextureName(_ar_session, bgTextureId);
    // Update session to get current frame and render camera background.
    if (ArSession_update(_ar_session, _ar_frame) != AR_SUCCESS) {
        LOGE("OnDrawFrame ArSession_update error");
    }

    ArCamera* camera;
    ArFrame_acquireCamera(_ar_session, _ar_frame, &camera);
    ArCamera_getViewMatrix(_ar_session, camera, (*view_mat).ptr());
    ArCamera_getProjectionMatrix(_ar_session,camera, 0.1f, 100.0f, (*proj_mat).ptr());

    //update camera pose
    ArPose* camera_pose = nullptr;
    ArPose_create(_ar_session, nullptr, &camera_pose);
    ArCamera_getPose(_ar_session, camera, camera_pose);

    ArPose_getPoseRaw(_ar_session, camera_pose, camera_pose_raw);
    ArPose_getMatrix(_ar_session, camera_pose, _camera_pose_col_major);

    quat2Euler(camera_pose_raw, camera_rot_euler[0], camera_rot_euler[1], camera_rot_euler[2]);
    camera_rot_Mat_osg = cvr::rawRotation2OsgMatrix(camera_pose_raw);
    camera_trans_Mat_osg = rawTrans2OsgMatrix(camera_pose_raw+4);
    cameraMatrix_osg = camera_rot_Mat_osg * camera_trans_Mat_osg;
//    TrackingManager::instance()->setTouchEventMatrix(cameraMatrix_osg);
    ArCamera_getTrackingState(_ar_session, camera, &cam_track_state);

    ArFrame_getDisplayGeometryChanged(_ar_session, _ar_frame, &geometry_changed);
    if (geometry_changed != 0)
        ArFrame_transformDisplayUvCoords(_ar_session, _ar_frame, 8, kUVs,
                                         transformed_camera_uvs);
    ArImage * ar_image;
    ArStatus status = ArFrame_acquireCameraImage(_ar_session, _ar_frame, &ar_image);
    if(status == AR_SUCCESS){
        ArImage_getNdkImage(ar_image, &bg_image);
        if(_frame % 50 == 0)
            update_ndk_image();
        ArImage_release(ar_image);
    }
    ArCamera_release(camera);
}
void ARCoreManager::setPixelSize(float sc_x, float sc_y) {
    ArCamera* camera;
    ArFrame_acquireCamera(_ar_session, _ar_frame, &camera);

    ArCameraIntrinsics * camera_intrinsics;
    ArCameraIntrinsics_create(_ar_session, &camera_intrinsics);
    ArCamera_getTextureIntrinsics(_ar_session, camera,
                                  camera_intrinsics);
    float fx, fy, x0, y0;
    //get focal length in pixels
    ArCameraIntrinsics_getFocalLength(_ar_session, camera_intrinsics, &fx, &fy);
    ArCameraIntrinsics_getPrincipalPoint(_ar_session, camera_intrinsics, &x0, &y0);
    cameraMatrix_K= osg::Matrixf(fx,0,x0,0,
                               0,fy,y0,0,
                               0,0,1,0,
                               0,0,0,0);
    ArCameraIntrinsics_destroy(camera_intrinsics);
    ArCamera_release(camera);
}
void ARCoreManager::postFrame(){
    if(_consumeEvent){
        _consumeEvent = false;
        _event_queue.pop();
    }
}

bool ARCoreManager::getPointCouldData(float*& pointCloudData, int32_t & point_num){
    if (cam_track_state != AR_TRACKING_STATE_TRACKING)
        return false;
    ArPointCloud * pointCloud;
    ArStatus  pointcloud_Status = ArFrame_acquirePointCloud(_ar_session, _ar_frame, &pointCloud);
    if(pointcloud_Status != AR_SUCCESS)
        return false;

    ArPointCloud_getNumberOfPoints(_ar_session, pointCloud, &point_num);
    if(point_num <= 0)
        return false;

    //point cloud data with 4 params (x,y,z, confidence)
    ArPointCloud_getData(_ar_session, pointCloud, &_pointCloudData);

    ArPointCloud_release(pointCloud);
    size_t memsize = 4 * sizeof(float) * point_num;
    pointCloudData = (float*)malloc(memsize);
    memcpy(pointCloudData, _pointCloudData, memsize);
    return true;
}

bool ARCoreManager::getPlaneData(ArPlane* plane, float*& plane_data,
                                 Matrixf& modelMat, osg::Vec3f& normal_vec,
                                 int32_t& vertice_num){
    if (cam_track_state != AR_TRACKING_STATE_TRACKING)
        return false;
    int32_t polygon_length;
    //get the number of elements(2*#vertives)
    ArPlane_getPolygonSize(_ar_session, plane, &polygon_length);

    if(polygon_length == 0)
        return false;
    vertice_num = polygon_length/2;

    plane_data = (float*)malloc(sizeof(float) * polygon_length);

    ArPlane_getPolygon(_ar_session, plane, plane_data);

    //get model matrix
    ArPose * arPose;
    ArPose_create(_ar_session, nullptr,&arPose);
    ArPlane_getCenterPose(_ar_session, plane, arPose);
    ArPose_getMatrix(_ar_session, arPose, modelMat.ptr());

    //get normal vector
    float plane_pose_raw[7] = {.0f};
    ArPose_getPoseRaw(_ar_session, arPose, plane_pose_raw);
    osg::Quat plane_quaternion(plane_pose_raw[0],plane_pose_raw[1], plane_pose_raw[2],plane_pose_raw[3]);
    // Get normal vector, normal is defined to be positive Y-position in local
    // frame.
    normal_vec = plane_quaternion * osg::Vec3f(0,1.0f,0);
    return true;
}
void ARCoreManager::getPlaneCenter(ArPlane* plane, osg::Vec3f& center_pos, osg::Quat& orientation){

    // get the model matrix for the plane
    ArPose * arPose;
    ArPose_create(_ar_session, nullptr,&arPose);
    ArPlane_getCenterPose(_ar_session, plane, arPose);
//    ArPose_getMatrix(_ar_session, arPose, modelMat.ptr());

    // get center position and orientation of the plane
    float plane_pose_raw[7] = {.0f};
    // extract the rotation and translation from the pose object
    ArPose_getPoseRaw(_ar_session, arPose, plane_pose_raw);

    // first 4 are the quarternion, last 3 are the translations
    osg::Quat plane_quaternion(plane_pose_raw[0],plane_pose_raw[1], plane_pose_raw[2],plane_pose_raw[3]);
    orientation = plane_quaternion;
    center_pos = osg::Vec3f(plane_pose_raw[4], plane_pose_raw[5], plane_pose_raw[6]);
}

LightSrc ARCoreManager::getLightEstimation(){
    if (cam_track_state != AR_TRACKING_STATE_TRACKING)
        return _envLight;
    ArLightEstimate* ar_light_estimate;
    ArLightEstimateState ar_light_estimate_state;
    ArLightEstimate_create(_ar_session, &ar_light_estimate);

    ArFrame_getLightEstimate(_ar_session, _ar_frame, ar_light_estimate);
    ArLightEstimate_getState(_ar_session, ar_light_estimate, &ar_light_estimate_state);
    if(ar_light_estimate_state == AR_LIGHT_ESTIMATE_STATE_VALID){
        ArLightEstimate_getColorCorrection(_ar_session, ar_light_estimate,  _envLight.color_correction);
        ArLightEstimate_getPixelIntensity(_ar_session, ar_light_estimate, &_envLight.intensity);
        _envLight.max_intensity*=0.998f;
        if(_envLight.intensity > _envLight.max_intensity){
            _envLight.max_intensity = _envLight.intensity;
            _envLight.lightSrc = Vec3f(.0, 1.0, .0) * (*view_mat);
        }
    }

    ArLightEstimate_destroy(ar_light_estimate);
    return _envLight;
}

float* ARCoreManager::getLightEstimation_SH() {
    if(_frame!=0 && _frame %100!=0)
        return LightingEstimator::instance()->getSHLightingParams();
//    update_ndk_image();
    return LightingEstimator::instance()->getSHLightingParams();
//    int size = TEX_SIZE * TEX_SIZE * 12;
//    uint8_t * image = new uint8_t[size];
//    for(int y=0; y<TEX_SIZE; y++){
//        int lineOff =y* 12*TEX_SIZE;
//        for(int i=0; i<4; i++){
//            memcpy(image + lineOff+ 3*i*TEX_SIZE, _envImgs[i]+lineOff, 3*TEX_SIZE * sizeof(uint8_t));
//        }
//    }
//
//
//
//        osg::Image * imageOSG= new osg::Image();
//        imageOSG->setImage(_ndk_image_width, _ndk_image_height, 1,
//                    GL_RGB, GL_RGB,
//                    GL_UNSIGNED_BYTE, image, osg::Image::USE_NEW_DELETE);
//
//        return LightingEstimator::instance()->getSHLightingParams(imageOSG);
}

planeMap ARCoreManager::getPlaneMap(){
    int detectedPlaneNum;
    ArTrackableList* plane_list = nullptr;
    ArTrackableList_create(_ar_session, & plane_list);
    CHECK(plane_list!= nullptr);

    ArSession_getAllTrackables(_ar_session, AR_TRACKABLE_PLANE, plane_list);
    ArTrackableList_getSize(_ar_session, plane_list, &detectedPlaneNum);

    for(int i=0; i<detectedPlaneNum; i++){
        ArTrackable * ar_trackable = nullptr;
        ArTrackableList_acquireItem(_ar_session, plane_list, i, &ar_trackable);

        //cast down trackable to plane
        ArPlane * ar_plane = ArAsPlane(ar_trackable);

        //check the trackingstate, if not tracking, skip rendering
        ArTrackingState trackingState;
        ArTrackable_getTrackingState(_ar_session, ar_trackable, &trackingState);
        if(trackingState != AR_TRACKING_STATE_TRACKING)
            continue;

        //check if the plane contain the subsume plane, if so, skip to avoid overlapping
        ArPlane * subsume_plane;
        ArPlane_acquireSubsumedBy(_ar_session, ar_plane, &subsume_plane);
        if(subsume_plane != nullptr)
            continue;

        auto iter = plane_color_map.find(ar_plane);
        if(iter == plane_color_map.end()){
            if(plane_color_map.empty())
                plane_color_map[ar_plane] = osg::Vec3f(1.0f, 1.0f, 1.0f);
            else
                plane_color_map[ar_plane] = GetRandomPlaneColor();
            _planes.push_back(ar_plane);
        }
        ArTrackable_release(ar_trackable);
    }
    ArTrackableList_destroy(plane_list);
    plane_list = nullptr;
    return plane_color_map;
}

bool ARCoreManager::updatePlaneHittest(float x, float y){
    _event_queue.push(Vec2f(x,y));
    if(!_ar_frame || !_ar_session) return false;
    ArHitResultList* hit_result_list = nullptr;
    ArHitResultList_create(_ar_session, &hit_result_list);
    CHECK(hit_result_list);
    ArFrame_hitTest(_ar_session, _ar_frame, x, y, hit_result_list);

    int32_t hit_result_list_size = 0;
    ArHitResultList_getSize(_ar_session, hit_result_list,
                            &hit_result_list_size);

    ArHitResult* ar_hit_result = nullptr;

    for (int32_t i = 0; i < hit_result_list_size; ++i) {
        ArHitResult *ar_hit = nullptr;
        ArHitResult_create(_ar_session, &ar_hit);
        ArHitResultList_getItem(_ar_session, hit_result_list, i, ar_hit);

        if (ar_hit == nullptr) {
            LOGE("HelloArApplication::OnTouched ArHitResultList_getItem error");
            return false;
        }

        ArTrackable *ar_trackable = nullptr;
        ArHitResult_acquireTrackable(_ar_session, ar_hit, &ar_trackable);
        ArTrackableType ar_trackable_type = AR_TRACKABLE_NOT_VALID;
        ArTrackable_getType(_ar_session, ar_trackable, &ar_trackable_type);
        ///////////////ONLY CHECK PLANE/////////////////////////////
        if(ar_trackable_type != AR_TRACKABLE_PLANE)
            continue;

        ArPose* hit_pose = nullptr;
        ArPose_create(_ar_session, nullptr, &hit_pose);
        ArHitResult_getHitPose(_ar_session, ar_hit, hit_pose);
        int32_t in_polygon = 0;
        ArPlane* ar_plane = ArAsPlane(ar_trackable);
        ArPlane_isPoseInPolygon(_ar_session, ar_plane, hit_pose, &in_polygon);

        float plane_pose_raw[7] = {0.f};
        ArPose_getPoseRaw(_ar_session, hit_pose, plane_pose_raw);

        ArPose_destroy(hit_pose);

        if (!in_polygon || calculateDistanceToPlane(plane_pose_raw, camera_pose_raw) < 0)
            continue;

        ar_hit_result = ar_hit;
        break;
    }
    ////////////////////////////
    if(ar_hit_result){
        ArAnchor* anchor = nullptr;
        if (ArHitResult_acquireNewAnchor(_ar_session, ar_hit_result, &anchor) !=
            AR_SUCCESS) {
            LOGE(
                    "HelloArApplication::OnTouched ArHitResult_acquireNewAnchor error");
            return false;
        }

        ArTrackingState tracking_state = AR_TRACKING_STATE_STOPPED;
        ArAnchor_getTrackingState(_ar_session, anchor, &tracking_state);
        if (tracking_state != AR_TRACKING_STATE_TRACKING) {
            ArAnchor_release(anchor);
            return false;
        }

        _hittedAnchors.push_back(anchor);
        ArHitResult_destroy(ar_hit_result);
        ar_hit_result = nullptr;

        ArHitResultList_destroy(hit_result_list);
        hit_result_list = nullptr;
    }
    return true;
}

bool ARCoreManager::getAnchorModelMatrixAt(Matrixf& modelMat, int loc, bool realCoord){
    if(loc>=_hittedAnchors.size())
        return false;
    ArTrackingState tracking_state = AR_TRACKING_STATE_STOPPED;
    ArAnchor_getTrackingState(_ar_session, _hittedAnchors[loc],
                              &tracking_state);
    if (tracking_state == AR_TRACKING_STATE_TRACKING) {
        ArPose* pose_;
        ArPose_create(_ar_session, nullptr, &pose_);
        ArAnchor_getPose(_ar_session, _hittedAnchors[loc], pose_);

        if(!realCoord){
            float pose_raw[7];
            ArPose_getPoseRaw(_ar_session, pose_, pose_raw);
            modelMat = cvr::rawRotation2OsgMatrix(pose_raw)* rawTrans2OsgMatrix(pose_raw+4);
        }else
            ArPose_getMatrix(_ar_session, pose_, modelMat.ptr());

        ArPose_destroy(pose_);
    }
    return true;
}

osg::Vec3f ARCoreManager::getRealWorldPositionFromScreen(float x, float y, float z){
    if(x > 1 || x < -1 || y>1 || y<-1){LOGE("Position should within [-1, 1]"); return osg::Vec3f(.0,.0,.0);}
    Matrixf invMat = Matrixf::inverse(getMVPMatrix());
    Vec4f nearPlanePos = Vec4f(x,y, z, 1.0f) * invMat;
    float inv_w = 1.0f / nearPlanePos.w();
    return Vec3f(nearPlanePos.x() * inv_w, nearPlanePos.y()*inv_w, nearPlanePos.z()*inv_w);
}
unsigned char* ARCoreManager::getImageData(int& width, int& height){
    if(!_ndk_image_width) return nullptr;
    width = _ndk_image_width; height =  _ndk_image_height;
    return _current_img.data;
}
void ARCoreManager::stitch_an_image() {
//    cv::Stitcher::Status status = _stitcher->stitch(_current_img,_panoImg);
//    if(status != cv::Stitcher::OK)
//        LOGE("Fail to stitch image error code = %d", int(status));
}

