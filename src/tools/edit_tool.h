/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2015-2017 Kai Pastor
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

#ifndef _OPENORIENTEERING_EDIT_TOOL_H_
#define _OPENORIENTEERING_EDIT_TOOL_H_

#include "tool_base.h"

#include <QSet>

class TextObject;
class PathObject;
typedef std::vector< std::pair< int, Object* > > SelectionInfoVector;


/**
 * Implements the object selection logic for edit tools.
 */
class ObjectSelector
{
public:
	/** Creates a selector for the given map. */
	ObjectSelector(Map* map);
	
	/**
	 * Selects an object at the given position.
	 * If there is already an object selected at this position, switches through
	 * the available objects.
	 * @param tolerance maximum, normal selection distance in map units.
	 *    It is enlarged by 1.5 if no objects are found with the normal distance.
	 * @param toggle corresponds to the shift key modifier.
	 * @return true if the selection has changed.
	 */
	bool selectAt(MapCoordF position, double tolerance, bool toggle);
	
	/**
	 * Applies box selection.
	 * @param toggle corresponds to the shift key modifier.
	 * @return true if the selection has changed.
	 */
	bool selectBox(MapCoordF corner1, MapCoordF corner2, bool toggle);
	
	// TODO: move to other place? util.h/cpp or object.h/cpp
	static bool sortObjects(const std::pair<int, Object*>& a, const std::pair<int, Object*>& b);
	
private:
	bool selectionInfosEqual(const SelectionInfoVector& a, const SelectionInfoVector& b);
	
	// Information about the last click
	SelectionInfoVector last_results;
	SelectionInfoVector last_results_ordered;
	int next_object_to_select;
	
	Map* map;
};

/**
 * Implements the logic to move sets of objects and / or object points for edit tools.
 */
class ObjectMover
{
public:
	/** Creates a mover for the map with the given cursor start position. */
	ObjectMover(Map* map, const MapCoordF& start_pos);
	
	/** Sets the start position. */
	void setStartPos(const MapCoordF& start_pos);
	
	/** Adds an object to the set of elements to move. */
	void addObject(Object* object);
	
	/** Adds a point to the set of elements to move. */
	void addPoint(PathObject* object, MapCoordVector::size_type point_index);
	
	/** Adds a line to the set of elements to move. */
	void addLine(PathObject* object, MapCoordVector::size_type start_point_index);
	
	/** Adds a text handle to the set of elements to move. */
	void addTextHandle(TextObject* text, int handle);
	
	/**
	 * Moves the elements.
	 * @param move_opposite_handles If false, opposite handles are reset to their original position.
	 * @param out_dx returns the move along the x coordinate in map units
	 * @param out_dy returns the move along the y coordinate in map units
	 */
	void move(const MapCoordF& cursor_pos, bool move_opposite_handles, qint32* out_dx = NULL, qint32* out_dy = NULL);
	
	/** Overload of move() taking delta values. */
	void move(qint32 dx, qint32 dy, bool move_opposite_handles);
	
private:
	using ObjectSet = QSet<Object*>;
	using CoordIndexSet = QSet<MapCoordVector::size_type>;
	
	CoordIndexSet* insertPointObject(PathObject* object);
	void calculateConstraints();
	
	// Basic information
	MapCoordF start_position;
	qint32 prev_drag_x;
	qint32 prev_drag_y;
	ObjectSet objects;
	QHash< PathObject*, CoordIndexSet> points;
	QHash< TextObject*, int > text_handles;
	
	/** Constraints calculated from the basic information */
	struct OppositeHandleConstraint
	{
		/** Object to which the constraint applies */
		PathObject* object;
		/** Index of moved handle */
		int moved_handle_index;
		/** Index of opposite handle */
		int opposite_handle_index;
		/** Index of center point in the middle of the handles */
		int curve_anchor_index;
		/** Distance of opposite handle to center point */
		double opposite_handle_dist;
		/** Original position of the opposite handle */
		MapCoord opposite_handle_original_position;
	};
	std::vector< OppositeHandleConstraint > handle_constraints;
	bool constraints_calculated;
};


/**
 * Base class for object editing tools.
 */
class EditTool : public MapEditorToolBase
{
public:
	/**
	 * @brief A type for general information on what is hovered over.
	 */
	enum HoverFlag
	{
		OverNothing      = 0,
		OverFrame        = 1,
		OverObjectNode   = 2,
		OverPathEdge     = 4,
	};
	Q_DECLARE_FLAGS(HoverState, HoverFlag)
	
Q_OBJECT
public:
	EditTool(MapEditorController* editor, MapEditorTool::Type tool_type, QAction* tool_action);
	
	virtual ~EditTool();
	
	/**
	 * The platform's key for deleting selected objects.
	 * 
	 * OS X use the backspace key for deleting selected objects,
	 * while other platforms use the delete key.
	 * 
	 * This causes translation issues and inconsistent behaviour on OS X:
	 * - In Finder, moving an object to trash is Cmd+Backspace.
	 * - Other programs are reported to use [forward] delete.
	 * - Some programs are reported to support multiple keys,
	 *   e.g. Delete and Backspace.
	 * - A major source of irritation is the absence of a delete key on some
	 *   Macbooks. On these keyboards, delete is entered as Fn+Backspace.
	 * - Some programs use another key for delete, e.g. "x".
	 *   (Note that Cmd-x (aka Cut) will have a similar effect.)
	 * 
	 * \todo Either use a function for testing whether a key means
	 *       "delete object", or switch to a QAction based implementation
	 *       since QAction supports alternative QKeySequences.
	 */
#ifdef Q_OS_MACOS
	static constexpr Qt::Key DeleteObjectKey = Qt::Key_Backspace;
#else
	static constexpr Qt::Key DeleteObjectKey = Qt::Key_Delete;
#endif
	
protected:
	/**
	 * Deletes all selected objects and updates the status text.
	 */
	void deleteSelectedObjects();
	
	/**
	 * Creates a replace object undo step for the given object.
	 */
	void createReplaceUndoStep(Object* object);
	
	/**
	 * Returns if the point is inside the click_tolerance from the rect's border.
	 */
	bool pointOverRectangle(QPointF point, const QRectF& rect) const;
	
	/**
	 * Returns the point on the rect which is closest to the given point.
	 */
	static MapCoordF closestPointOnRect(MapCoordF point, const QRectF& rect);
	
	/**
	 * Configures the angle helper from the primary directions of the edited objects.
	 * 
	 * If no primary directions are found, the default directions are set.
	 */
	void setupAngleHelperFromEditedObjects();
	
	/**
	 * Draws a bounding box with a dashed line of the given color.
	 * 
	 * @param bounding_box the box extent in map coordinates
	 */
	void drawBoundingBox(QPainter* painter, MapWidget* widget, const QRectF& bounding_box, const QRgb& color);
	
	/**
	 * Draws a bounding path with a dashed line of the given color.
	 * 
	 * @param bounding_box the box extent in map coordinates
	 */
	void drawBoundingPath(QPainter* painter, MapWidget* widget, const std::vector<QPointF>& bounding_path, const QRgb& color);
	
	/**
	 * An utility implementing object selection logic.
	 */
	QScopedPointer<ObjectSelector> object_selector;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(EditTool::HoverState)

#endif
