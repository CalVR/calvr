#include "cvrMenu/NewUI/UISlider.h"
#include "cvrKernel/NodeMask.h"

#include <algorithm>

using namespace cvr;


UISlider::UISlider(std::string emptytexture, std::string filledtexture, std::string handletexture)
	: UIElement()
{
	empty = new UITexture(emptytexture);
	filled = new UITexture(filledtexture);
	handle = new UITexture(handletexture);
	handle->setAbsolutePos(handle->getAbsolutePos() + osg::Vec3(0, -0.1f, 0));
	addChild(empty);
	addChild(filled);
	addChild(handle);
	_button = 0;
	_percent = 1e-9;
}

UISlider::UISlider(osg::Vec4 emptyColor, osg::Vec4 filledColor, osg::Vec4 handleColor)
	: UIElement()
{
	empty = new UITexture(emptyColor);
	filled = new UITexture(filledColor);
	handle = new UITexture(handleColor);
	handle->setAbsolutePos(handle->getAbsolutePos() + osg::Vec3(0, -0.1f, 0));
	addChild(empty);
	addChild(filled);
	addChild(handle);
	_button = 0;
	_percent = 1e-9;
}

void UISlider::updateElement(osg::Vec3 pos, osg::Vec3 size)
{
	if (!_intersect)
	{
		createGeometry();
	}
	if (_dirty)
	{
		calculateBounds(pos, size);

		updateGeometry();
		for (int i = 0; i < _children.size(); ++i)
		{
			_children[i]->setDirty(true);
		}
		_dirty = false;
	}

	for (int i = 0; i < _children.size(); ++i)
	{
		UIElement* child = _children[i].get();

		float emptywidth = _actualSize.x() * (1.0f - _percent);
		float filledwidth = _actualSize.x() * _percent;

		if (child == filled)
		{
			filled->updateElement(_actualPos, osg::Vec3(filledwidth, _actualSize.y(), _actualSize.z()));
		}
		else if (child == empty)
		{
			empty->updateElement(_actualPos + osg::Vec3(filledwidth, 0, 0), osg::Vec3(emptywidth, _actualSize.y(), _actualSize.z()));
		}
		else if (child == handle)
		{
			handle->updateElement(_actualPos + osg::Vec3(filledwidth, 0, 0), _actualSize);
		}
		else
		{
			_children[i]->updateElement(_actualPos, _actualSize);
		}
	}
}

void UISlider::createGeometry()
{
	_transform = new osg::MatrixTransform();
	_intersect = new osg::Geode();
	//_geode = new osg::Geode();

	_group->addChild(_transform);
	_transform->addChild(_intersect);
	//_transform->addChild(_geode);

	_intersect->setNodeMask(cvr::INTERSECT_MASK);

	osg::Geometry* drawable = UIUtil::makeQuad(1, 1, osg::Vec4(1,1,1,1), osg::Vec3(0, 0, 0));
	//_geode->addDrawable(drawable);
	//should quadelement have an intersectable?
	_intersect->addDrawable(drawable);

	updateGeometry();

}

void UISlider::updateGeometry()
{
	osg::Matrix mat = osg::Matrix();
	mat.makeScale(_actualSize);
	mat.postMultTranslate(_actualPos);
	_transform->setMatrix(mat);
}

bool UISlider::processEvent(InteractionEvent* event)
{
	TrackedButtonInteractionEvent* tie = event->asTrackedButtonEvent();
	if (tie && tie->getButton() == _button)
	{
		if (tie->getInteraction() == BUTTON_DOWN || tie->getInteraction() == BUTTON_DRAG)
		{
			_percent = _lastHitPoint.x();
			_percent = std::min(std::max(1e-9f, _percent), 1.0f - 1e-9f);
			_dirty = true;
			//std::cerr << "<" << _lastHitPoint.x() << ", " << _lastHitPoint.y() << ", " << _lastHitPoint.z() << ">" << std::endl;
			return onPercentChange();
		}
	}

	return false;
}

void UISlider::processHover(bool hover)
{

}