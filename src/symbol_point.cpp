/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2012-2015 Kai Pastor
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


#include "symbol_point.h"

#include <QVBoxLayout>
#include <QXmlStreamAttributes>

#include "core/map_color.h"
#include "map.h"
#include "object.h"
#include "symbol_setting_dialog.h"
#include "symbol_properties_widget.h"
#include "symbol_point_editor.h"
#include "renderable_implementation.h"
#include "util.h"
#include "util_gui.h"

PointSymbol::PointSymbol() : Symbol(Symbol::Point)
{
	rotatable = false;
	inner_radius = 1000;
	inner_color = NULL;
	outer_width = 0;
	outer_color = NULL;
}
PointSymbol::~PointSymbol()
{
	int size = objects.size();
	for (int i = 0; i < size; ++i)
	{
		delete objects[i];
		delete symbols[i];
	}
}
Symbol* PointSymbol::duplicate(const MapColorMap* color_map) const
{
	PointSymbol* new_point = new PointSymbol();
	new_point->duplicateImplCommon(this);
	
	new_point->rotatable = rotatable;
	new_point->inner_radius = inner_radius;
	new_point->inner_color = color_map ? color_map->value(inner_color) : inner_color;
	new_point->outer_width = outer_width;
	new_point->outer_color = color_map ? color_map->value(outer_color) : outer_color;
	
	new_point->symbols.resize(symbols.size());
	for (int i = 0; i < (int)symbols.size(); ++i)
		new_point->symbols[i] = symbols[i]->duplicate(color_map);
	
	new_point->objects.resize(objects.size());
	for (int i = 0; i < (int)objects.size(); ++i)
	{
		new_point->objects[i] = objects[i]->duplicate();
		new_point->objects[i]->setSymbol(new_point->symbols[i], true);
	}
	
	return new_point;
}

void PointSymbol::createRenderables(
        const Object* object,
        const VirtualCoordVector& coords,
        ObjectRenderables& output,
        RenderableOptions options ) const
{
	auto point = object->asPoint();
	auto rotation = isRotatable() ? -point->getRotation() : 0.0f;
	
	if (options.testFlag(Symbol::RenderBaselines))
	{
		const MapColor* dominant_color = guessDominantColor();
		if (dominant_color)
		{
			PointSymbol* point = Map::getUndefinedPoint();
			const MapColor* temp_color = point->getInnerColor();
			point->setInnerColor(dominant_color);
			
			point->createRenderablesScaled(coords[0], rotation, output, 1.0f);
			
			point->setInnerColor(temp_color);
		}
	}
	else
	{
		createRenderablesScaled(coords[0], rotation, output, 1.0f);
	}
}

void PointSymbol::createRenderablesScaled(MapCoordF coord, float rotation, ObjectRenderables& output, float coord_scale) const
{
	if (inner_color && inner_radius > 0)
		output.insertRenderable(new DotRenderable(this, coord));
	if (outer_color && outer_width > 0)
		output.insertRenderable(new CircleRenderable(this, coord));
	
	if (!objects.empty())
	{
		auto offset_x = coord.x();
		auto offset_y = coord.y();
		auto cosr = 1.0;
		auto sinr = 0.0;
		if (rotation != 0.0)
		{
			cosr = cos(rotation);
			sinr = sin(rotation);
		}
		
		// Add elements which possibly need to be moved and rotated
		auto size = objects.size();
		for (auto i = 0u; i < size; ++i)
		{
			// Point symbol elements should not be entered into the map,
			// otherwise map settings like area hatching affect them
			Q_ASSERT(!objects[i]->getMap());
			
			const MapCoordVector& object_coords = objects[i]->getRawCoordinateVector();
			
			MapCoordVectorF transformed_coords;
			transformed_coords.reserve(object_coords.size());
			for (auto& coord : object_coords)
			{
				auto ox = coord_scale * coord.x();
				auto oy = coord_scale * coord.y();
				transformed_coords.emplace_back(ox * cosr - oy * sinr + offset_x,
				                                oy * cosr + ox * sinr + offset_y);
			}
			
			// TODO: if this point is rotated, it has to pass it on to its children to make it work that rotatable point objects can be children.
			// But currently only basic, rotationally symmetric points can be children, so it does not matter for now.
			symbols[i]->createRenderables(objects[i], VirtualCoordVector(object_coords, transformed_coords), output, Symbol::RenderNormal);
		}
	}
}

int PointSymbol::getNumElements() const
{
	return (int)objects.size();
}
void PointSymbol::addElement(int pos, Object* object, Symbol* symbol)
{
	objects.insert(objects.begin() + pos, object);
	symbols.insert(symbols.begin() + pos, symbol);
}
Object* PointSymbol::getElementObject(int pos)
{
	return objects[pos];
}
const Object* PointSymbol::getElementObject(int pos) const
{
    return objects[pos];
}
Symbol* PointSymbol::getElementSymbol(int pos)
{
	return symbols[pos];
}
const Symbol* PointSymbol::getElementSymbol(int pos) const
{
    return symbols[pos];
}
void PointSymbol::deleteElement(int pos)
{
	delete objects[pos];
	objects.erase(objects.begin() + pos);
	delete symbols[pos];
	symbols.erase(symbols.begin() + pos);
}

bool PointSymbol::isEmpty() const
{
	return getNumElements() == 0 && (inner_color == NULL || inner_radius == 0) && (outer_color == NULL || outer_width == 0);
}
bool PointSymbol::isSymmetrical() const
{
	int num_elements = (int)objects.size();
	for (int i = 0; i < num_elements; ++i)
	{
		if (symbols[i]->getType() != Symbol::Point)
			return false;
		PointObject* point = reinterpret_cast<PointObject*>(objects[i]);
		if (point->getCoordF() != MapCoordF(0, 0))
			return false;
	}
	return true;
}

void PointSymbol::colorDeleted(const MapColor* color)
{
	bool change = false;
	
	if (color == inner_color)
	{
		inner_color = NULL;
		change = true;
	}
	if (color == outer_color)
	{
		outer_color = NULL;
		change = true;
	}
	
	int num_elements = (int)objects.size();
	for (int i = 0; i < num_elements; ++i)
	{
		symbols[i]->colorDeleted(color);
		change = true;
	}
	
	if (change)
		resetIcon();
}
bool PointSymbol::containsColor(const MapColor* color) const
{
	if (color == inner_color)
		return true;
	if (color == outer_color)
		return true;
	
	int num_elements = (int)objects.size();
	for (int i = 0; i < num_elements; ++i)
	{
		if (symbols[i]->containsColor(color))
			return true;
	}
	
	return false;
}

const MapColor* PointSymbol::guessDominantColor() const
{
	bool have_inner_color = inner_color && inner_radius > 0;
	bool have_outer_color = outer_color && outer_width > 0;
	if (have_inner_color != have_outer_color)
		return have_inner_color ? inner_color : outer_color;
	else if (have_inner_color && have_outer_color)
	{
		if (inner_color->isWhite())
			return outer_color;
		else if (outer_color->isWhite())
			return inner_color;
		else
			return (qPow(inner_radius, 2) * M_PI > qPow(inner_radius + outer_width, 2) * M_PI - qPow(inner_radius, 2) * M_PI) ? inner_color : outer_color;
	}
	else
	{
		// Hope that the first element's color is representative
		if (symbols.size() > 0)
			return symbols[0]->guessDominantColor();
		else
			return NULL;
	}
}

void PointSymbol::scale(double factor)
{
	inner_radius = qRound(inner_radius * factor);
	outer_width = qRound(outer_width * factor);
	
	int size = (int)objects.size();
	for (int i = 0; i < size; ++i)
	{
		symbols[i]->scale(factor);
		objects[i]->scale(MapCoordF(0, 0), factor);
	}
	
	resetIcon();
}

#ifndef NO_NATIVE_FILE_FORMAT

bool PointSymbol::loadImpl(QIODevice* file, int version, Map* map)
{
	file->read((char*)&rotatable, sizeof(bool));
	
	file->read((char*)&inner_radius, sizeof(int));
	int temp;
	file->read((char*)&temp, sizeof(int));
	inner_color = (temp >= 0) ? map->getColor(temp) : NULL;
	
	file->read((char*)&outer_width, sizeof(int));
	file->read((char*)&temp, sizeof(int));
	outer_color = (temp >= 0) ? map->getColor(temp) : NULL;
	
	int num_elements;
	file->read((char*)&num_elements, sizeof(int));
	objects.resize(num_elements);
	symbols.resize(num_elements);
	for (int i = 0; i < num_elements; ++i)
	{
		int save_type;
		file->read((char*)&save_type, sizeof(int));
		symbols[i] = Symbol::getSymbolForType(static_cast<Symbol::Type>(save_type));
		if (!symbols[i])
			return false;
		if (!symbols[i]->load(file, version, map))
			return false;
		
		file->read((char*)&save_type, sizeof(int));
		objects[i] = Object::getObjectForType(static_cast<Object::Type>(save_type), symbols[i]);
		if (!objects[i])
			return false;
		objects[i]->load(file, version, NULL);
	}
	
	return true;
}

#endif

void PointSymbol::saveImpl(QXmlStreamWriter& xml, const Map& map) const
{
	xml.writeStartElement(QString::fromLatin1("point_symbol"));
	if (rotatable)
		xml.writeAttribute(QString::fromLatin1("rotatable"), QString::fromLatin1("true"));
	xml.writeAttribute(QString::fromLatin1("inner_radius"), QString::number(inner_radius));
	xml.writeAttribute(QString::fromLatin1("inner_color"), QString::number(map.findColorIndex(inner_color)));
	xml.writeAttribute(QString::fromLatin1("outer_width"), QString::number(outer_width));
	xml.writeAttribute(QString::fromLatin1("outer_color"), QString::number(map.findColorIndex(outer_color)));
	int num_elements = (int)objects.size();
	xml.writeAttribute(QString::fromLatin1("elements"), QString::number(num_elements));
	for (int i = 0; i < num_elements; ++i)
	{
		xml.writeStartElement(QString::fromLatin1("element"));
		symbols[i]->save(xml, map);
		objects[i]->save(xml);
		xml.writeEndElement(/*element*/);
	}
	xml.writeEndElement(/*point_symbol*/);
}

bool PointSymbol::loadImpl(QXmlStreamReader& xml, const Map& map, SymbolDictionary& symbol_dict)
{
	if (xml.name() != QLatin1String("point_symbol"))
		return false;
	
	QXmlStreamAttributes attributes(xml.attributes());
	rotatable = (attributes.value(QLatin1String("rotatable")) == QLatin1String("true"));
	inner_radius = attributes.value(QLatin1String("inner_radius")).toInt();
	int temp = attributes.value(QLatin1String("inner_color")).toInt();
	inner_color = (temp >= 0) ? map.getColor(temp) : NULL;
	outer_width = attributes.value(QLatin1String("outer_width")).toInt();
	temp = attributes.value(QLatin1String("outer_color")).toInt();
	outer_color = (temp >= 0) ? map.getColor(temp) : NULL;
	int num_elements = attributes.value(QLatin1String("elements")).toInt();
	
	symbols.reserve(qMin(num_elements, 10)); // 10 is not a limit
	objects.reserve(qMin(num_elements, 10)); // 10 is not a limit
	for (int i = 0; xml.readNextStartElement(); ++i)
	{
		if (xml.name() == QLatin1String("element"))
		{
			while (xml.readNextStartElement())
			{
				if (xml.name() == QLatin1String("symbol"))
					symbols.push_back(Symbol::load(xml, map, symbol_dict));
				else if (xml.name() == QLatin1String("object"))
					objects.push_back(Object::load(xml, NULL, symbol_dict, symbols.back()));
				else
					xml.skipCurrentElement(); // unknown element
			}
		}
		else
			xml.skipCurrentElement(); // unknown element
	}
	return true;
}

bool PointSymbol::equalsImpl(const Symbol* other, Qt::CaseSensitivity case_sensitivity) const
{
	const PointSymbol* point = static_cast<const PointSymbol*>(other);
	
	if (rotatable != point->rotatable)
		return false;
	if (!MapColor::equal(inner_color, point->inner_color))
		return false;
	if (inner_color && inner_radius != point->inner_radius)
		return false;
	if (!MapColor::equal(outer_color, point->outer_color))
		return false;
	if (outer_color && outer_width != point->outer_width)
		return false;
	
	if (symbols.size() != point->symbols.size())
		return false;
	// TODO: Comparing the contained elements in fixed order does not find every case of identical points symbols
	// (but at least if the point symbol has not been changed, it is always seen as identical). Could be improved.
	for (int i = 0, size = (int)objects.size(); i < size; ++i)
	{
		if (!symbols[i]->equals(point->symbols[i], case_sensitivity))
			return false;
		if (!objects[i]->equals(point->objects[i], false))
			return false;
	}
	
	return true;
}

SymbolPropertiesWidget* PointSymbol::createPropertiesWidget(SymbolSettingDialog* dialog)
{
	return new PointSymbolSettings(this, dialog);
}


// ### PointSymbolSettings ###

PointSymbolSettings::PointSymbolSettings(PointSymbol* symbol, SymbolSettingDialog* dialog)
: SymbolPropertiesWidget(symbol, dialog), 
  symbol(symbol)
{
	symbol_editor = new PointSymbolEditorWidget(dialog->getPreviewController(), symbol, 0, true);
	connect(symbol_editor, SIGNAL(symbolEdited()), this, SIGNAL(propertiesModified()) );
	
	layout = new QVBoxLayout();
	layout->addWidget(symbol_editor);
	
	point_tab = new QWidget();
	point_tab->setLayout(layout);
	addPropertiesGroup(tr("Point symbol"), point_tab);
	
	connect(this, SIGNAL(currentChanged(int)), this, SLOT(tabChanged(int)));
}

void PointSymbolSettings::reset(Symbol* symbol)
{
	Q_ASSERT(symbol->getType() == Symbol::Point);
	
	SymbolPropertiesWidget::reset(symbol);
	this->symbol = reinterpret_cast<PointSymbol*>(symbol);
	
	layout->removeWidget(symbol_editor);
	delete(symbol_editor);
	
	symbol_editor = new PointSymbolEditorWidget(dialog->getPreviewController(), this->symbol, 0, true);
	connect(symbol_editor, SIGNAL(symbolEdited()), this, SIGNAL(propertiesModified()) );
	layout->addWidget(symbol_editor);
}

void PointSymbolSettings::tabChanged(int index)
{
	Q_UNUSED(index);
	symbol_editor->setEditorActive( currentWidget()==point_tab );
}
