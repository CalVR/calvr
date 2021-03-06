#ifndef CALVR_MENU_TEXT_ENTRY_ITEM_H
#define CALVR_MENU_TEXT_ENTRY_ITEM_H

#include <cvrMenu/Export.h>
#include <cvrMenu/MenuItemGroup.h>

#include <string>

namespace cvr
{

class MenuButton;
class TextInputPanel;

class CVRMENU_EXPORT MenuTextEntryItem : public MenuItemGroup, public MenuCallback
{
    public:
        MenuTextEntryItem(std::string label, std::string text, MenuItemGroup::AlignmentHint hint = MenuItemGroup::ALIGN_LEFT);
        virtual ~MenuTextEntryItem();

        std::string getText();
        std::string getLabel();
        void setText(std::string text);
        void setLabel(std::string label);

        void setSearchList(std::vector<std::string> & list, int numDisplayResults);

        virtual void menuCallback(MenuItem * item, int handID);
    protected:
        std::string _label;
        MenuButton * _numberText;
        TextInputPanel * _inputPanel;
        MenuItemGroup * _enterRow;
        MenuButton * _enterButton;
        std::string _text;
};

}

#endif
