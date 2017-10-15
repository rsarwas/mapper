/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2012-2017 Kai Pastor
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


#include "combined_symbol.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

#include <QtGlobal>
#include <QIODevice>
#include <QLatin1String>
#include <QString>
#include <QStringRef>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/map.h"
#include "core/map_color.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"


CombinedSymbol::CombinedSymbol()
: Symbol{Symbol::Combined}
, private_parts{ false, false }
, parts{ private_parts.size(), nullptr }
{
	Q_ASSERT(private_parts.size() == parts.size());
	// nothing else
}

CombinedSymbol::~CombinedSymbol()
{
	auto is_private = begin(private_parts);
	for (auto subsymbol : parts)
	{
		if (*is_private)
			delete subsymbol;
		++is_private;
	};
}

Symbol* CombinedSymbol::duplicate(const MapColorMap* color_map) const
{
	auto new_symbol = new CombinedSymbol();
	new_symbol->duplicateImplCommon(this);
	new_symbol->parts = parts;
	new_symbol->private_parts = private_parts;
	auto is_private = begin(new_symbol->private_parts);
	for (auto& subsymbol : new_symbol->parts)
	{
		if (*is_private)
			subsymbol = subsymbol->duplicate(color_map);
		++is_private;
	}
	return new_symbol;
}



bool CombinedSymbol::validate() const
{
	return std::all_of(begin(parts), end(parts), [](auto& symbol) { return symbol->validate(); });
}



void CombinedSymbol::createRenderables(
        const Object *object,
        const VirtualCoordVector &coords,
        ObjectRenderables &output,
        Symbol::RenderableOptions options) const        
{
	auto path = static_cast<const PathObject*>(object);
	PathPartVector path_parts = PathPart::calculatePathParts(coords);
	createRenderables(path, path_parts, output, options);
}

void CombinedSymbol::createRenderables(
        const PathObject* object,
        const PathPartVector& path_parts,
        ObjectRenderables &output,
        Symbol::RenderableOptions options) const
{
	for (auto subsymbol : parts)
	{
		if (subsymbol)
			subsymbol->createRenderables(object, path_parts, output, options);
	}
}

void CombinedSymbol::colorDeleted(const MapColor* color)
{
	if (containsColor(color))
		resetIcon();
	
	auto is_private = begin(private_parts);
	for (auto subsymbol : parts)
	{
		if (*is_private)
			const_cast<Symbol*>(subsymbol)->colorDeleted(color);
		++is_private;
	};
}

bool CombinedSymbol::containsColor(const MapColor* color) const
{
	return std::any_of(begin(parts), end(parts), [color](const auto& part) {
		return part && part->containsColor(color);
	});
}

const MapColor* CombinedSymbol::guessDominantColor() const
{
	// Speculative heuristic. Prefers areas and non-white colors.
	const MapColor* dominant_color = nullptr;
	for (auto subsymbol : parts)
	{
		if (subsymbol && subsymbol->getContainedTypes() & Symbol::Area)
		{
			dominant_color = subsymbol->guessDominantColor();
			if (dominant_color && !dominant_color->isWhite())
				return dominant_color;
		}
	}
	
	if (dominant_color)
		return dominant_color;
	
	for (auto subsymbol : parts)
	{
		if (subsymbol && !(subsymbol->getContainedTypes() & Symbol::Area))
		{
			dominant_color = subsymbol->guessDominantColor();
			if (dominant_color && !dominant_color->isWhite())
				return dominant_color;
		}
	}
	
	return dominant_color;
}

bool CombinedSymbol::symbolChanged(const Symbol* old_symbol, const Symbol* new_symbol)
{
	bool have_symbol = false;
	for (auto& subsymbol : parts)
	{
		if (subsymbol == old_symbol)
		{
			have_symbol = true;
			subsymbol = new_symbol;
		}
	}
	
	// always invalidate the icon, since the parts might have changed.
	resetIcon();
	
	return have_symbol;
}

bool CombinedSymbol::containsSymbol(const Symbol* symbol) const
{
	for (auto subsymbol : parts)
	{
		if (subsymbol == symbol)
			return true;
		
		if (subsymbol == nullptr)
			continue;
		
		if (subsymbol->getType() == Symbol::Combined)	// TODO: see TODO in SymbolDropDown constructor.
		{
			const CombinedSymbol* combined_symbol = reinterpret_cast<const CombinedSymbol*>(subsymbol);
			if (combined_symbol->containsSymbol(symbol))
				return true;
		}
	}
	return false;
}

void CombinedSymbol::scale(double factor)
{
	auto is_private = begin(private_parts);
	for (auto subsymbol : parts)
	{
		if (*is_private)
			const_cast<Symbol*>(subsymbol)->scale(factor);
		++is_private;
	};
	
	resetIcon();
}

Symbol::Type CombinedSymbol::getContainedTypes() const
{
	auto type = int(getType());
	
	for (auto subsymbol : parts)
	{
		if (subsymbol)
			type |= subsymbol->getContainedTypes();
	}
	
	return Type(type);
}

#ifndef NO_NATIVE_FILE_FORMAT

bool CombinedSymbol::loadImpl(QIODevice* file, int version, Map* map)
{
	int size;
	file->read((char*)&size, sizeof(int));
	temp_part_indices.resize(size);
	parts.resize(size);
	
	for (int i = 0; i < size; ++i)
	{
		bool is_private = false;
		if (version >= 22)
			file->read((char*)&is_private, sizeof(bool));
		private_parts[i] = is_private;
		
		if (is_private)
		{
			// Note on const_cast: private part is owned by this symbol.
			if (!Symbol::loadSymbol(const_cast<Symbol*&>(parts[i]), file, version, map))
				return false;
			temp_part_indices[i] = -1;
		}
		else
		{
			int temp;
			file->read((char*)&temp, sizeof(int));
			temp_part_indices[i] = temp;
		}
	}
	return true;
}

#endif

void CombinedSymbol::saveImpl(QXmlStreamWriter& xml, const Map& map) const
{
	xml.writeStartElement(QString::fromLatin1("combined_symbol"));
	int num_parts = int(parts.size());
	xml.writeAttribute(QString::fromLatin1("parts"), QString::number(num_parts));
	
	auto is_private = begin(private_parts);
	for (const auto subsymbol : parts)
	{
		xml.writeStartElement(QString::fromLatin1("part"));
		if (*is_private)
		{
			xml.writeAttribute(QString::fromLatin1("private"), QString::fromLatin1("true"));
			subsymbol->save(xml, map);
		}
		else
		{
			auto index = map.findSymbolIndex(subsymbol);
			xml.writeAttribute(QString::fromLatin1("symbol"), QString::number(index));
		}
		xml.writeEndElement(/*part*/);
		++is_private;
	}
	xml.writeEndElement(/*combined_symbol*/);
}

bool CombinedSymbol::loadImpl(QXmlStreamReader& xml, const Map& map, SymbolDictionary& symbol_dict)
{
	if (xml.name() != QLatin1String("combined_symbol"))
		return false;
	
	int num_parts = xml.attributes().value(QLatin1String("parts")).toInt();
	temp_part_indices.reserve(num_parts % 10); // 10 is not the limit
	private_parts.clear();
	private_parts.reserve(num_parts % 10);
	parts.clear();
	parts.reserve(num_parts % 10);
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("part"))
		{
			bool is_private = (xml.attributes().value(QLatin1String("private")) == QLatin1String("true"));
			private_parts.push_back(is_private);
			if (is_private)
			{
				xml.readNextStartElement();
				parts.push_back(Symbol::load(xml, map, symbol_dict));
				temp_part_indices.push_back(-1);
			}
			else
			{
				int temp = xml.attributes().value(QLatin1String("symbol")).toInt();
				temp_part_indices.push_back(temp);
				parts.push_back(nullptr);
			}
			xml.skipCurrentElement();
		}
		else
			xml.skipCurrentElement(); // unknown
	}
	
	return true;
}

bool CombinedSymbol::equalsImpl(const Symbol* other, Qt::CaseSensitivity case_sensitivity) const
{
	const CombinedSymbol* combination = static_cast<const CombinedSymbol*>(other);
	return parts.size() == combination->parts.size()
	       && std::equal(begin(private_parts), end(private_parts), begin(combination->private_parts))
	       && std::equal(begin(parts), end(parts), begin(combination->parts),
	                      [case_sensitivity](const auto lhs, const auto rhs)
	{
		return (!lhs && !rhs)
		       || (lhs && rhs && lhs->equals(rhs, case_sensitivity));
	});
}

bool CombinedSymbol::loadFinished(Map* map)
{
	const auto num_symbols = map->getNumSymbols();
	const auto last = std::find_if(begin(temp_part_indices), end(temp_part_indices),
	                               [num_symbols](const auto index)
	{
		return index >= num_symbols;
	});
	
	std::transform(begin(temp_part_indices), last, begin(parts), begin(parts),
	               [map](const auto index, const auto& subsymbol) -> const Symbol*
	{
		return (index < 0) ? subsymbol : map->getSymbol(index);
	});
	
	return last == end(temp_part_indices);
}

float CombinedSymbol::calculateLargestLineExtent(Map* map) const
{
	float result = 0;
	for (auto subsymbol : parts)
	{
		if (subsymbol)
			result = qMax(result, subsymbol->calculateLargestLineExtent(map));
	}
	return result;
}

void CombinedSymbol::setPart(int i, const Symbol* symbol, bool is_private)
{
	const auto index = std::size_t(i);
	
	if (private_parts[index])
		delete parts[index];
	
	parts[index] = symbol;
	private_parts[index] = symbol ? is_private : false;
}
