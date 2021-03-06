#include <cvrKernel/ScreenMVZones.h>
#include <cvrKernel/CVRCullVisitor.h>
#include <cvrKernel/CVRViewer.h>
#include <cvrKernel/NodeMask.h>
#include <cvrKernel/PluginHelper.h>
#include <cvrConfig/ConfigManager.h>
#include <cvrInput/TrackingManager.h>

#include <iostream>
#include <math.h>

using namespace cvr;

#ifdef WIN32
#define M_PI 3.141592653589793238462643
#endif

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MAX_ZONES_DEFAULT 16

// Statics
setContributionFunc ScreenMVZones::setContribution;
std::vector<setContributionFunc> ScreenMVZones::setContributionFuncs;
bool ScreenMVZones::_orientation3d;
bool ScreenMVZones::_autoAdjust;
bool ScreenMVZones::_multipleUsers;
bool ScreenMVZones::_zoneColoring;
float ScreenMVZones::_autoAdjustTarget;
float ScreenMVZones::_autoAdjustOffset;
int ScreenMVZones::_setZoneColumns;
int ScreenMVZones::_setZoneRows;
int ScreenMVZones::_maxZoneColumns;
int ScreenMVZones::_maxZoneRows;
osg::Vec4 ScreenMVZones::_clearColor = osg::Vec4(0,0,0,0);
bool ScreenMVZones::_autoContributionVar = true;
float ScreenMVZones::_contributionVar = M_PI;

/*** Declarations for setContribution functions ***/
void linear(osg::Vec3 toZone0, osg::Vec3 orientation0, float &contribution0,
        osg::Vec3 toZone1, osg::Vec3 orientation1, float &contribution1);
void cosine(osg::Vec3 toZone0, osg::Vec3 orientation0, float &contribution0,
        osg::Vec3 toZone1, osg::Vec3 orientation1, float &contribution1);
void gaussian(osg::Vec3 toZone0, osg::Vec3 orientation0, float &contribution0,
        osg::Vec3 toZone1, osg::Vec3 orientation1, float &contribution1);
/**************************************************/

ScreenMVZones::ScreenMVZones() :
        ScreenMVSimulator()
{
}

ScreenMVZones::~ScreenMVZones()
{
}

void ScreenMVZones::init(int mode)
{
    _stereoMode = (osg::DisplaySettings::StereoMode)mode;

    switch(_stereoMode)
    {
        default: // Shouldn't see this!
            std::cerr << "Error! Invalid _stereoMode.\n";
        case osg::DisplaySettings::HORIZONTAL_INTERLACE:
            //_viewPtr = _projPtr = NULL;
            break; // NexCAVE
        case osg::DisplaySettings::LEFT_EYE:
            //_viewPtr = &_viewLeftPtr;
            //_projPtr = &_projLeftPtr;
            break;
        case osg::DisplaySettings::RIGHT_EYE:
            //_viewPtr = &_viewRightPtr;
            //_projPtr = &_projRightPtr;
            break;
    }

    _viewPtr = _projPtr = NULL;

    _zonesChanged = true;
    _zones = 0;
    _zoneRows = 1;
    _zoneColumns = 1;
    _maxZoneColumns = ConfigManager::getInt("maxColumns","Zones",
    MAX_ZONES_DEFAULT);
    _maxZoneRows = ConfigManager::getInt("maxRows","Zones",4);
    _orientation3d = ConfigManager::getBool("Orientation3d",true);
    _autoAdjust = ConfigManager::getBool("autoAdjust","FrameRate",true);
    setAutoAdjustTarget(ConfigManager::getFloat("target","FrameRate",20));
    setAutoAdjustOffset(ConfigManager::getFloat("offset","FrameRate",4));

    _colorZones = false;

    /*** Setup setContributionFuncs Vector ***/
    setContribution = cosine;
    setContributionFuncs.push_back(linear);
    setContributionFuncs.push_back(cosine);
    setContributionFuncs.push_back(gaussian);
    /*** Done setting up setContributionFuncs Vector ***/

    _invScreenRotation = osg::Quat(-_myInfo->h * M_PI / 180.0,osg::Vec3(0,0,1),
            -_myInfo->p * M_PI / 180.0,osg::Vec3(1,0,0),
            -_myInfo->r * M_PI / 180.0,osg::Vec3(0,1,0));

    // create cameras, one for each zone (based on maximum zone quantity)
    createCameras();

    // Only need a single zone for 1 user
    _multipleUsers = 1 < TrackingManager::instance()->getNumHeads();

    if(!_multipleUsers)
    {
        setZoneColumns(1);
        setZoneRows(1);
    }
    else
    {
        setZoneColumns(_maxZoneColumns);
        setZoneRows(_maxZoneRows);
    }
}

void ScreenMVZones::computeViewProj()
{
    // Calculate zone quantity - sets _zonesChanged
    determineZoneQuantity();

    if(_zonesChanged)
    {
        _zones = _zoneRows * _zoneColumns;

        // Setup zones (zone centers)
        setupZones();

        // Setup cameras (recompute view matrices)
        setupCameras();
    }

    // Handle zone coloring toggling
    if(_colorZones && !_zoneColoring)
    {
        // We just turned off zone coloring
        setClearColor(_clearColor);
    }
    _colorZones = _zoneColoring;

    // Find eye interpolated locations based on contributions from users
    std::vector<osg::Vec3> eyeLeft;
    std::vector<osg::Vec3> eyeRight;
    setEyeLocations(eyeLeft,eyeRight);

    //translate screen to origin
    osg::Matrix screenTrans;
    screenTrans.makeTranslate(-_myInfo->xyz);

    //rotate screen to xz
    osg::Matrix screenRot;
    screenRot.makeRotate(_invScreenRotation);

    // Move eyes via screen changes
    for(int i = 0; i < _zones; i++)
    {
        eyeLeft[i] = eyeLeft[i] * screenTrans * screenRot;
        eyeRight[i] = eyeRight[i] * screenTrans * screenRot;
    }

    float zHeight = _myInfo->height / _zoneRows;
    float zWidth = _myInfo->width / _zoneColumns;

    // Set up ComputeStereoMatricesCallback per camera
    for(int i = 0; i < _zones; i++)
    {
        int r = i / _zoneColumns;
        int c = i % _zoneColumns;

        osgViewer::Renderer * renderer =
                dynamic_cast<osgViewer::Renderer*>(_camera[i]->getRenderer());
        StereoCallback * sc =
                dynamic_cast<StereoCallback*>(renderer->getSceneView(0)->getComputeStereoMatricesCallback());

        //make frustums
        float top, bottom, left, right;
        float screenDist;
        osg::Matrix cameraTrans;

        // NexCAVE or StarCAVE-Left-Eye
        if(_stereoMode != osg::DisplaySettings::RIGHT_EYE)
        {
            screenDist = -eyeLeft[i].y();

            top = _near
                    * (-_myInfo->height / 2.0 + (r + 1) * zHeight
                            - eyeLeft[i].z()) / screenDist;
            bottom = _near
                    * (-_myInfo->height / 2.0 + r * zHeight - eyeLeft[i].z())
                    / screenDist;
            left = _near * (-_myInfo->width / 2.0 + c * zWidth - eyeLeft[i].x())
                    / screenDist;
            right =
                    _near
                            * (-_myInfo->width / 2.0 + (c + 1) * zWidth
                                    - eyeLeft[i].x()) / screenDist;

            sc->_projLeft.makeFrustum(left,right,bottom,top,_near,_far);

            // move camera to origin
            cameraTrans.makeTranslate(-eyeLeft[i]);

            //make view
            sc->_viewLeft = screenTrans * screenRot * cameraTrans
                    * osg::Matrix::lookAt(osg::Vec3(0,0,0),osg::Vec3(0,1,0),
                            osg::Vec3(0,0,1));
        }

        // NexCAVE or StarCAVE-Right-Eye
        if(_stereoMode != osg::DisplaySettings::LEFT_EYE)
        {
            screenDist = -eyeRight[i].y();

            top = _near
                    * (-_myInfo->height / 2.0 + (r + 1) * zHeight
                            - eyeRight[i].z()) / screenDist;
            bottom = _near
                    * (-_myInfo->height / 2.0 + r * zHeight - eyeRight[i].z())
                    / screenDist;
            left = _near
                    * (-_myInfo->width / 2.0 + c * zWidth - eyeRight[i].x())
                    / screenDist;
            right = _near
                    * (-_myInfo->width / 2.0 + (c + 1) * zWidth
                            - eyeRight[i].x()) / screenDist;

            sc->_projRight.makeFrustum(left,right,bottom,top,_near,_far);

            // move camera to origin
            cameraTrans.makeTranslate(-eyeRight[i]);

            //make view
            sc->_viewRight = screenTrans * screenRot * cameraTrans
                    * osg::Matrix::lookAt(osg::Vec3(0,0,0),osg::Vec3(0,1,0),
                            osg::Vec3(0,0,1));
        }

        _camera[i]->setCullMask(CULL_MASK);
        _camera[i]->setCullMaskLeft(CULL_MASK_LEFT);
        _camera[i]->setCullMaskRight(CULL_MASK_RIGHT);
    }
    for(int i = _zones; i < _camera.size(); i++)
    {
        _camera[i]->setCullMask(0);
        _camera[i]->setCullMaskLeft(0);
        _camera[i]->setCullMaskRight(0);
    }
}

void ScreenMVZones::updateCamera()
{
    if(_viewPtr == NULL || _projPtr == NULL)
        return; // Not in StarCAVE

    for(int i = 0; i < _camera.size(); i++)
    {
        _camera[i]->setViewMatrix(*(*_viewPtr)[i]);
        _camera[i]->setProjectionMatrix(*(*_projPtr)[i]);
    }
}

osg::Matrixd ScreenMVZones::StereoCallback::computeLeftEyeProjection(
        const osg::Matrixd &) const
{
    return _projLeft;
}

osg::Matrixd ScreenMVZones::StereoCallback::computeLeftEyeView(
        const osg::Matrixd &) const
{
    return _viewLeft;
}

osg::Matrixd ScreenMVZones::StereoCallback::computeRightEyeProjection(
        const osg::Matrixd &) const
{
    return _projRight;
}

osg::Matrixd ScreenMVZones::StereoCallback::computeRightEyeView(
        const osg::Matrixd &) const
{
    return _viewRight;
}

void ScreenMVZones::setClearColor(osg::Vec4 color)
{
    _clearColor = color;

    for(int i = 0; i < _camera.size(); i++)
        _camera[i]->setClearColor(color);
}

ScreenInfo * ScreenMVZones::findScreenInfo(osg::Camera * c)
{
    for(int i = 0; i < _camera.size(); i++)
    {
        if(c == _camera[i].get())
        {
            return _myInfo;
        }
    }
    return NULL;
}

void ScreenMVZones::createCameras()
{
    osg::GraphicsContext* gc = _myInfo->myChannel->myWindow->gc;
    GLenum buffer =
            _myInfo->myChannel->myWindow->gc->getTraits()->doubleBuffer ?
                    GL_BACK : GL_FRONT;
    int quantity = _maxZoneRows * _maxZoneColumns;

    // make new cameras as necessary
    for(int i = 0; i < quantity; i++)
    {
        osg::ref_ptr<osg::Camera> cam = new osg::Camera();
        //osg::DisplaySettings * ds = new osg::DisplaySettings();
        //cam->setDisplaySettings(ds);
        _camera.push_back(cam);
        cam->setGraphicsContext(gc);

        cam->setDrawBuffer(buffer);
        cam->setReadBuffer(buffer);

        cam->setClearColor(_clearColor);

        CVRViewer::instance()->addSlave(_camera[i].get(),osg::Matrixd(),
                osg::Matrixd());
        osgViewer::Renderer * renderer =
                dynamic_cast<osgViewer::Renderer*>(_camera[i]->getRenderer());

        if(!renderer)
        {
            std::cerr << "Error getting renderer pointer." << std::endl;
        }
        else
        {
            osg::DisplaySettings * ds =
                    renderer->getSceneView(0)->getDisplaySettings();

            //ds->setStereo(_stereoMode == osg::DisplaySettings::HORIZONTAL_INTERLACE);
            ds->setStereo(true);
            ds->setStereoMode(_stereoMode);

            ds = renderer->getSceneView(1)->getDisplaySettings();

            //ds->setStereo(_stereoMode == osg::DisplaySettings::HORIZONTAL_INTERLACE);
            ds->setStereo(true);
            ds->setStereoMode(_stereoMode);

            StereoCallback * sc = new StereoCallback;

            _projLeftPtr.push_back(&sc->_projLeft);
            _projRightPtr.push_back(&sc->_projRight);
            _viewLeftPtr.push_back(&sc->_viewLeft);
            _viewRightPtr.push_back(&sc->_viewRight);

            renderer->getSceneView(0)->setComputeStereoMatricesCallback(sc);
            renderer->getSceneView(1)->setComputeStereoMatricesCallback(sc);
            renderer->getSceneView(0)->getDisplaySettings()->setSerializeDrawDispatch(
                    false);
            renderer->getSceneView(1)->getDisplaySettings()->setSerializeDrawDispatch(
                    false);

            if(ConfigManager::getEntry("value","CullingMode","CALVR")
                    == "CALVR")
            {
                renderer->getSceneView(0)->setCullVisitor(new CVRCullVisitor());
                renderer->getSceneView(0)->setCullVisitorLeft(
                        new CVRCullVisitor());
                renderer->getSceneView(0)->setCullVisitorRight(
                        new CVRCullVisitor());
                renderer->getSceneView(1)->setCullVisitor(new CVRCullVisitor());
                renderer->getSceneView(1)->setCullVisitorLeft(
                        new CVRCullVisitor());
                renderer->getSceneView(1)->setCullVisitorRight(
                        new CVRCullVisitor());
            }
        }
    }
}

void ScreenMVZones::determineZoneQuantity()
{
    _zonesChanged = true; // assume true, set false if wrong

    // set zone row/column quantity to 1 for a single user
    if(!_multipleUsers)
    {
        _zoneRows = _zoneColumns = 1;

        // Did we change since last frame?
        _zonesChanged = _zones != 1;
        return;
    }

    if(!_autoAdjust)
    {
        if(_setZoneColumns == _zoneColumns && _setZoneRows == _zoneRows)
        {
            _zonesChanged = false;
            return;
        }

        _zoneColumns = _setZoneColumns;
        _zoneRows = _setZoneRows;
        return;
    }

    double lastFrameDuration = CVRViewer::instance()->getLastFrameDuration();

    if(1.0 / lastFrameDuration < _autoAdjustTarget - _autoAdjustOffset)
    {
        if(_zoneColumns > 1)
            _zoneColumns--;
        else if(_zoneRows > 1)
            _zoneRows--;
        else
            _zonesChanged = false;
    }
    else if(1.0 / lastFrameDuration > _autoAdjustTarget + _autoAdjustOffset)
    {
        if(_zoneColumns < _maxZoneColumns)
            _zoneColumns++;
        else if(_zoneRows < _maxZoneRows)
            _zoneRows++;
        else
            _zonesChanged = false;
    }
    else
        _zonesChanged = false;

    // balance out rows/columns if we have changed them
    if(_zonesChanged)
    {
        const float THRESHOLD = 1.5;

        if(_zoneColumns * _zoneRows > THRESHOLD * _maxZoneColumns * _zoneRows
                && _zoneRows < _maxZoneRows)
        {
            _zoneColumns = _zoneColumns * _zoneRows / (_zoneRows + 1);
            _zoneRows++;
        }
        else if(THRESHOLD * _zoneColumns * _maxZoneRows
                < _maxZoneColumns * _zoneRows && _zoneRows > 1)
        {
            _zoneColumns = _zoneColumns * _zoneRows / (_zoneRows - 1);
            _zoneRows--;
        }
    }
}

void ScreenMVZones::setupZones()
{
    for(int i = _zoneCenter.size(); i < _zones; i++)
    {
        _zoneCenter.push_back(osg::Vec3());
    }

    float negWidth_2 = -_myInfo->myChannel->width / 2;
    float negHeight_2 = -_myInfo->myChannel->height / 2;
    float zoneWidth = _myInfo->myChannel->width / _zoneColumns;
    float zoneHeight = _myInfo->myChannel->height / _zoneRows;

    for(int r = 0; r < _zoneRows; r++)
    {
        for(int c = 0; c < _zoneColumns; c++)
        {
            osg::Vec4 zc = osg::Vec4(negWidth_2 + (c + .5) * zoneWidth,0,
                    negHeight_2 + (r + .5) * zoneHeight,1) * _myInfo->transform;
            _zoneCenter[r * _zoneColumns + c] = osg::Vec3(zc.x(),zc.y(),zc.z());
        }
    }
}

void ScreenMVZones::setupCameras()
{
    float left = _myInfo->myChannel->left;
    float bottom = _myInfo->myChannel->bottom;
    float zoneWidth = _myInfo->myChannel->width / _zoneColumns;
    float zoneHeight = _myInfo->myChannel->height / _zoneRows;

    for(int i = 0; i < _camera.size(); i++)
    {
        // Camera needs its viewport setup
        if(i < _zones)
        {
            int r = i / _zoneColumns;
            int c = i % _zoneColumns;

            // "extra" calculation handles float->int rounding
            _camera[i]->setViewport((int)(left + c * zoneWidth),
                    (int)(bottom + r * zoneHeight),
                    (int)(left + (c + 1) * zoneWidth)
                            - (int)(left + c * zoneWidth),
                    (int)(bottom + (r + 1) * zoneHeight)
                            - (int)(bottom + r * zoneHeight));
        }
        else //*(i >= _zones)*/
        {
            _camera[i]->setViewport(0,0,0,0);
        }
    }
}

void ScreenMVZones::setEyeLocations(std::vector<osg::Vec3> &eyeLeft,
        std::vector<osg::Vec3> &eyeRight)
{
    // For a single user, just use the default eye positions
    if(!_multipleUsers)
    {
        eyeLeft.push_back(defaultLeftEye(0));
        eyeRight.push_back(defaultRightEye(0));
        return;
    }

    // Get the head matrices and the eye positions
    osg::Matrix headMat0 = getCurrentHeadMatrix(0);
    osg::Matrix headMat1 = getCurrentHeadMatrix(1);
    osg::Vec3 pos0 = headMat0.getTrans();
    osg::Vec3 pos1 = headMat1.getTrans();

    osg::Vec3 eyeLeft0 = defaultLeftEye(0);
    osg::Vec3 eyeRight0 = defaultRightEye(0);
    osg::Vec3 eyeLeft1 = defaultLeftEye(1);
    osg::Vec3 eyeRight1 = defaultRightEye(1);

    // user orientations
    osg::Vec4 u0op = osg::Vec4(0,1,0,0) * headMat0;
    osg::Vec4 u1op = osg::Vec4(0,1,0,0) * headMat1;
    osg::Vec3 o0, o1;

    if(_orientation3d)
    {
        o0 = osg::Vec3(u0op.x(),u0op.y(),u0op.z());
        o1 = osg::Vec3(u1op.x(),u1op.y(),u1op.z());
    }
    else
    {
        o0 = osg::Vec3(u0op.x(),u0op.y(),0);
        o1 = osg::Vec3(u1op.x(),u1op.y(),0);
    }
    o0.normalize();
    o1.normalize();

    // auto-set contribution variable if needed (minimum of 90 degrees)
    if(_autoContributionVar)
        setContributionVar(MAX(M_PI/2,acos(o0 * o1)));

    // compute contributions and set eye locations
    for(int i = 0; i < _zones; i++)
    {
        // user to zone center vectors
        osg::Vec3 u0toZC = _zoneCenter[i] - pos0;
        osg::Vec3 u1toZC = _zoneCenter[i] - pos1;

        // normalize all vectors
        u0toZC.normalize();
        u1toZC.normalize();

        // set the contribution level of each user to the zone
        float contribution0, contribution1;
        setContribution(u0toZC,o0,contribution0,u1toZC,o1,contribution1);

        // set default values for the eyes for this camera
        eyeLeft.push_back(eyeLeft0 * contribution0 + eyeLeft1 * contribution1);
        eyeRight.push_back(
                eyeRight0 * contribution0 + eyeRight1 * contribution1);

        // set this camera's "clear color" based on contributions as neccessary
        if(_colorZones)
        {
            _camera[i]->setClearColor(
                    osg::Vec4(contribution0,contribution1,0,0));
        }
    }
}

void linear(osg::Vec3 toZone0, osg::Vec3 orientation0, float &contribution0,
        osg::Vec3 toZone1, osg::Vec3 orientation1, float &contribution1)
{
    // contribution factors for each user
    float var = ScreenMVZones::getContributionVar();

    float angle = acos(toZone0 * orientation0);
    if(angle >= var)
        contribution0 = 0;
    else
    {
        contribution0 = 1 - angle / var;
    }

    angle = acos(toZone1 * orientation1);
    if(angle >= var)
        contribution1 = 0;
    else
    {
        contribution1 = 1 - angle / var;
    }

    contribution0 = MAX(0.001,contribution0);
    contribution1 = MAX(0.001,contribution1);

    float cTotal = contribution0 + contribution1;

    contribution0 /= cTotal;
    contribution1 /= cTotal;
}

void cosine(osg::Vec3 toZone0, osg::Vec3 orientation0, float &contribution0,
        osg::Vec3 toZone1, osg::Vec3 orientation1, float &contribution1)
{
    // contribution factors for each user
    float var = ScreenMVZones::getContributionVar();

    float angle = acos(toZone0 * orientation0);
    if(angle >= var)
        contribution0 = 0;
    else
    {
        contribution0 = cos(angle * M_PI / 2 / var);
    }

    angle = acos(toZone1 * orientation1);
    if(angle >= var)
        contribution1 = 0;
    else
    {
        contribution1 = cos(angle * M_PI / 2 / var);
    }

    contribution0 = MAX(0.001,contribution0);
    contribution1 = MAX(0.001,contribution1);

    float cTotal = contribution0 + contribution1;

    contribution0 /= cTotal;
    contribution1 /= cTotal;
}

#if 0 // ifdef WIN32
double erf(double x)
{
    // constants
    double a1 = 0.254829592;
    double a2 = -0.284496736;
    double a3 = 1.421413741;
    double a4 = -1.453152027;
    double a5 = 1.061405429;
    double p = 0.3275911;

    // Save the sign of x
    int sign = 1;
    if (x < 0)
    sign = -1;
    x = fabs(x);

    // A&S formula 7.1.26
    double t = 1.0/(1.0 + p*x);
    double y = 1.0 - (((((a5*t + a4)*t) + a3)*t + a2)*t + a1)*t*exp(-x*x);

    return sign*y;
}
#endif

// Returns the value to the right of z in a standardized normal/gaussian distribution
float cdfGaussian(float z)
{
    return 1 - (1 + erf(z / sqrt((float)2))) / 2;
}

void gaussian(osg::Vec3 toZone0, osg::Vec3 orientation0, float &contribution0,
        osg::Vec3 toZone1, osg::Vec3 orientation1, float &contribution1)
{
    // contribution factors for each user
    float var = ScreenMVZones::getContributionVar();
    float sigma = var / 3;

    float angle = acos(toZone0 * orientation0);
    if(angle >= var)
        contribution0 = 0;
    else
    {
        contribution0 = cdfGaussian(angle / sigma);
    }

    angle = acos(toZone1 * orientation1);
    if(angle >= var)
        contribution1 = 0;
    else
    {
        contribution1 = cdfGaussian(angle / sigma);
    }

    contribution0 = MAX(0.001,contribution0);
    contribution1 = MAX(0.001,contribution1);

    float cTotal = contribution0 + contribution1;

    contribution0 /= cTotal;
    contribution1 /= cTotal;
}

bool ScreenMVZones::setSetContributionFunc(int funcNum)
{
    if(funcNum < 0 || funcNum >= setContributionFuncs.size())
        return false;

    setContribution = setContributionFuncs[funcNum];
    return true;
}

void ScreenMVZones::setOrientation3d(bool o3d)
{
    _orientation3d = o3d;
}

bool ScreenMVZones::getOrientation3d()
{
    return _orientation3d;
}

void ScreenMVZones::setZoneColumns(int columns)
{
    if(columns < 1 || columns > _maxZoneColumns)
        return;

    // in single user mode -> should not have multiple zones
    if(columns != 1 && !_multipleUsers)
        return;

    _setZoneColumns = columns;
}

void ScreenMVZones::setZoneRows(int rows)
{
    if(rows < 1 || rows > _maxZoneRows)
        return;

    // in single user mode -> should not have multiple zones
    if(rows != 1 && !_multipleUsers)
        return;

    _setZoneRows = rows;
}

int ScreenMVZones::getZoneColumns()
{
    return _setZoneColumns;
}

int ScreenMVZones::getMaxZoneColumns()
{
    return _maxZoneColumns;
}
int ScreenMVZones::getZoneRows()
{
    return _setZoneRows;
}

int ScreenMVZones::getMaxZoneRows()
{
    return _maxZoneRows;
}

void ScreenMVZones::setAutoAdjust(bool adjust)
{
    _autoAdjust = adjust;
}

bool ScreenMVZones::getAutoAdjust()
{
    return _autoAdjust;
}

void ScreenMVZones::setAutoAdjustTarget(float target)
{
    if(target < 0)
        return;

    _autoAdjustTarget = target;
}

float ScreenMVZones::getAutoAdjustTarget()
{
    return _autoAdjustTarget;
}

void ScreenMVZones::setAutoAdjustOffset(float offset)
{
    if(offset < 0)
        return;

    _autoAdjustOffset = offset;
}

float ScreenMVZones::getAutoAdjustOffset()
{
    return _autoAdjustOffset;
}

void ScreenMVZones::setMultipleUsers(bool multipleUsers)
{
    _multipleUsers = multipleUsers;
}

bool ScreenMVZones::getMultipleUsers()
{
    return _multipleUsers;
}

void ScreenMVZones::setZoneColoring(bool zoneColoring)
{
    _zoneColoring = zoneColoring;
}

bool ScreenMVZones::getZoneColoring()
{
    return _zoneColoring;
}

void ScreenMVZones::setContributionVar(float var)
{
    _contributionVar = var;
}

float ScreenMVZones::getContributionVar()
{
    return _contributionVar;
}

void ScreenMVZones::setAutoContributionVar(bool autoCV)
{
    _autoContributionVar = autoCV;
}

bool ScreenMVZones::getAutoContributionVar()
{
    return _autoContributionVar;
}
