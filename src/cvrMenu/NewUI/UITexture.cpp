#include "cvrMenu/NewUI/UITexture.h"
#include "cvrMenu/NewUI/UIUtil.h"
#include "cvrKernel/NodeMask.h"

using namespace cvr;

void UITexture::updateGeometry()
{
	UIQuadElement::updateGeometry();
	//_transform->setNodeMask(~cvr::INTERSECT_MASK);
	
	osg::Matrix mat = _transform->getMatrix();
	mat.preMultRotate(_rotQuat);
	mat.preMultTranslate(osg::Vec3(-.5, 0, .5));
	mat.postMultTranslate(osg::Vec3(mat.getScale().x() * .5, 0, mat.getScale().z() *-.5));

	_transform->setMatrix(mat);
	if (_texture.valid())
	{
		_geode->getDrawable(0)->getOrCreateStateSet()->setTextureAttributeAndModes(0, _texture, osg::StateAttribute::ON);
		_geode->getDrawable(0)->getOrCreateStateSet()->setDefine("USE_TEXTURE", true);
	}
}

void UITexture::setTexture(osg::Texture2D* texture)
{
	_texture = texture;
	/*
	if (_texture.valid())
	{
		_geode->getDrawable(0)->getOrCreateStateSet()->setTextureAttributeAndModes(0, _texture, osg::StateAttribute::ON);
	}
	*/
}

void UITexture::setTexture(std::string texturePath)
{
	_texture = UIUtil::loadImage(texturePath);
	/*
	if (_texture && _texture.valid())
	{
		_geode->getDrawable(0)->getOrCreateStateSet()->setTextureAttributeAndModes(0, _texture, osg::StateAttribute::ON);
	}
	*/
}
