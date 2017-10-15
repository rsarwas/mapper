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

#ifndef OPENORIENTEERING_OBJECT_MOVER_H
#define OPENORIENTEERING_OBJECT_MOVER_H

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <QtGlobal>

#include "core/map_coord.h"

class Map;
class Object;
class PathObject;
class TextObject;
using SelectionInfoVector = std::vector<std::pair<int, Object*>>;


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
	void move(const MapCoordF& cursor_pos, bool move_opposite_handles, qint32* out_dx = nullptr, qint32* out_dy = nullptr);
	
	/** Overload of move() taking delta values. */
	void move(qint32 dx, qint32 dy, bool move_opposite_handles);
	
private:
	using ObjectSet = std::unordered_set<Object*>;
	using CoordIndexSet = std::unordered_set<MapCoordVector::size_type>;
	
	CoordIndexSet* insertPointObject(PathObject* object);
	void calculateConstraints();
	
	// Basic information
	MapCoordF start_position;
	qint32 prev_drag_x;
	qint32 prev_drag_y;
	ObjectSet objects;
	std::unordered_map<PathObject*, CoordIndexSet> points;
	std::unordered_map<TextObject*, int> text_handles;
	
	/** Constraints calculated from the basic information */
	struct OppositeHandleConstraint
	{
		/** Object to which the constraint applies */
		PathObject* object;
		/** Index of moved handle */
		MapCoordVector::size_type moved_handle_index;
		/** Index of opposite handle */
		MapCoordVector::size_type opposite_handle_index;
		/** Index of center point in the middle of the handles */
		MapCoordVector::size_type curve_anchor_index;
		/** Distance of opposite handle to center point */
		qreal opposite_handle_dist;
		/** Original position of the opposite handle */
		MapCoord opposite_handle_original_position;
	};
	std::vector<OppositeHandleConstraint> handle_constraints;
	bool constraints_calculated;
};


#endif
