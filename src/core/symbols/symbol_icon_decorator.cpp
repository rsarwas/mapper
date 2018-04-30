/*
 *    Copyright 2014, 2018 Kai Pastor
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

#include "symbol_icon_decorator.h"

#include <QtMath>
#include <QPainter>


namespace OpenOrienteering {

//### SymbolIconDecorator ###

SymbolIconDecorator::~SymbolIconDecorator() = default;



//### HiddenSymbolDecorator ###

HiddenSymbolDecorator::HiddenSymbolDecorator(int icon_size)
: icon_size(icon_size)
, pen_width(qMax(1, qCeil(0.06*icon_size)))
, x_width(icon_size/3)
, offset(1 + pen_width, 1 + pen_width)
{
	; // nothing else
}

HiddenSymbolDecorator::~HiddenSymbolDecorator() = default;

void HiddenSymbolDecorator::draw(QPainter& painter) const
{
	// Draw a lock symbol
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	
	painter.setOpacity(0.6);
	painter.fillRect(0, 0, icon_size, icon_size, QBrush(Qt::white));
	
	painter.translate(offset);
	painter.setOpacity(1.0);
	QPen pen(Qt::red);
	pen.setWidth(pen_width);
	painter.setPen(pen);
	painter.drawLine(QPoint(0, 0), QPoint(x_width, x_width));
	painter.drawLine(QPoint(x_width, 0), QPoint(0, x_width));
	
	painter.restore();
}



//### ProtectedSymbolDecorator ###

ProtectedSymbolDecorator::ProtectedSymbolDecorator(int icon_size)
: arc_size(qFloor(qMax(3.0, 0.15*icon_size)))
, pen_width(qMax(1, qCeil(0.4*arc_size)))
, box_width(arc_size + pen_width + qMax(1, qFloor(0.1*icon_size)))
, box_height(qMax(arc_size, qCeil(0.6*box_width)))
, offset(icon_size - 3 - box_width, 1 + pen_width)
{
	; // nothing else
}

ProtectedSymbolDecorator::~ProtectedSymbolDecorator() = default;

void ProtectedSymbolDecorator::draw(QPainter& painter) const
{
	// Draw a lock symbol
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.translate(offset);
	
	painter.setOpacity(0.5);
	QPen arc_background_pen(Qt::white);
	arc_background_pen.setWidth(pen_width+2);
	painter.setPen(arc_background_pen);
	painter.drawRoundedRect((box_width-arc_size)/2, 0, arc_size, arc_size+pen_width, qreal(pen_width), qreal(pen_width));
	painter.fillRect(-1, arc_size-1, box_width+2, box_height+2, QBrush(Qt::white));
	
	painter.setOpacity(1.0);
	QPen arc_pen(Qt::darkGray);
	arc_pen.setWidth(pen_width);
	painter.setPen(arc_pen);
	painter.drawRoundedRect((box_width-arc_size)/2, 0, arc_size, arc_size+pen_width, qreal(pen_width), qreal(pen_width));
	painter.fillRect(0, arc_size, box_width, box_height, QBrush(Qt::darkGray));
	
	painter.restore();
}


}  // namespace OpenOrienteering
