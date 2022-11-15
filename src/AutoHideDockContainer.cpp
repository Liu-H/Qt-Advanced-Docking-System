/*******************************************************************************
** Qt Advanced Docking System
** Copyright (C) 2017 Uwe Kindler
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License as published by the Free Software Foundation; either
** version 2.1 of the License, or (at your option) any later version.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public
** License along with this library; If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/


//============================================================================
/// \file   AutoHideDockContainer.cpp
/// \author Syarif Fakhri
/// \date   05.09.2022
/// \brief  Implementation of CAutoHideDockContainer class
//============================================================================

//============================================================================
//                                   INCLUDES
//============================================================================
#include "AutoHideDockContainer.h"

#include <QXmlStreamWriter>
#include <QBoxLayout>
#include <QPainter>
#include <QSplitter>
#include <QPointer>
#include <QApplication>
#include <QCursor>

#include "DockManager.h"
#include "DockAreaWidget.h"
#include "ResizeHandle.h"
#include "DockComponentsFactory.h"
#include "AutoHideSideBar.h"
#include "AutoHideTab.h"


#include <iostream>

namespace ads
{
static const int ResizeMargin = 30;

//============================================================================
bool static isHorizontalArea(SideBarLocation Area)
{
	switch (Area)
	{
	case SideBarLocation::SideBarTop:
	case SideBarLocation::SideBarBottom: return true;
	case SideBarLocation::SideBarLeft:
	case SideBarLocation::SideBarRight: return false;
	default:
		return true;
	}

	return true;
}


//============================================================================
Qt::Edge static edgeFromSideTabBarArea(SideBarLocation Area)
{
	switch (Area)
	{
	case SideBarLocation::SideBarTop: return Qt::BottomEdge;
	case SideBarLocation::SideBarBottom: return Qt::TopEdge;
	case SideBarLocation::SideBarLeft: return Qt::RightEdge;
	case SideBarLocation::SideBarRight: return Qt::LeftEdge;
	default:
		return Qt::LeftEdge;
	}

	return Qt::LeftEdge;
}


//============================================================================
int resizeHandleLayoutPosition(SideBarLocation Area)
{
	switch (Area)
	{
	case SideBarLocation::SideBarBottom:
	case SideBarLocation::SideBarRight: return 0;

	case SideBarLocation::SideBarTop:
	case SideBarLocation::SideBarLeft: return 1;

	default:
		return 0;
	}

	return 0;
}


/**
 * Private data of CAutoHideDockContainer - pimpl
 */
struct AutoHideDockContainerPrivate
{
    CAutoHideDockContainer* _this;
	CDockAreaWidget* DockArea{nullptr};
	CDockWidget* DockWidget{nullptr};
	SideBarLocation SideTabBarArea = SideBarNone;
	QBoxLayout* Layout = nullptr;
	CResizeHandle* ResizeHandle = nullptr;
	QSize Size; // creates invalid size
	QPointer<CAutoHideTab> SideTab;

	/**
	 * Private data constructor
	 */
	AutoHideDockContainerPrivate(CAutoHideDockContainer *_public);

	/**
	 * Convenience function to get a dock widget area
	 */
	DockWidgetArea getDockWidgetArea(SideBarLocation area)
	{
        switch (area)
        {
            case SideBarLocation::SideBarLeft: return LeftDockWidgetArea;
            case SideBarLocation::SideBarRight: return RightDockWidgetArea;
            case SideBarLocation::SideBarBottom: return BottomDockWidgetArea;
            case SideBarLocation::SideBarTop: return TopDockWidgetArea;
            default:
            	return LeftDockWidgetArea;
        }

		return LeftDockWidgetArea;
	}

	/**
	 * Update the resize limit of the resize handle
	 */
	void updateResizeHandleSizeLimitMax()
	{
		auto Rect = _this->dockContainer()->contentRect();
		const auto maxResizeHandleSize = ResizeHandle->orientation() == Qt::Horizontal
			? Rect.width() : Rect.height();
		ResizeHandle->setMaxResizeSize(maxResizeHandleSize - ResizeMargin);
	}

	/**
	 * Convenience function to check, if this is an horizontal area
	 */
	bool isHorizontal() const
	{
		return isHorizontalArea(SideTabBarArea);
	}

	/**
	 * Forward this event to the dock container
	 */
	void forwardEventToDockContainer(QEvent* event)
	{
		auto DockContainer = _this->dockContainer();
		if (DockContainer)
		{
			DockContainer->handleAutoHideWidgetEvent(event, _this);
		}
	}

}; // struct AutoHideDockContainerPrivate


//============================================================================
AutoHideDockContainerPrivate::AutoHideDockContainerPrivate(
    CAutoHideDockContainer *_public) :
	_this(_public)
{

}


//============================================================================
CDockContainerWidget* CAutoHideDockContainer::dockContainer() const
{
	if (d->DockArea)
	{
		return d->DockArea->dockContainer();
	}
	else
	{
		return internal::findParent<CDockContainerWidget*>(this);
	}
}


//============================================================================
CAutoHideDockContainer::CAutoHideDockContainer(CDockWidget* DockWidget, SideBarLocation area, CDockContainerWidget* parent) :
	Super(parent),
    d(new AutoHideDockContainerPrivate(this))
{
	hide(); // auto hide dock container is initially always hidden
	d->SideTabBarArea = area;
	d->SideTab = componentsFactory()->createDockWidgetSideTab(nullptr);
	connect(d->SideTab, &CAutoHideTab::pressed, this, &CAutoHideDockContainer::toggleCollapseState);
	d->DockArea = new CDockAreaWidget(DockWidget->dockManager(), parent);
	d->DockArea->setObjectName("autoHideDockArea");
	d->DockArea->setAutoHideDockContainer(this);

	setObjectName("autoHideDockContainer");

	d->Layout = new QBoxLayout(isHorizontalArea(area) ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
	d->Layout->setContentsMargins(0, 0, 0, 0);
	d->Layout->setSpacing(0);
	setLayout(d->Layout);
	d->ResizeHandle = new CResizeHandle(edgeFromSideTabBarArea(area), this);
	d->ResizeHandle->setMinResizeSize(64);
	bool OpaqueResize = CDockManager::testConfigFlag(CDockManager::OpaqueSplitterResize);
	d->ResizeHandle->setOpaqueResize(OpaqueResize);
	d->Size = d->DockArea->size();

	addDockWidget(DockWidget);
	parent->registerAutoHideWidget(this);
	// The dock area should not be added to the layout before it contains the
	// dock widget. If you add it to the layout before it contains the dock widget
	// then you will likely see this warning for OpenGL widgets or QAxWidgets:
	// setGeometry: Unable to set geometry XxY+Width+Height on QWidgetWindow/'WidgetClassWindow
	d->Layout->addWidget(d->DockArea);
	d->Layout->insertWidget(resizeHandleLayoutPosition(area), d->ResizeHandle);
}


//============================================================================
void CAutoHideDockContainer::updateSize()
{
	auto dockContainerParent = dockContainer();
	if (!dockContainerParent)
	{
		return;
	}

	auto rect = dockContainerParent->contentRect();

	switch (sideBarLocation())
	{
	case SideBarLocation::SideBarTop:
		 resize(rect.width(), qMin(rect.height()  - ResizeMargin, d->Size.height()));
		 move(rect.topLeft());
		 break;

	case SideBarLocation::SideBarLeft:
		 resize(qMin(d->Size.width(), rect.width() - ResizeMargin), rect.height());
		 move(rect.topLeft());
		 break;

	case SideBarLocation::SideBarRight:
		 {
			 resize(qMin(d->Size.width(), rect.width() - ResizeMargin), rect.height());
			 QPoint p = rect.topRight();
			 p.rx() -= (width() - 1);
			 move(p);
		 }
		 break;

	case SideBarLocation::SideBarBottom:
		 {
			 resize(rect.width(), qMin(rect.height() - ResizeMargin, d->Size.height()));
			 QPoint p = rect.bottomLeft();
			 p.ry() -= (height() - 1);
			 move(p);
		 }
		 break;

	default:
		break;
	}
}

//============================================================================
CAutoHideDockContainer::~CAutoHideDockContainer()
{
	ADS_PRINT("~CAutoHideDockContainer");

	// Remove event filter in case there are any queued messages
	qApp->removeEventFilter(this);
	if (dockContainer())
	{
		dockContainer()->removeAutoHideWidget(this);
	}

	if (d->SideTab)
	{
		delete d->SideTab;
	}

	delete d;
}

//============================================================================
CAutoHideSideBar* CAutoHideDockContainer::sideBar() const
{
	return dockContainer()->sideTabBar(d->SideTabBarArea);
}


//============================================================================
CAutoHideTab* CAutoHideDockContainer::autoHideTab() const
{
	return d->SideTab;
}


//============================================================================
CDockWidget* CAutoHideDockContainer::dockWidget() const
{
	return d->DockWidget;
}

//============================================================================
void CAutoHideDockContainer::addDockWidget(CDockWidget* DockWidget)
{
	if (d->DockWidget)
	{
		// Remove the old dock widget at this area
        d->DockArea->removeDockWidget(d->DockWidget);
	}

	d->DockWidget = DockWidget;
	d->SideTab->setDockWidget(DockWidget);
    CDockAreaWidget* OldDockArea = DockWidget->dockAreaWidget();
    auto IsRestoringState = DockWidget->dockManager()->isRestoringState();
    if (OldDockArea && !IsRestoringState)
    {
		// The initial size should be a little bit bigger than the original dock
		// area size to prevent that the resize handle of this auto hid dock area
		// is near of the splitter of the old dock area.
		d->Size = OldDockArea->size() + QSize(16, 16);
        OldDockArea->removeDockWidget(DockWidget);
    }
	d->DockArea->addDockWidget(DockWidget);
	updateSize();
}


//============================================================================
SideBarLocation CAutoHideDockContainer::sideBarLocation() const
{
	return d->SideTabBarArea;
}

//============================================================================
CDockAreaWidget* CAutoHideDockContainer::dockAreaWidget() const
{
	return d->DockArea;
}

//============================================================================
void CAutoHideDockContainer::moveContentsToParent()
{
	cleanupAndDelete();
	// If we unpin the auto hide dock widget, then we insert it into the same
	// location like it had as a auto hide widget.  This brings the least surprise
	// to the user and he does not have to search where the widget was inserted.
	d->DockWidget->setDockArea(nullptr);
	auto DockContainer = dockContainer();
	DockContainer->addDockWidget(d->getDockWidgetArea(d->SideTabBarArea), d->DockWidget);
}


//============================================================================
void CAutoHideDockContainer::cleanupAndDelete()
{
	const auto dockWidget = d->DockWidget;
	if (dockWidget)
	{

		auto SideTab = d->SideTab;
        SideTab->removeFromSideBar();
        SideTab->setParent(nullptr);
        SideTab->hide();
	}

	hide();
	deleteLater();
}


//============================================================================
void CAutoHideDockContainer::saveState(QXmlStreamWriter& s)
{
	s.writeStartElement("Widget");
	s.writeAttribute("Name", d->DockWidget->objectName());
	s.writeAttribute("Closed", QString::number(d->DockWidget->isClosed() ? 1 : 0));
    s.writeAttribute("Size", QString::number(d->isHorizontal() ? d->Size.height() : d->Size.width()));
	s.writeEndElement();
}


//============================================================================
void CAutoHideDockContainer::toggleView(bool Enable)
{
	if (Enable)
	{
        if (d->SideTab)
        {
            d->SideTab->show();
        }
	}
	else
	{
        if (d->SideTab)
        {
            d->SideTab->hide();
        }
        hide();
        qApp->removeEventFilter(this);
	}
}


//============================================================================
void CAutoHideDockContainer::collapseView(bool Enable)
{
	if (Enable)
	{
		hide();
		qApp->removeEventFilter(this);
	}
	else
	{
		updateSize();
		d->updateResizeHandleSizeLimitMax();
		raise();
		show();
		d->DockWidget->dockManager()->setDockWidgetFocused(d->DockWidget);
		qApp->installEventFilter(this);
	}

	ADS_PRINT("CAutoHideDockContainer::collapseView " << Enable);
    d->SideTab->updateStyle();
}


//============================================================================
void CAutoHideDockContainer::toggleCollapseState()
{
	collapseView(isVisible());
}


//============================================================================
void CAutoHideDockContainer::setSize(int Size)
{
	if (d->isHorizontal())
	{
		d->Size.setHeight(Size);
	}
	else
	{
		d->Size.setWidth(Size);
	}

	updateSize();
}


//============================================================================
bool CAutoHideDockContainer::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize)
	{
		if (!d->ResizeHandle->isResizing())
		{
			updateSize();
		}
	}
	else if (event->type() == QEvent::MouseButtonPress)
	{
		auto Container = dockContainer();
		// First we check, if the mouse button press is inside the container
		// widget. If it is not, i.e. if someone resizes the main window or
		// clicks into the application menu or toolbar, then we ignore the
		// event
		auto widget = qobject_cast<QWidget*>(watched);
		bool IsContainer = false;
		while (widget)
		{
			if (widget == Container)
			{
				IsContainer = true;
			}
			widget = widget->parentWidget();
		}

		if (!IsContainer)
		{
			return Super::eventFilter(watched, event);
		}

		// Now we check, if the user clicked inside of this auto hide container.
		// If the click is inside of this auto hide container, then we can also
		// ignore the event, because the auto hide overlay should not get collapsed if
		// user works in it
		QMouseEvent* me = static_cast<QMouseEvent*>(event);
		auto GlobalPos = internal::globalPositionOf(me);
		auto pos = mapFromGlobal(GlobalPos);
		if (rect().contains(pos))
		{
			return Super::eventFilter(watched, event);
		}

		// Now check, if the user clicked into the side tab and ignore this event,
		// because the side tab click handler will call collapseView(). If we
		// do not ignore this here, then we will collapse the container and the side tab
		// click handler will uncollapse it
		auto SideTab = d->SideTab;
		pos = SideTab->mapFromGlobal(GlobalPos);
		if (SideTab->rect().contains(pos))
		{
			return Super::eventFilter(watched, event);
		}

		// If the mouse button down event is in the dock manager but outside
		// of the open auto hide container, then the auto hide dock widget
		// should get collapsed
		collapseView(true);
	}
	else if (event->type() == QEvent::NonClientAreaMouseButtonPress)
	{
		// If the user starts dragging a floating widget, then we collapse
		// the auto hide widget
		CFloatingDockContainer* FloatingWidget = qobject_cast<CFloatingDockContainer*>(watched);
		if (FloatingWidget)
		{
			collapseView(true);
		}
	}
    else if (event->type() == internal::FloatingWidgetDragStartEvent)
    {
        collapseView(true);
    }

	return Super::eventFilter(watched, event);
}


//============================================================================
void CAutoHideDockContainer::resizeEvent(QResizeEvent* event)
{
    Super::resizeEvent(event);
	if (d->ResizeHandle->isResizing())
	{
        d->Size = this->size();
		d->updateResizeHandleSizeLimitMax();
	}
}


//============================================================================
void CAutoHideDockContainer::leaveEvent(QEvent *event)
{
	// Resizing of the dock container via the resize handle in non opaque mode
	// mays cause a leave event that is not really a leave event. Therefore
	// we check here, if we are really outside of our rect.
	auto pos = mapFromGlobal(QCursor::pos());
	if (!rect().contains(pos))
	{
		d->forwardEventToDockContainer(event);
	}
	Super::leaveEvent(event);
}


//============================================================================
bool CAutoHideDockContainer::event(QEvent* event)
{
	switch (event->type())
	{
	case QEvent::Enter:
	case QEvent::Hide:
		 d->forwardEventToDockContainer(event);
		 break;

	default:
		break;
	}

	return Super::event(event);
}

}

