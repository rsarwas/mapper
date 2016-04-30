/*
 *    Copyright 2012, 2013, 2014 Thomas Schöps
 *    Copyright 2013, 2014 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "tool_base.h"

#include <QApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>

#include "gui/main_window.h"
#include "gui/widgets/key_button_bar.h"
#include "map.h"
#include "map_editor.h"
#include "object_undo.h"
#include "map_widget.h"
#include "object.h"
#include "object_text.h"
#include "settings.h"
#include "tool_helpers.h"
#include "util.h"


MapEditorToolBase::MapEditorToolBase(const QCursor& cursor, MapEditorTool::Type type, MapEditorController* editor, QAction* tool_button)
: MapEditorTool(editor, type, tool_button),
  start_drag_distance(Settings::getInstance().getStartDragDistancePx()),
  angle_helper(new ConstrainAngleToolHelper()),
  snap_helper(new SnappingToolHelper(this)),
  snap_exclude_object(NULL),
  cur_map_widget(editor->getMainWidget()),
  key_button_bar(NULL),
  cursor(scaledToScreen(cursor)),
  preview_update_triggered(false),
  dragging(false),
  dragging_canceled(false),
  renderables(new MapRenderables(map())),
  old_renderables(new MapRenderables(map()))
{
	angle_helper->setActive(false);
}

MapEditorToolBase::~MapEditorToolBase()
{
	deleteOldSelectionRenderables(*old_renderables, false);
	if (key_button_bar)
		editor->deletePopupWidget(key_button_bar);
}

void MapEditorToolBase::init()
{
	connect(map(), SIGNAL(objectSelectionChanged()), this, SLOT(objectSelectionChanged()));
	connect(map(), SIGNAL(selectedObjectEdited()), this, SLOT(updateDirtyRect()));
	initImpl();
	updateDirtyRect();
	updateStatusText();
	
	MapEditorTool::init();
}

const QCursor& MapEditorToolBase::getCursor() const
{
	return cursor;
}

bool MapEditorToolBase::mousePressEvent(QMouseEvent* event, MapCoordF map_coord, MapWidget* widget)
{
	active_modifiers = Qt::KeyboardModifiers(event->modifiers() | (key_button_bar ? key_button_bar->activeModifiers() : 0));
	if (event->button() == Qt::LeftButton)
	{
		cur_map_widget = widget;
		
		click_pos = event->pos();
		click_pos_map = map_coord;
		
		cur_pos = click_pos;
		cur_pos_map = click_pos_map;
		calcConstrainedPositions(widget);
		
		clickPress();
		return true;
	}
	else if (event->button() == Qt::RightButton)
	{
		// Do not show the ring menu when editing
		return editingInProgress();
	}
	else
	{
		return false;
	}
}

bool MapEditorToolBase::mouseMoveEvent(QMouseEvent* event, MapCoordF map_coord, MapWidget* widget)
{
	active_modifiers = Qt::KeyboardModifiers(event->modifiers() | (key_button_bar ? key_button_bar->activeModifiers() : 0));
	cur_pos = event->pos();
	cur_pos_map = map_coord;
	calcConstrainedPositions(widget);
	
	if (event->buttons().testFlag(Qt::LeftButton))
	{
		if (dragging)
		{
			updateDragging();
		}
		else if (dragging_canceled)
		{
			click_pos = cur_pos;
			click_pos_map = cur_pos_map;
			dragging_canceled = false;
		}
		else if ((event->pos() - click_pos).manhattanLength() >= start_drag_distance)
		{
			startDragging();
		}
		return true;
	}
	else
	{
		mouseMove();
		return false;
	}
}

bool MapEditorToolBase::mouseReleaseEvent(QMouseEvent* event, MapCoordF map_coord, MapWidget* widget)
{
	active_modifiers = Qt::KeyboardModifiers(event->modifiers() | (key_button_bar ? key_button_bar->activeModifiers() : 0));
	cur_pos = event->pos();
	cur_pos_map = map_coord;
	calcConstrainedPositions(widget);
	
	if (event->button() == Qt::LeftButton)
	{
		if (dragging)
		{
			finishDragging();
		}
		else
		{
			clickRelease();
		}
		return true;
	}
	else if (event->button() == Qt::RightButton)
	{
		// Do not show the ring menu when editing
		return editingInProgress();
	}
	else
	{
		return false;
	}
}

bool MapEditorToolBase::keyPressEvent(QKeyEvent* event)
{
	active_modifiers = Qt::KeyboardModifiers(event->modifiers() | (key_button_bar ? key_button_bar->activeModifiers() : 0));
#if defined(Q_OS_MAC)
	// FIXME: On Mac, QKeyEvent::modifiers() seems to return the keyboard 
	// modifier flags that existed immediately before the event occurred.
	// This is in contradiction to the documenation for QKeyEvent::modifiers(),
	// but the documented behaviour of (parent) QInputEvent::modifiers().
	// Qt5 doc says QKeyEvent::modifiers() "cannot always be trusted." ...
	switch (event->key())
	{
		case Qt::Key_Shift:
			active_modifiers |= Qt::ShiftModifier;
			break;
		case Qt::Key_Control:
			active_modifiers |= Qt::ControlModifier;
			break;
		case Qt::Key_Alt:
			active_modifiers |= Qt::AltModifier;
			break;
		case Qt::Key_Meta:
			active_modifiers |= Qt::MetaModifier;
			break;
		default:
			; // nothing
	}
#endif
	return keyPress(event);
}

bool MapEditorToolBase::keyReleaseEvent(QKeyEvent* event)
{
	active_modifiers = Qt::KeyboardModifiers(event->modifiers() | (key_button_bar ? key_button_bar->activeModifiers() : 0));
#if defined(Q_OS_MAC)
	// FIXME: On Mac, QKeyEvent::modifiers() seems to return the keyboard 
	// modifier flags that existed immediately before the event occurred.
	// This is in contradiction to the documenation for QKeyEvent::modifiers(),
	// but the documented behaviour of (parent) QInputEvent::modifiers().
	// Qt5 doc says QKeyEvent::modifiers() "cannot always be trusted." ...
	switch (event->key())
	{
		case Qt::Key_Shift:
			active_modifiers &= ~Qt::ShiftModifier;
			break;
		case Qt::Key_Control:
			active_modifiers &= ~Qt::ControlModifier;
			break;
		case Qt::Key_Alt:
			active_modifiers &= ~Qt::AltModifier;
			break;
		case Qt::Key_Meta:
			active_modifiers &= ~Qt::MetaModifier;
			break;
		default:
			; // nothing
	}
#endif
	return keyRelease(event);
}

void MapEditorToolBase::draw(QPainter* painter, MapWidget* widget)
{
	drawImpl(painter, widget);
	if (angle_helper->isActive())
		angle_helper->draw(painter, widget);
	if (snap_helper->getFilter() != SnappingToolHelper::NoSnapping)
		snap_helper->draw(painter, widget);
}

void MapEditorToolBase::updateDirtyRect()
{
	int pixel_border = 0;
	QRectF rect;
	
	map()->includeSelectionRect(rect);
	if (angle_helper->isActive())
	{
		angle_helper->includeDirtyRect(rect);
		pixel_border = qMax(pixel_border, angle_helper->getDisplayRadius());
	}
	if (snap_helper->getFilter() != SnappingToolHelper::NoSnapping)
	{
		snap_helper->includeDirtyRect(rect);
		pixel_border = qMax(pixel_border, snap_helper->getDisplayRadius());
	}
	
	pixel_border = qMax(pixel_border, updateDirtyRectImpl(rect));
	if (pixel_border >= 0)
		map()->setDrawingBoundingBox(rect, pixel_border, true);
	else
		map()->clearDrawingBoundingBox();
}

void MapEditorToolBase::objectSelectionChanged()
{
	objectSelectionChangedImpl();
}

void MapEditorToolBase::drawImpl(QPainter* painter, MapWidget* widget)
{
	drawSelectionOrPreviewObjects(painter, widget);
}

void MapEditorToolBase::clickPress()
{
	// nothing
}

void MapEditorToolBase::clickRelease()
{
	// nothing
}

void MapEditorToolBase::mouseMove()
{
	// nothing
}

void MapEditorToolBase::gestureStarted()
{
	if (dragging)
		cancelDragging();
}

void MapEditorToolBase::startDragging() 
{
	Q_ASSERT(!dragging);
	dragging = true;
	dragging_canceled = false;
	dragStart();
	dragMove();
}

void MapEditorToolBase::updateDragging()
{
	Q_ASSERT(dragging);
	dragMove();
}

void MapEditorToolBase::finishDragging()
{
	Q_ASSERT(dragging);
	dragMove();
	dragging = false;
	dragFinish();
}

void MapEditorToolBase::cancelDragging()
{
	Q_ASSERT(dragging);
	dragging = false;
	dragging_canceled = true;
	dragCanceled();
}

void MapEditorToolBase::dragStart() 
{
	// nothing
}

void MapEditorToolBase::dragMove()
{
	// nothing
}

void MapEditorToolBase::dragFinish()
{
	// nothing
}

void MapEditorToolBase::dragCanceled()
{
	// nothing
}

bool MapEditorToolBase::keyPress(QKeyEvent* event)
{
	Q_UNUSED(event);
	return false;
}

bool MapEditorToolBase::keyRelease(QKeyEvent* event)
{
	Q_UNUSED(event);
	return false;
}

void MapEditorToolBase::updatePreviewObjectsSlot()
{
	preview_update_triggered = false;
	if (editingInProgress())
		updatePreviewObjects();
}

int MapEditorToolBase::updateDirtyRectImpl(QRectF& rect)
{
	Q_UNUSED(rect);
	return -1;
}

void MapEditorToolBase::updatePreviewObjects()
{
	if (!editingInProgress())
	{
		qWarning("MapEditorToolBase::updatePreviewObjects() called but editing == false");
		return;
	}
	updateSelectionEditPreview(*renderables);
	updateDirtyRect();
}

void MapEditorToolBase::updatePreviewObjectsAsynchronously()
{
	if (!editingInProgress())
	{
		qWarning("MapEditorToolBase::updatePreviewObjectsAsynchronously() called but editing == false");
		return;
	}
	
	if (!preview_update_triggered)
	{
		QTimer::singleShot(10, this, SLOT(updatePreviewObjectsSlot()));
		preview_update_triggered = true;
	}
}

void MapEditorToolBase::drawSelectionOrPreviewObjects(QPainter* painter, MapWidget* widget, bool draw_opaque)
{
	map()->drawSelection(painter, true, widget, renderables->empty() ? NULL : renderables.data(), draw_opaque);
}

void MapEditorToolBase::startEditing()
{
	Q_ASSERT(!editingInProgress());
	
	setEditingInProgress(true);
	startEditingSelection(*old_renderables);
}

void MapEditorToolBase::abortEditing()
{
	Q_ASSERT(editingInProgress());
	
	resetEditedObjects();
	finishEditingSelection(*renderables, *old_renderables, false);
	setEditingInProgress(false);
}

// virtual
void MapEditorToolBase::finishEditing()
{
	finishEditing(false, true);
}

void MapEditorToolBase::finishEditing(bool delete_objects, bool create_undo_step)
{
	Q_ASSERT(editingInProgress());
	
	finishEditingSelection(*renderables, *old_renderables, create_undo_step, delete_objects);
	map()->setObjectsDirty();
	map()->emitSelectionEdited();
	MapEditorTool::finishEditing();
}

void MapEditorToolBase::activateAngleHelperWhileEditing(bool enable)
{
	angle_helper->setActive(enable);
	calcConstrainedPositions(cur_map_widget);
	if (dragging)
		dragMove();
	else
		mouseMove();
}

void MapEditorToolBase::activateSnapHelperWhileEditing(bool enable)
{
	Q_UNUSED(enable);
	
	snap_helper->setFilter(SnappingToolHelper::AllTypes);
	calcConstrainedPositions(cur_map_widget);
	if (dragging)
		dragMove();
	else
		mouseMove();
}

void MapEditorToolBase::calcConstrainedPositions(MapWidget* widget)
{
	if (snap_helper->getFilter() != SnappingToolHelper::NoSnapping)
	{
		SnappingToolHelperSnapInfo info;
		constrained_pos_map = MapCoordF(snap_helper->snapToObject(cur_pos_map, widget, &info, snap_exclude_object));
		constrained_pos = widget->mapToViewport(constrained_pos_map).toPoint();
		snapped_to_pos = info.type != SnappingToolHelper::NoSnapping;
	}
	else
	{
		constrained_pos_map = cur_pos_map;
		constrained_pos = cur_pos;
		snapped_to_pos = false;
	}
	if (angle_helper->isActive())
	{
		QPointF temp_pos;
		angle_helper->getConstrainedCursorPositions(constrained_pos_map, constrained_pos_map, temp_pos, widget);
		constrained_pos = temp_pos.toPoint();
	}
}
