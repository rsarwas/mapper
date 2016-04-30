/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2013-2015 Kai Pastor
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


#include "tool_edit_point.h"

#include <limits>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>

#include "util.h"
#include "symbol.h"
#include "object.h"
#include "object_text.h"
#include "map.h"
#include "map_widget.h"
#include "object_undo.h"
#include "tool_draw_text.h"
#include "tool_helpers.h"
#include "symbol_line.h"
#include "symbol_text.h"
#include "renderable.h"
#include "settings.h"
#include "map_editor.h"
#include "gui/main_window.h"
#include "gui/modifier_key.h"
#include "gui/widgets/key_button_bar.h"

class SymbolWidget;


namespace
{
	/**
	 * Maximum number of objects in the selection for which point handles
	 * will still be displayed (and can be edited).
	 */
	static unsigned int max_objects_for_handle_display = 10;
	
	/**
	 * The value which indicates that no point of the current object is hovered.
	 */
	static auto no_point = std::numeric_limits<MapCoordVector::size_type>::max();
}



EditPointTool::EditPointTool(MapEditorController* editor, QAction* tool_button)
 : EditTool { editor, EditPoint, tool_button }
 , hover_state { OverNothing }
 , hover_object { nullptr }
 , hover_point { 0 }
 , box_selection { false }
 , no_more_effect_on_click { false }
 , space_pressed { false }
 , text_editor { nullptr }
{
	// noting else
}

EditPointTool::~EditPointTool()
{
	delete text_editor;
}

bool EditPointTool::addDashPointDefault() const
{
	// Toggle dash points depending on if the selected symbol has a dash symbol.
	// TODO: instead of just looking if it is a line symbol with dash points,
	// could also check for combined symbols containing lines with dash points
	return ( hover_object &&
	         hover_object->getSymbol()->getType() == Symbol::Line &&
	         hover_object->getSymbol()->asLine()->getDashSymbol() != nullptr );
}

bool EditPointTool::mousePressEvent(QMouseEvent* event, MapCoordF map_coord, MapWidget* widget)
{
	// TODO: port TextObjectEditorHelper to MapEditorToolBase
	if (text_editor)
	{
		if (!text_editor->mousePressEvent(event, map_coord, widget))
			finishEditing();
		return true;
	}
	else
		return MapEditorToolBase::mousePressEvent(event, map_coord, widget);
}

bool EditPointTool::mouseMoveEvent(QMouseEvent* event, MapCoordF map_coord, MapWidget* widget)
{
	// TODO: port TextObjectEditorHelper to MapEditorToolBase
	if (text_editor)
		return text_editor->mouseMoveEvent(event, map_coord, widget);
	else
		return MapEditorToolBase::mouseMoveEvent(event, map_coord, widget);
}

bool EditPointTool::mouseReleaseEvent(QMouseEvent* event, MapCoordF map_coord, MapWidget* widget)
{
	// TODO: port TextObjectEditorHelper to MapEditorToolBase
	if (text_editor)
	{
		if (event->button() == Qt::LeftButton)
		{
			box_selection = false;
		}
		if (!text_editor->mouseReleaseEvent(event, map_coord, widget))
			finishEditing();
		return true;
	}
	else
		return MapEditorToolBase::mouseReleaseEvent(event, map_coord, widget);
}

void EditPointTool::mouseMove()
{
	updateHoverState(cur_pos_map);
	
	// For texts, decide whether to show the beam cursor
	if (hoveringOverSingleText())
		cur_map_widget->setCursor(QCursor(Qt::IBeamCursor));
	else
		cur_map_widget->setCursor(getCursor());
}

void EditPointTool::clickPress()
{
	Q_ASSERT(!hover_state.testFlag(OverObjectNode) ||
	         !hover_state.testFlag(OverPathEdge) ||
	         hover_object);
	
	if (hover_state.testFlag(OverPathEdge) &&
	    active_modifiers & Qt::ControlModifier)
	{
		// Add new point to path
		PathObject* path = hover_object->asPath();
		
		float distance_sq;
		PathCoord path_coord;
		path->calcClosestPointOnPath(cur_pos_map, distance_sq, path_coord);
		
		float click_tolerance_map_sq = cur_map_widget->getMapView()->pixelToLength(clickTolerance());
		click_tolerance_map_sq = click_tolerance_map_sq * click_tolerance_map_sq;
		
		if (distance_sq <= click_tolerance_map_sq)
		{
			startEditing();
			QScopedValueRollback<bool> no_effect_rollback(no_more_effect_on_click);
			no_more_effect_on_click = true;
			startDragging();
			hover_state = OverObjectNode;
			hover_point = path->subdivide(path_coord);
			if (addDashPointDefault() ^ space_pressed)
			{
				MapCoord point = path->getCoordinate(hover_point);
				point.setDashPoint(true);
				path->setCoordinate(hover_point, point);
				map()->emitSelectionEdited();
			}
			startEditingSetup();
			updatePreviewObjects();
		}
	}
    else if (hover_state.testFlag(OverObjectNode) &&
	         hover_object->getType() == Object::Path)
	{
		PathObject* hover_object = this->hover_object->asPath();
		Q_ASSERT(hover_point < hover_object->getCoordinateCount());
		
		if (space_pressed &&
		    !hover_object->isCurveHandle(hover_point))
		{
			// Switch point between dash / normal point
			createReplaceUndoStep(hover_object);
			
			MapCoord& hover_coord = hover_object->getCoordinate(hover_point);
			hover_coord.setDashPoint(!hover_coord.isDashPoint());
			hover_object->update();
			updateDirtyRect();
			no_more_effect_on_click = true;
		}
		else if (active_modifiers & Qt::ControlModifier)
		{
			auto hover_point_part_index = hover_object->findPartIndexForIndex(hover_point);
			PathPart& hover_point_part = hover_object->parts()[hover_point_part_index];
			
			if (hover_object->isCurveHandle(hover_point))
			{
				// Convert the curve into a straight line
				createReplaceUndoStep(hover_object);
				hover_object->deleteCoordinate(hover_point, false);
				hover_object->update();
				map()->emitSelectionEdited();
				updateHoverState(cur_pos_map);
				updateDirtyRect();
				no_more_effect_on_click = true;
			}
			else
			{
				// Delete the point
				if (hover_point_part.countRegularNodes() <= 2 ||
				    ( !(hover_object->getSymbol()->getContainedTypes() & Symbol::Line) &&
				      hover_point_part.size() <= 3 ) )
				{
					// Not enough remaining points -> delete the part and maybe object
					if (hover_object->parts().size() == 1)
					{
						map()->removeObjectFromSelection(hover_object, false);
						auto undo_step = new AddObjectsUndoStep(map());
						auto part = map()->getCurrentPart();
						int index = part->findObjectIndex(hover_object);
						Q_ASSERT(index >= 0);
						undo_step->addObject(index, hover_object);
						map()->deleteObject(hover_object, true);
						map()->push(undo_step);
						map()->setObjectsDirty();
						map()->emitSelectionEdited();
						updateHoverState(cur_pos_map);
					}
					else
					{
						createReplaceUndoStep(hover_object);
						hover_object->deletePart(hover_point_part_index);
						hover_object->update();
						map()->emitSelectionEdited();
						updateHoverState(cur_pos_map);
						updateDirtyRect();
					}
					no_more_effect_on_click = true;
				}
				else
				{
					// Delete the point only
					createReplaceUndoStep(hover_object);
					int delete_bezier_spline_point_setting;
					if (active_modifiers & Qt::ShiftModifier)
						delete_bezier_spline_point_setting = Settings::EditTool_DeleteBezierPointActionAlternative;
					else
						delete_bezier_spline_point_setting = Settings::EditTool_DeleteBezierPointAction;
					hover_object->deleteCoordinate(hover_point, true, Settings::getInstance().getSettingCached((Settings::SettingsEnum)delete_bezier_spline_point_setting).toInt());
					hover_object->update();
					map()->emitSelectionEdited();
					updateHoverState(cur_pos_map);
					updateDirtyRect();
					no_more_effect_on_click = true;
				}
			}
		}
	}
	else if (hoveringOverSingleText())
	{
		TextObject* hover_object = map()->getFirstSelectedObject()->asText();
		startEditing();
		
		// Don't show the original text while editing
		map()->removeRenderablesOfObject(hover_object, true);
		
		// Make sure that the TextObjectEditorHelper remembers the correct standard cursor
		cur_map_widget->setCursor(getCursor());
		
		old_text = hover_object->getText();
		old_horz_alignment = (int)hover_object->getHorizontalAlignment();
		old_vert_alignment = (int)hover_object->getVerticalAlignment();
		text_editor = new TextObjectEditorHelper(hover_object, editor);
		connect(text_editor, SIGNAL(selectionChanged(bool)), this, SLOT(textSelectionChanged(bool)));
		
		// Select clicked position
		int pos = hover_object->calcTextPositionAt(cur_pos_map, false);
		text_editor->setSelection(pos, pos);
		
		updatePreviewObjects();
	}
	
	click_timer.restart();
}

void EditPointTool::clickRelease()
{
	// Maximum time in milliseconds a click may take to cause
	// a selection change if hovering over a handle / frame.
	// If the click takes longer, it is assumed that the user
	// wanted to move the objects instead and no selection change is done.
	const int selection_click_time_threshold = 150;
	
	if (no_more_effect_on_click)
	{
		no_more_effect_on_click = false;
		return;
	}
	if (hover_state != OverNothing &&
	    click_timer.elapsed() >= selection_click_time_threshold)
	{
		return;
	}
	
	object_selector->selectAt(cur_pos_map, cur_map_widget->getMapView()->pixelToLength(clickTolerance()), active_modifiers & Qt::ShiftModifier);
	updateHoverState(cur_pos_map);
}

void EditPointTool::dragStart()
{
	if (no_more_effect_on_click)
		return;
	
	updateHoverState(click_pos_map);
	
	if (hover_state == OverNothing)
	{
		box_selection = true;
	}
	else
	{
		startEditing();
		startEditingSetup();
		
		if (active_modifiers & Qt::ControlModifier)
			activateAngleHelperWhileEditing();
		if (active_modifiers & Qt::ShiftModifier)
			activateSnapHelperWhileEditing();
	}
}

void EditPointTool::dragMove()
{
	if (no_more_effect_on_click)
		return;
	
	if (editingInProgress())
	{
		if (snapped_to_pos && handle_offset != MapCoordF(0, 0))
		{
			// Snapped to a position. Correct the handle offset, so the
			// object moves to this position exactly.
			click_pos_map += handle_offset;
			object_mover->setStartPos(click_pos_map);
			handle_offset = MapCoordF(0, 0);
		}
		
		object_mover->move(constrained_pos_map, !(active_modifiers & Qt::ShiftModifier));
		updatePreviewObjectsAsynchronously();
	}
	else if (box_selection)
	{
		updateDirtyRect();
	}
}

void EditPointTool::dragFinish()
{
	if (no_more_effect_on_click)
		no_more_effect_on_click = false;
	
	if (editingInProgress())
	{
		finishEditing();
		angle_helper->setActive(false);
		snap_helper->setFilter(SnappingToolHelper::NoSnapping);
	}
	else if (box_selection)
	{
		object_selector->selectBox(click_pos_map, cur_pos_map, active_modifiers & Qt::ShiftModifier);
	}
	box_selection = false;
}

void EditPointTool::focusOutEvent(QFocusEvent* event)
{
	Q_UNUSED(event);
	
	// Deactivate modifiers - not always correct, but should be
	// wrong only in unusual cases and better than leaving the modifiers on forever
	space_pressed = false;
	updateStatusText();
}

bool EditPointTool::keyPress(QKeyEvent* event)
{
	if (text_editor)
	{
		if (event->key() == Qt::Key_Escape)
		{
			finishEditing(); 
			return true;
		}
		return text_editor->keyPressEvent(event);
	}
	
	int num_selected_objects = map()->getNumSelectedObjects();
	
	if (num_selected_objects > 0 && event->key() == delete_object_key)
	{
		deleteSelectedObjects();
	}
	else if (num_selected_objects > 0 && event->key() == Qt::Key_Escape)
	{
		map()->clearObjectSelection(true);
	}
	else if (event->key() == Qt::Key_Control)
	{
		if (editingInProgress())
			activateAngleHelperWhileEditing();
	}
	else if (event->key() == Qt::Key_Shift && editingInProgress())
	{
		if (hover_state == OverObjectNode &&
			hover_object->getType() == Object::Path &&
			hover_object->asPath()->isCurveHandle(hover_point))
		{
			// In this case, Shift just activates deactivates
			// the opposite curve handle constraints
			return true;
		}
		activateSnapHelperWhileEditing();
	}
	else if (event->key() == Qt::Key_Space)
	{
		space_pressed = true;
	}
	else
	{
		return false;
	}
	
	updateStatusText();
	return true;
}

bool EditPointTool::keyRelease(QKeyEvent* event)
{
	if (text_editor)
		return text_editor->keyReleaseEvent(event);
	
	if (event->key() == Qt::Key_Control)
	{
		angle_helper->setActive(false);
		if (editingInProgress())
		{
			calcConstrainedPositions(cur_map_widget);
			dragMove();
		}
	}
	else if (event->key() == Qt::Key_Shift)
	{
		snap_helper->setFilter(SnappingToolHelper::NoSnapping);
		if (editingInProgress())
		{
			calcConstrainedPositions(cur_map_widget);
			dragMove();
		}
	}
	else if (event->key() == Qt::Key_Space)
	{
		space_pressed = false;
	}
	else
	{
		return false;
	}
	
	updateStatusText();
	return true;
}

void EditPointTool::initImpl()
{
	objectSelectionChanged();
	
	if (editor->isInMobileMode())
	{
		// Create key replacement bar
		key_button_bar = new KeyButtonBar(this, editor->getMainWidget());
		key_button_bar->addModifierKey(Qt::Key_Shift, Qt::ShiftModifier, tr("Snap", "Snap to existing objects"));
		key_button_bar->addModifierKey(Qt::Key_Control, Qt::ControlModifier, tr("Point / Angle", "Modify points or use constrained angles"));
		key_button_bar->addModifierKey(Qt::Key_Space, 0, tr("Toggle dash", "Toggle dash points"));
		editor->showPopupWidget(key_button_bar, QString{});
	}
}

void EditPointTool::objectSelectionChangedImpl()
{
	if (text_editor)
	{
		// This case can be reproduced by using "select all objects of symbol" for any symbol while editing a text.
		// Revert selection to text object in order to be able to finish editing. Not optimal, but better than crashing.
		map()->clearObjectSelection(false);
		map()->addObjectToSelection(text_editor->getObject(), false);
		finishEditing();
		map()->emitSelectionChanged();
		return;
	}
	
	updateHoverState(cur_pos_map);
	updateDirtyRect();
	updateStatusText();
}

int EditPointTool::updateDirtyRectImpl(QRectF& rect)
{
	bool show_object_points = map()->selectedObjects().size() <= max_objects_for_handle_display;
	
	selection_extent = QRectF();
	map()->includeSelectionRect(selection_extent);
	
	rectInclude(rect, selection_extent);
	int pixel_border = show_object_points ? (scaleFactor() * 6) : 1;
	
	// Control points
	if (show_object_points)
	{
		for (Map::ObjectSelection::const_iterator it = map()->selectedObjectsBegin(), end = map()->selectedObjectsEnd(); it != end; ++it)
			(*it)->includeControlPointsRect(rect);
	}
	
	// Text selection
	if (text_editor)
		text_editor->includeDirtyRect(rect);
	
	// Box selection
	if (isDragging() && box_selection)
	{
		rectIncludeSafe(rect, click_pos_map);
		rectIncludeSafe(rect, cur_pos_map);
	}
	
	return pixel_border;
}

void EditPointTool::drawImpl(QPainter* painter, MapWidget* widget)
{
	auto num_selected_objects = map()->selectedObjects().size();
	if (num_selected_objects > 0)
	{
		drawSelectionOrPreviewObjects(painter, widget, text_editor != nullptr);
		
		if (!text_editor)
		{
			Object* object = *map()->selectedObjectsBegin();
			if (num_selected_objects == 1 &&
			    object->getType() == Object::Text &&
			    !object->asText()->hasSingleAnchor())
			{
				drawBoundingPath(painter, widget, object->asText()->controlPoints(), hover_state.testFlag(OverFrame) ? active_color : selection_color);
			}
			else if (selection_extent.isValid())
			{
				auto active = hover_state.testFlag(OverFrame) && !hover_state.testFlag(OverObjectNode);
				drawBoundingBox(painter, widget, selection_extent, active ? active_color : selection_color);
			}
			
			if (num_selected_objects <= max_objects_for_handle_display)
			{
				for (const auto object: map()->selectedObjects())
				{
					auto active = hover_state.testFlag(OverObjectNode) && hover_object == object;
					auto hover_point = active ? this->hover_point : no_point;
					pointHandles().draw(painter, widget, object, hover_point, true, PointHandles::NormalHandleState);
				}
			}
		}
	}
	
	// Text editor
	if (text_editor)
	{
		painter->save();
		widget->applyMapTransform(painter);
		text_editor->draw(painter, widget);
		painter->restore();
	}
	
	// Box selection
	if (isDragging() && box_selection)
		drawSelectionBox(painter, widget, click_pos_map, cur_pos_map);
}

void EditPointTool::textSelectionChanged(bool text_change)
{
	Q_UNUSED(text_change);
	updatePreviewObjects();
}

void EditPointTool::finishEditing()
{
	bool create_undo_step = true;
	bool delete_objects = false;
	
	if (text_editor)
	{
		delete text_editor;
		text_editor = nullptr;
		
		TextObject* text_object = reinterpret_cast<TextObject*>(*map()->selectedObjectsBegin());
		if (text_object->getText().isEmpty())
		{
			text_object->setText(old_text);
			text_object->setHorizontalAlignment((TextObject::HorizontalAlignment)old_horz_alignment);
			text_object->setVerticalAlignment((TextObject::VerticalAlignment)old_vert_alignment);
			create_undo_step = false;
			delete_objects = true;
		}
		else if (text_object->getText() == old_text && (int)text_object->getHorizontalAlignment() == old_horz_alignment && (int)text_object->getVerticalAlignment() == old_vert_alignment)
			create_undo_step = false;
	}
	
	MapEditorToolBase::finishEditing(delete_objects, create_undo_step);
	
	if (delete_objects)
		deleteSelectedObjects();
	
	updateStatusText();
}

void EditPointTool::updatePreviewObjects()
{
	MapEditorToolBase::updatePreviewObjects();
	updateStatusText();
}

void EditPointTool::updateStatusText()
{
	QString text;
	if (text_editor)
	{
		text = tr("<b>%1</b>: Finish editing. ").arg(ModifierKey::escape());
	}
	else if (editingInProgress())
	{
		MapCoordF drag_vector = constrained_pos_map - click_pos_map;
		text = EditTool::tr("<b>Coordinate offset:</b> %1, %2 mm  <b>Distance:</b> %3 m ").
		       arg(QLocale().toString(drag_vector.x(), 'f', 1)).
		       arg(QLocale().toString(-drag_vector.y(), 'f', 1)).
		       arg(QLocale().toString(0.001 * map()->getScaleDenominator() * drag_vector.length(), 'f', 1)) +
		       QLatin1String("| ");
		
		if (!angle_helper->isActive())
			text += EditTool::tr("<b>%1</b>: Fixed angles. ").arg(ModifierKey::control());
		
		if (!(active_modifiers & Qt::ShiftModifier))
		{
			if (hover_state == OverObjectNode &&
				hover_object->getType() == Object::Path &&
				hover_object->asPath()->isCurveHandle(hover_point))
			{
				text += tr("<b>%1</b>: Keep opposite handle positions. ").arg(ModifierKey::shift());
			}
			else
			{
				text += EditTool::tr("<b>%1</b>: Snap to existing objects. ").arg(ModifierKey::shift());
			}
		}
	}
	else
	{
		text = EditTool::tr("<b>Click</b>: Select a single object. <b>Drag</b>: Select multiple objects. <b>%1+Click</b>: Toggle selection. ").arg(ModifierKey::shift());
		if (map()->getNumSelectedObjects() > 0)
		{
			text += EditTool::tr("<b>%1</b>: Delete selected objects. ").arg(ModifierKey(delete_object_key));
			
			if (map()->selectedObjects().size() <= max_objects_for_handle_display)
			{
				// TODO: maybe show this only if at least one PathObject among the selected objects?
				if (active_modifiers & Qt::ControlModifier)
				{
					if (addDashPointDefault())
						text = tr("<b>%1+Click</b> on point: Delete it; on path: Add a new dash point; with <b>%2</b>: Add a normal point. ").
						       arg(ModifierKey::control(), ModifierKey::space());
					else
						text = tr("<b>%1+Click</b> on point: Delete it; on path: Add a new point; with <b>%2</b>: Add a dash point. ").
						       arg(ModifierKey::control(), ModifierKey::space());
				}
				else if (space_pressed)
					text = tr("<b>%1+Click</b> on point to switch between dash and normal point. ").arg(ModifierKey::space());
				else
					text += QLatin1String("| ") + MapEditorTool::tr("More: %1, %2").arg(ModifierKey::control(), ModifierKey::space());
			}
		}
	}
	setStatusBarText(text);
}

void EditPointTool::updateHoverState(MapCoordF cursor_pos)
{
	HoverState new_hover_state = OverNothing;
	const Object* new_hover_object = nullptr;
	MapCoordVector::size_type new_hover_point = no_point;
	
	if (text_editor)
	{
		handle_offset = MapCoordF(0, 0);
	}
	else if (!map()->selectedObjects().empty())
	{
		if (map()->selectedObjects().size() <= max_objects_for_handle_display)
		{
			// Try to find object node.
			auto best_distance_sq = std::numeric_limits<double>::max();
			for (const auto object : map()->selectedObjects())
			{
				MapCoordF handle_pos;
				auto hover_point = findHoverPoint(cur_map_widget->mapToViewport(cursor_pos), cur_map_widget, object, true, &handle_pos);
				if (hover_point == no_point)
					continue;
				
				auto distance_sq = cursor_pos.distanceSquaredTo(handle_pos);
				if (distance_sq < best_distance_sq)
				{
					new_hover_state |= OverObjectNode;
					new_hover_object = object;
					new_hover_point  = hover_point;
					best_distance_sq = distance_sq;
					handle_offset    = handle_pos - cursor_pos;
				}
			}
			
			if (!new_hover_state.testFlag(OverObjectNode))
			{
				// No object node found. Try to find path object edge.
				/// \todo De-duplicate: Copied from EditLineTool
				auto click_tolerance_sq = qPow(0.001 * cur_map_widget->getMapView()->pixelToLength(clickTolerance()), 2);
				
				for (auto object : map()->selectedObjects())
				{
					if (object->getType() == Object::Path)
					{
						PathObject* path = object->asPath();
						float distance_sq;
						PathCoord path_coord;
						path->calcClosestPointOnPath(cursor_pos, distance_sq, path_coord);
						
						if (distance_sq >= +0.0 &&
						    distance_sq < best_distance_sq &&
						    distance_sq < qMax(click_tolerance_sq, qPow(path->getSymbol()->calculateLargestLineExtent(map()), 2)))
						{
							new_hover_state |= OverPathEdge;
							new_hover_object = path;
							new_hover_point  = path_coord.index;
							best_distance_sq = distance_sq;
							handle_offset    = path_coord.pos - cursor_pos;
						}
					}
				}
			}
		}
		
		if (!new_hover_state.testFlag(OverObjectNode) && selection_extent.isValid())
		{
			QRectF selection_extent_viewport = cur_map_widget->mapToViewport(selection_extent);
			if (pointOverRectangle(cur_map_widget->mapToViewport(cursor_pos), selection_extent_viewport))
			{
				new_hover_state |= OverFrame;
				handle_offset    = closestPointOnRect(cursor_pos, selection_extent) - cursor_pos;
			}
		}
	}
	
	if (new_hover_state  != hover_state  ||
	    new_hover_object != hover_object ||
	    new_hover_point  != hover_point)
	{
		hover_state = new_hover_state;
		// We have got a Map*, so we may get an non-const Object*.
		hover_object = const_cast<Object*>(new_hover_object);
		hover_point  = new_hover_point;
		if (hover_state != OverNothing)
			start_drag_distance = 0;
		else
			start_drag_distance = Settings::getInstance().getStartDragDistancePx();
		updateDirtyRect();
	}
	
	Q_ASSERT((hover_state.testFlag(OverObjectNode) || hover_state.testFlag(OverPathEdge)) == (hover_object != nullptr));
}

void EditPointTool::setupAngleHelperFromHoverObject()
{
	Q_ASSERT(hover_object->getType() == Object::Path);
	
	angle_helper->clearAngles();
	bool forward_ok = false;
	auto part = hover_object->asPath()->findPartForIndex(hover_point);
	MapCoordF forward_tangent = part->calculateTangent(hover_point, false, forward_ok);
	if (forward_ok)
		angle_helper->addAngles(-forward_tangent.angle(), M_PI/2);
	bool backward_ok = false;
	MapCoordF backward_tangent = part->calculateTangent(hover_point, true, backward_ok);
	if (backward_ok)
		angle_helper->addAngles(-backward_tangent.angle(), M_PI/2);
	
	if (forward_ok && backward_ok)
	{
		double angle = (-backward_tangent.angle() - forward_tangent.angle()) / 2;
		angle_helper->addAngle(angle);
		angle_helper->addAngle(angle + M_PI/2);
		angle_helper->addAngle(angle + M_PI);
		angle_helper->addAngle(angle + 3*M_PI/2);
	}
}

void EditPointTool::startEditingSetup()
{
	snap_exclude_object = hover_object;
	
	// Collect elements to move
	object_mover.reset(new ObjectMover(map(), click_pos_map));
	if (hover_state.testFlag(OverObjectNode))
	{
		switch (hover_object->getType())
		{
		case Object::Point:
			object_mover->addObject(hover_object);
			setupAngleHelperFromSelectedObjects();
			break;
			
		case Object::Path:
			object_mover->addPoint(hover_object->asPath(), hover_point);
			setupAngleHelperFromHoverObject();
			break;
			
		case Object::Text:
			if (hover_object->asText()->hasSingleAnchor())
				object_mover->addObject(hover_object);
			else
				object_mover->addTextHandle(hover_object->asText(), hover_point);
			setupAngleHelperFromSelectedObjects();
			break;
			
		default:
			Q_UNREACHABLE();
		}
		angle_helper->setCenter(click_pos_map);
	}
	else if (hover_state.testFlag(OverFrame))
	{
		for (auto object : map()->selectedObjects())
			object_mover->addObject(object);
		setupAngleHelperFromSelectedObjects();
		angle_helper->setCenter(click_pos_map);
	}
}

bool EditPointTool::hoveringOverSingleText() const
{
	return hover_state == OverNothing &&
	       map()->selectedObjects().size() == 1 &&
	       map()->getFirstSelectedObject()->getType() == Object::Text &&
	       map()->getFirstSelectedObject()->asText()->calcTextPositionAt(cur_pos_map, true) >= 0;
}
